#pragma once
// WAV audio reading for ltc_scan235.
// Only 48 kHz, 16-bit PCM, mono or stereo WAV files are supported.
// The file is read in chunks; only the selected timecode channel is
// deinterleaved, the source recording is never modified.

#include <cstdint>
#include <string>

class WavReader {
public:
    WavReader() = default;
    ~WavReader();

    WavReader(const WavReader&) = delete;
    WavReader& operator=(const WavReader&) = delete;

    // Opens the file and validates format. channel: 0 or 1, or -1 for auto
    // (mono -> 0; stereo -> 0 unless the caller overrides).
    // Returns false and fills error on any problem.
    bool open(const std::string& path, int channel, std::string& error);

    // Reads up to maxFrames sample frames; only the selected channel is
    // written to out (mono stream of int16 samples). Returns frames read
    // (0 at end of file).
    uint64_t readChunk(int16_t* out, uint64_t maxFrames);

    uint32_t sampleRate() const { return 48000; }
    uint32_t channels() const { return channels_; }
    int selectedChannel() const { return channel_; }
    uint64_t totalFrames() const { return totalFrames_; }

private:
    void* wav_ = nullptr;       // drwav handle (opaque to keep header clean)
    uint32_t channels_ = 0;
    int channel_ = 0;
    uint64_t totalFrames_ = 0;
    int16_t* interleaveBuf_ = nullptr;
    uint64_t interleaveCap_ = 0;
};

