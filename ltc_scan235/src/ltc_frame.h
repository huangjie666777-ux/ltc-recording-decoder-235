#pragma once
// SMPTE 80-bit LTC frame assembly and validation for ltc_scan235.
//
// Bits from the biphase decoder are shifted into an 80-bit window. The
// sync word (bits 64..79 = 0011 1111 1111 1101, LSB first) aligns frames.
// While locked, frames are expected every 80 bits; a sync mismatch or an
// invalid timecode unlocks the assembler and the search resumes, so
// partial frames at the head or tail of a recording are never delivered.

#include <array>
#include <cstdint>
#include <functional>
#include <string>

struct LtcFrame {
    int hours = 0, minutes = 0, seconds = 0, frames = 0;
    std::array<uint8_t, 80> bits{};  // raw 80 bits, bit 0 first
    double sampleStart = 0.0;        // absolute sample position of bit 0 cell
    double sampleEnd = 0.0;          // one past the bit 79 cell (half-open)
};

class FrameAssembler {
public:
    // Validated frame ready for delivery.
    using FrameCallback = std::function<void(const LtcFrame&)>;
    // A sync-aligned frame failed validation (bad sync word, bad BCD,
    // out-of-range field, drop-frame flag set). Ends any continuous run.
    using BadFrameCallback = std::function<void()>;

    void setFrameCallback(FrameCallback cb) { frameCb_ = std::move(cb); }
    void setBadFrameCallback(BadFrameCallback cb) { badCb_ = std::move(cb); }

    // One decoded bit with its cell interval [cellStart, cellEnd).
    void feedBit(int bit, double cellStart, double cellEnd);

    // Signal gap from the biphase decoder: drop lock immediately.
    void signalGap();

    // End of stream.
    void finish();

    // Validates the 80 bits; fills frame fields. Returns false if invalid.
    static bool validate(const std::array<uint8_t, 80>& bits, LtcFrame& out);

    // "HH:MM:SS:FF"
    static std::string timecodeString(const LtcFrame& f);

    // Frame counter value for continuity checks (25 fps, midnight wrap).
    static int64_t frameIndex(const LtcFrame& f);

private:
    void checkWindow();

    FrameCallback frameCb_;
    BadFrameCallback badCb_;

    std::array<uint8_t, 80> window_{};
    std::array<double, 80> cellStart_{};
    std::array<double, 80> cellEnd_{};
    int windowFill_ = 0;

    bool locked_ = false;
    int bitsSinceSync_ = 0;
};

