#pragma once
// Biphase-mark (FM0) bit recovery for ltc_scan235.
//
// Samples are fed in chunks (absolute sample positions are kept across
// chunks, so the chunk length never affects the result). Zero-crossing
// edges are located between sample pairs, and the bit cell period is
// tracked from the measured edge-to-edge intervals (never by counting a
// fixed number of samples), which tolerates a constant playback speed
// deviation of about +/-2% and either signal polarity.

#include <cstdint>
#include <functional>

class BiphaseDecoder {
public:
    // Called for every recovered bit.
    //   bit       - 0 or 1
    //   cellStart - absolute sample position (fractional) where the bit
    //               cell began
    //   cellEnd   - absolute sample position where the bit cell ended
    //               (half-open interval [cellStart, cellEnd))
    using BitCallback = std::function<void(int bit, double cellStart, double cellEnd)>;

    // Called when the edge pattern can no longer be followed (silence,
    // dropouts, end of signal). No bits are emitted for the gap.
    using GapCallback = std::function<void()>;

    // sampleRate: must be 48000. fps: only 25 supported (80 bits/frame,
    // 2000 bit/s, nominal 24.0 samples per bit cell).
    explicit BiphaseDecoder(uint32_t sampleRate = 48000, uint32_t fps = 25);

    void setBitCallback(BitCallback cb) { bitCb_ = std::move(cb); }
    void setGapCallback(GapCallback cb) { gapCb_ = std::move(cb); }

    // Feed one chunk of mono samples. baseIndex is the absolute sample
    // index of samples[0] in the file.
    void feed(const int16_t* samples, uint64_t count, uint64_t baseIndex);

    // Flush at end of stream: closes any pending state.
    void finish();

private:
    void handleEdge(double t);

    BitCallback bitCb_;
    GapCallback gapCb_;

    double nominalHalf_;   // nominal half bit cell in samples (12.0)
    double halfMin_, halfMax_;

    // Edge / level tracking state (persists across chunks).
    int prevSign_ = 0;         // sign of previous nonzero sample (-1/0/+1)
    double prevT_ = 0.0;       // absolute position of previous sample
    bool havePrev_ = false;

    // Bit-cell tracking state.
    bool haveEdge_ = false;    // have a previous edge
    double lastEdge_ = 0.0;    // time of previous edge
    double half_ = 12.0;       // current half-cell estimate in samples
    bool pendingHalf_ = false; // saw the first half of a '1' bit
    bool gapSignalled_ = true; // avoid repeated gap callbacks
};
