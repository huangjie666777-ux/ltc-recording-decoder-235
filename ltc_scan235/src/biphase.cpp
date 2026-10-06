#include "biphase.h"

#include <algorithm>
#include <cmath>

BiphaseDecoder::BiphaseDecoder(uint32_t sampleRate, uint32_t fps) {
    // 80 bits per LTC frame.
    double bitCell = (double)sampleRate / ((double)fps * 80.0);
    nominalHalf_ = bitCell / 2.0;
    half_ = nominalHalf_;
    // Accept constant speed deviations a little beyond +/-2%.
    halfMin_ = nominalHalf_ / 1.04;
    halfMax_ = nominalHalf_ * 1.04;
}

void BiphaseDecoder::feed(const int16_t* samples, uint64_t count, uint64_t baseIndex) {
    for (uint64_t i = 0; i < count; ++i) {
        double t = (double)(baseIndex + i);
        int s = samples[i] > 0 ? 1 : (samples[i] < 0 ? -1 : 0);
        if (!havePrev_) {
            prevSign_ = s;
            prevT_ = t;
            havePrev_ = true;
            continue;
        }
        if (s != 0 && s != prevSign_) {
            // Level change between the previous sample and this one
            // (including the silence-to-signal transition at the start
            // of a recording). Use the midpoint of the sample pair.
            handleEdge(t - 0.5);
        }
        prevSign_ = s;
        prevT_ = t;
    }
}

void BiphaseDecoder::handleEdge(double t) {
    if (!haveEdge_) {
        lastEdge_ = t;
        haveEdge_ = true;
        gapSignalled_ = false;
        return;
    }
    double delta = t - lastEdge_;
    // Edge intervals are k half-cells: k = 1 (one half of a '1' bit) or
    // k = 2 (a whole '0' bit). Larger k means the signal was lost.
    int k = (int)llround(delta / half_);
    if (k < 1) k = 1;
    if (k > 2) {
        // Gap: silence or severe disruption. Resynchronise from this edge.
        if (!gapSignalled_) {
            if (gapCb_) gapCb_();
            gapSignalled_ = true;
        }
        pendingHalf_ = false;
        lastEdge_ = t;
        return;
    }
    // Refine the half-cell estimate from the measured interval.
    double measured = delta / (double)k;
    double clamped = std::min(std::max(measured, halfMin_), halfMax_);
    half_ = 0.875 * half_ + 0.125 * clamped;

    double cellStart = t - 2.0 * half_;
    if (k == 1) {
        if (pendingHalf_) {
            // Second half of a '1' bit: this edge is the cell boundary.
            if (bitCb_) bitCb_(1, cellStart, t);
            pendingHalf_ = false;
        } else {
            // First half of a '1' bit: wait for the boundary edge.
            pendingHalf_ = true;
        }
    } else {
        if (pendingHalf_) {
            // A full-cell interval cannot follow a half-cell interval in
            // valid biphase-mark: our half-cell pairing was off (e.g. the
            // recording started in the middle of a cell). Realign here.
            pendingHalf_ = false;
            if (!gapSignalled_) {
                if (gapCb_) gapCb_();
                gapSignalled_ = true;
            }
            lastEdge_ = t;
            return;
        }
        if (bitCb_) bitCb_(0, cellStart, t);
    }
    lastEdge_ = t;
    gapSignalled_ = false;
}

void BiphaseDecoder::finish() {
    if (haveEdge_ && !gapSignalled_) {
        if (gapCb_) gapCb_();
        gapSignalled_ = true;
    }
    havePrev_ = false;
    haveEdge_ = false;
    pendingHalf_ = false;
}
