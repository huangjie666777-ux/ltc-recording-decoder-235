// ltc_scan235: offline LTC (SMPTE 80-bit, 25 fps, non-drop) scanner.
//
// Usage: ltc_scan235 <input.wav> [--channel N|auto] [--chunk N]
//                    [--summary] [--output file.json]
//
// Reads a 48 kHz / 16-bit PCM mono or stereo WAV, decodes the biphase-mark
// timecode on the selected channel, and prints JSON to stdout (or a file).
// The source recording is never modified.

#include "audio.h"
#include "biphase.h"
#include "ltc_frame.h"
#include "segments.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

using nlohmann::json;

static std::string bitsToHex(const std::array<uint8_t, 80>& bits) {
    // 80 bits -> 20 hex chars, bit 0 is the LSB of the first nibble pair.
    std::string hex;
    hex.reserve(20);
    for (int i = 0; i < 80; i += 4) {
        int v = bits[i] | (bits[i+1] << 1) | (bits[i+2] << 2) | (bits[i+3] << 3);
        hex += "0123456789abcdef"[v];
    }
    return hex;
}

int main(int argc, char** argv) {
    std::string input, outputPath;
    int channel = -1;             // auto
    uint64_t chunk = 4096;
    bool summaryOnly = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--channel") {
            std::string v = need("--channel");
            channel = (v == "auto") ? -1 : std::stoi(v);
        } else if (a == "--chunk") {
            chunk = std::stoull(need("--chunk"));
            if (chunk == 0) chunk = 4096;
        } else if (a == "--summary") {
            summaryOnly = true;
        } else if (a == "--output" || a == "-o") {
            outputPath = need("--output");
        } else if (a == "--help" || a == "-h") {
            std::printf("usage: ltc_scan235 <input.wav> [--channel N|auto] "
                        "[--chunk N] [--summary] [--output file.json]\n");
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            return 2;
        } else {
            input = a;
        }
    }
    if (input.empty()) {
        std::fprintf(stderr, "no input file (see --help)\n");
        return 2;
    }

    WavReader wav;
    std::string error;
    if (!wav.open(input, channel, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    // Pipeline: WAV chunks -> biphase bits -> 80-bit frames -> segments.
    json segmentsJson = json::array();
    uint64_t totalFrames = 0;

    SegmentBuilder segments;
    segments.setSegmentCallback([&](Segment& seg) {
        totalFrames += seg.frames.size();
        json sj;
        const LtcFrame& first = seg.frames.front();
        const LtcFrame& last = seg.frames.back();
        sj["index"] = segmentsJson.size();
        sj["first_timecode"] = FrameAssembler::timecodeString(first);
        sj["last_timecode"] = FrameAssembler::timecodeString(last);
        sj["frame_count"] = seg.frames.size();
        sj["start_sample"] = (uint64_t)llround(first.sampleStart);
        sj["end_sample"] = (uint64_t)llround(last.sampleEnd);
        sj["start_seconds"] = first.sampleStart / 48000.0;
        sj["end_seconds"] = last.sampleEnd / 48000.0;
        sj["end_reason"] = seg.endReason;
        if (!summaryOnly) {
            json frames = json::array();
            for (const LtcFrame& f : seg.frames) {
                json fj;
                fj["timecode"] = FrameAssembler::timecodeString(f);
                fj["bits"] = bitsToHex(f.bits);
                fj["sample_start"] = (uint64_t)llround(f.sampleStart);
                fj["sample_end"] = (uint64_t)llround(f.sampleEnd);
                fj["seconds_start"] = f.sampleStart / 48000.0;
                fj["seconds_end"] = f.sampleEnd / 48000.0;
                frames.push_back(std::move(fj));
            }
            sj["frames"] = std::move(frames);
        }
        segmentsJson.push_back(std::move(sj));
    });

    FrameAssembler assembler;
    assembler.setFrameCallback([&](const LtcFrame& f) { segments.onFrame(f); });
    assembler.setBadFrameCallback([&]() { segments.onBadFrame(); });

    BiphaseDecoder decoder(48000, 25);
    decoder.setBitCallback([&](int bit, double cs, double ce) {
        assembler.feedBit(bit, cs, ce);
    });
    decoder.setGapCallback([&]() {
        assembler.signalGap();
        segments.onGap();
    });

    std::vector<int16_t> buf(chunk);
    uint64_t base = 0, got;
    while ((got = wav.readChunk(buf.data(), chunk)) > 0) {
        decoder.feed(buf.data(), got, base);
        base += got;
    }
    decoder.finish();
    assembler.finish();
    segments.finish();

    json out;
    out["file"] = input;
    out["sample_rate"] = 48000;
    out["channels"] = wav.channels();
    out["timecode_channel"] = wav.selectedChannel();
    out["fps"] = 25;
    out["drop_frame"] = false;
    out["segment_count"] = segmentsJson.size();
    out["total_frames"] = totalFrames;
    out["segments"] = std::move(segmentsJson);

    std::string text = out.dump(2);
    if (outputPath.empty()) {
        std::fputs(text.c_str(), stdout);
        std::fputc('\n', stdout);
    } else {
        FILE* fp = std::fopen(outputPath.c_str(), "w");
        if (!fp) {
            std::fprintf(stderr, "cannot write %s\n", outputPath.c_str());
            return 1;
        }
        std::fputs(text.c_str(), fp);
        std::fputc('\n', fp);
        std::fclose(fp);
    }
    return 0;
}

