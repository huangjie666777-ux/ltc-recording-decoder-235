#include "segments.h"

#include <cmath>

void SegmentBuilder::onFrame(const LtcFrame& f) {
    if (open_) {
        int64_t idx = FrameAssembler::frameIndex(f);
        int64_t expect = (lastIndex_ + 1) % (int64_t)(24 * 60 * 60 * 25);
        bool tcOk = (idx == expect);
        // Adjacent on the tape: half-open intervals must touch. Positions
        // are fractional; allow sub-sample slack only.
        bool posOk = std::fabs(f.sampleStart - current_.frames.back().sampleEnd) < 1.0;
        if (tcOk && posOk) {
            current_.frames.push_back(f);
            lastIndex_ = idx;
            return;
        }
        close(tcOk ? "position_gap" : "timecode_jump");
    }
    open_ = true;
    current_ = Segment{};
    current_.frames.push_back(f);
    lastIndex_ = FrameAssembler::frameIndex(f);
}

void SegmentBuilder::onBadFrame() {
    if (open_) close("bad_frame");
}

void SegmentBuilder::onGap() {
    if (open_) close("signal_lost");
}

void SegmentBuilder::finish() {
    if (open_) close("end_of_audio");
}

void SegmentBuilder::close(const char* reason) {
    current_.endReason = reason;
    if (segCb_) segCb_(current_);
    open_ = false;
    current_ = Segment{};
}

