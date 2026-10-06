#include "ltc_frame.h"

#include <cstdio>

// Sync word: bits 64..79 in transmission order are
// 0011 1111 1111 1101 (the SMPTE sync word, 0x3FFD read LSB-first).
static const uint8_t kSyncBits[16] = {0,0,1,1, 1,1,1,1, 1,1,1,1, 1,1,0,1};

static bool windowHasSync(const std::array<uint8_t, 80>& w) {
    for (int i = 0; i < 16; ++i)
        if (w[64 + i] != kSyncBits[i]) return false;
    return true;
}

static int bcd(const std::array<uint8_t, 80>& b, int lo, int n) {
    int v = 0;
    for (int i = n - 1; i >= 0; --i) v = (v << 1) | b[lo + i];
    return v;
}

bool FrameAssembler::validate(const std::array<uint8_t, 80>& bits, LtcFrame& out) {
    if (!windowHasSync(bits)) return false;
    if (bits[10] != 0) return false;  // drop-frame flag: unsupported

    int frameUnits = bcd(bits, 0, 4);
    int frameTens  = bcd(bits, 8, 2);
    int secUnits   = bcd(bits, 16, 4);
    int secTens    = bcd(bits, 24, 3);
    int minUnits   = bcd(bits, 32, 4);
    int minTens    = bcd(bits, 40, 3);
    int hourUnits  = bcd(bits, 48, 4);
    int hourTens   = bcd(bits, 56, 2);

    // Reject non-BCD nibbles and out-of-range fields.
    if (frameUnits > 9 || secUnits > 9 || minUnits > 9 || hourUnits > 9)
        return false;
    int frames = frameTens * 10 + frameUnits;
    int secs   = secTens * 10 + secUnits;
    int mins   = minTens * 10 + minUnits;
    int hours  = hourTens * 10 + hourUnits;
    if (frames > 24 || secs > 59 || mins > 59 || hours > 23) return false;

    out.hours = hours; out.minutes = mins; out.seconds = secs; out.frames = frames;
    out.bits = bits;
    return true;
}

std::string FrameAssembler::timecodeString(const LtcFrame& f) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d:%02d",
                  f.hours, f.minutes, f.seconds, f.frames);
    return buf;
}

int64_t FrameAssembler::frameIndex(const LtcFrame& f) {
    return (((int64_t)f.hours * 60 + f.minutes) * 60 + f.seconds) * 25 + f.frames;
}

void FrameAssembler::feedBit(int bit, double cellStart, double cellEnd) {
    // Shift the 80-bit window left by one, append the new bit at 79.
    for (int i = 0; i < 79; ++i) {
        window_[i] = window_[i + 1];
        cellStart_[i] = cellStart_[i + 1];
        cellEnd_[i] = cellEnd_[i + 1];
    }
    window_[79] = (uint8_t)(bit & 1);
    cellStart_[79] = cellStart;
    cellEnd_[79] = cellEnd;
    if (windowFill_ < 80) ++windowFill_;

    if (locked_) {
        ++bitsSinceSync_;
        if (bitsSinceSync_ == 80) {
            bitsSinceSync_ = 0;
            checkWindow();
            if (!locked_) {
                // Sync lost: the window may already contain a new
                // alignment; checkWindow handles relock attempts below
                // on subsequent bits.
            }
        }
    } else if (windowFill_ == 80) {
        if (windowHasSync(window_)) {
            locked_ = true;
            bitsSinceSync_ = 0;
            checkWindow();
        }
    }
}

void FrameAssembler::checkWindow() {
    LtcFrame f;
    if (validate(window_, f)) {
        f.sampleStart = cellStart_[0];
        f.sampleEnd = cellEnd_[79];
        if (frameCb_) frameCb_(f);
    } else {
        locked_ = false;
        if (badCb_) badCb_();
    }
}

void FrameAssembler::signalGap() {
    locked_ = false;
    bitsSinceSync_ = 0;
    windowFill_ = 0;
}

void FrameAssembler::finish() {
    signalGap();
}

