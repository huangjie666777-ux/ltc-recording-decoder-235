#pragma once
// Continuous-segment assembly for ltc_scan235.
//
// Validated frames are grouped into segments. A segment continues only
// while frames are adjacent on the tape (next sample start == previous
// sample end) AND the timecode increments by exactly one frame (midnight
// wrap 23:59:59:24 -> 00:00:00:00 allowed). Silence, bad frames and
// timecode jumps close the current segment; no frames are interpolated.

#include "ltc_frame.h"

#include <cstdint>
#include <string>
#include <vector>

struct Segment {
    std::vector<LtcFrame> frames;
    std::string endReason;  // filled when the segment is closed
};

class SegmentBuilder {
public:
    // Called when a segment is closed (moved out; frames are not copied).
    using SegmentCallback = std::function<void(Segment&)>;

    void setSegmentCallback(SegmentCallback cb) { segCb_ = std::move(cb); }

    void onFrame(const LtcFrame& f);
    void onBadFrame();   // invalid frame on the tape
    void onGap();        // silence / loss of biphase lock
    void finish();       // end of stream

private:
    void close(const char* reason);

    SegmentCallback segCb_;
    Segment current_;
    bool open_ = false;
    int64_t lastIndex_ = 0;  // frameIndex of last frame in current segment
};

