// gen_wav: reproducible LTC test-signal generator for ltc_scan235.
//
// Synthesises a 48 kHz / 16-bit PCM WAV containing SMPTE 80-bit LTC
// (25 fps, non-drop) encoded as biphase-mark on one channel. Supports
// leading silence, polarity inversion, constant speed deviation, gaps,
// timecode jumps, corrupted frames and mid-frame recording starts.
//
// Prints a JSON object to stdout with the expected timecodes in order
// (corrupted frames excluded), for use by the test suite.

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

static const uint8_t kSyncBits[16] = {0,0,1,1, 1,1,1,1, 1,1,1,1, 1,1,0,1};

struct Tc { int h, m, s, f; };

static void putBcd(std::vector<uint8_t>& bits, int lo, int n, int v) {
    for (int i = 0; i < n; ++i) bits[lo + i] = (v >> i) & 1;
}

static std::vector<uint8_t> buildFrame(const Tc& tc, bool dropFlag, bool corrupt) {
    std::vector<uint8_t> bits(80, 0);
    int fr = corrupt ? 31 : tc.f;  // 31 is an illegal frame number at 25 fps
    putBcd(bits, 0, 4, fr % 10);
    putBcd(bits, 8, 2, fr / 10);
    bits[10] = dropFlag ? 1 : 0;
    putBcd(bits, 16, 4, tc.s % 10);
    putBcd(bits, 24, 3, tc.s / 10);
    putBcd(bits, 32, 4, tc.m % 10);
    putBcd(bits, 40, 3, tc.m / 10);
    putBcd(bits, 48, 4, tc.h % 10);
    putBcd(bits, 56, 2, tc.h / 10);
    for (int i = 0; i < 16; ++i) bits[64 + i] = kSyncBits[i];
    return bits;
}

static void advance(Tc& tc, int n) {
    long idx = (((long)tc.h * 60 + tc.m) * 60 + tc.s) * 25 + tc.f;
    idx = (idx + n) % (24L * 60 * 60 * 25);
    tc.h = (int)(idx / (25 * 3600));
    tc.m = (int)(idx / (25 * 60)) % 60;
    tc.s = (int)(idx / 25) % 60;
    tc.f = (int)(idx % 25);
}

int main(int argc, char** argv) {
    std::string outPath;
    Tc tc{1, 0, 0, 0};
    int numFrames = 250;
    int channels = 2, tcChannel = 0;
    double silence = 0.5;      // leading silence, seconds
    double gapLen = 0.0;       // mid-stream silence, seconds
    int gapAt = -1;            // frame index where the gap starts
    int jumpAt = -1, jumpBy = 100;
    int corruptAt = -1;        // frame index to corrupt (illegal BCD)
    int dropAt = -1;           // frame index with drop-frame flag set
    int startOffsetBits = 0;   // skip this many bits of the first frame
    bool invert = false;
    double speed = 1.0;        // playback speed factor (1.02 = +2%)
    double amp = 0.8;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "missing value for %s\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--out") outPath = need();
        else if (a == "--start") std::sscanf(need().c_str(), "%d:%d:%d:%d", &tc.h, &tc.m, &tc.s, &tc.f);
        else if (a == "--frames") numFrames = std::stoi(need());
        else if (a == "--channels") channels = std::stoi(need());
        else if (a == "--tc-channel") tcChannel = std::stoi(need());
        else if (a == "--silence") silence = std::stod(need());
        else if (a == "--gap-at") gapAt = std::stoi(need());
        else if (a == "--gap-len") gapLen = std::stod(need());
        else if (a == "--jump-at") jumpAt = std::stoi(need());
        else if (a == "--jump-by") jumpBy = std::stoi(need());
        else if (a == "--corrupt-at") corruptAt = std::stoi(need());
        else if (a == "--dropflag-at") dropAt = std::stoi(need());
        else if (a == "--start-offset-bits") startOffsetBits = std::stoi(need());
        else if (a == "--invert") invert = true;
        else if (a == "--speed") speed = std::stod(need());
        else if (a == "--amp") amp = std::stod(need());
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (outPath.empty()) { std::fprintf(stderr, "--out required\n"); return 2; }

    const double halfCell = 12.0 / speed;  // samples per half bit cell
    const int16_t level = (int16_t)(amp * 30000.0);

    std::vector<int16_t> mono;  // timecode channel only
    auto appendSilence = [&](uint64_t n) {
        mono.insert(mono.end(), n, 0);
    };
    appendSilence((uint64_t)(silence * 48000.0));

    nlohmann::json expected = nlohmann::json::array();
    double acc = 0.0;      // fractional sample accumulator
    int sign = invert ? -1 : 1;

    auto halfCellOut = [&]() {
        acc += halfCell;
        uint64_t n = (uint64_t)acc;
        acc -= (double)n;
        mono.insert(mono.end(), n, (int16_t)(sign * level));
    };

    for (int fi = 0; fi < numFrames; ++fi) {
        if (fi == gapAt) {
            sign = -sign;          // closing edge before the signal stops
            halfCellOut();
            appendSilence((uint64_t)(gapLen * 48000.0));
        }
        if (fi == jumpAt) advance(tc, jumpBy);

        bool corrupt = (fi == corruptAt);
        bool drop = (fi == dropAt);
        std::vector<uint8_t> bits = buildFrame(tc, drop, corrupt);
        if (!corrupt && !drop && !(fi == 0 && startOffsetBits > 0)) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%02d:%02d:%02d:%02d", tc.h, tc.m, tc.s, tc.f);
            expected.push_back(buf);
        }
        int b0 = (fi == 0) ? startOffsetBits : 0;
        for (int b = b0; b < 80; ++b) {
            sign = -sign;          // edge at cell boundary
            halfCellOut();
            if (bits[b]) sign = -sign; // 1: extra mid-cell edge
            halfCellOut();
        }
        advance(tc, 1);
    }
    sign = -sign;          // closing edge for the final bit cell
    halfCellOut();
    appendSilence(4800);  // trailing silence

    // Interleave into the target channel count.
    drwav_data_format fmt;
    fmt.container = drwav_container_riff;
    fmt.format = DR_WAVE_FORMAT_PCM;
    fmt.channels = (uint32_t)channels;
    fmt.sampleRate = 48000;
    fmt.bitsPerSample = 16;
    drwav wav;
    if (!drwav_init_file_write(&wav, outPath.c_str(), &fmt, nullptr)) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::vector<int16_t> inter(mono.size() * channels, 0);
    for (size_t i = 0; i < mono.size(); ++i)
        inter[i * channels + tcChannel] = mono[i];
    drwav_write_pcm_frames(&wav, mono.size(), inter.data());
    drwav_uninit(&wav);

    nlohmann::json meta;
    meta["expected_timecodes"] = expected;
    meta["total_samples"] = mono.size();
    std::puts(meta.dump().c_str());
    return 0;
}

