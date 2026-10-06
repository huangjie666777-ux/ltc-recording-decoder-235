#include "audio.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

WavReader::~WavReader() {
    if (wav_) {
        drwav_uninit(static_cast<drwav*>(wav_));
        delete static_cast<drwav*>(wav_);
    }
    delete[] interleaveBuf_;
}

bool WavReader::open(const std::string& path, int channel, std::string& error) {
    drwav* wav = new drwav;
    if (!drwav_init_file(wav, path.c_str(), nullptr)) {
        delete wav;
        error = "cannot open file: " + path;
        return false;
    }

    if (wav->sampleRate != 48000) {
        error = "unsupported sample rate " + std::to_string(wav->sampleRate) +
                " (only 48000 Hz supported)";
        drwav_uninit(wav); delete wav;
        return false;
    }
    if (wav->bitsPerSample != 16 || wav->translatedFormatTag != DR_WAVE_FORMAT_PCM) {
        error = "unsupported format (only 16-bit PCM supported)";
        drwav_uninit(wav); delete wav;
        return false;
    }
    if (wav->channels < 1 || wav->channels > 2) {
        error = "unsupported channel count " + std::to_string(wav->channels) +
                " (only mono or stereo supported)";
        drwav_uninit(wav); delete wav;
        return false;
    }

    int ch = channel;
    if (ch < 0) ch = 0;  // auto: mono -> only channel, stereo -> first channel
    if (ch >= (int)wav->channels) {
        error = "requested channel " + std::to_string(ch) + " not present";
        drwav_uninit(wav); delete wav;
        return false;
    }

    wav_ = wav;
    channels_ = wav->channels;
    channel_ = ch;
    totalFrames_ = wav->totalPCMFrameCount;
    return true;
}

uint64_t WavReader::readChunk(int16_t* out, uint64_t maxFrames) {
    if (!wav_ || maxFrames == 0) return 0;
    drwav* wav = static_cast<drwav*>(wav_);

    if (channels_ == 1) {
        return drwav_read_pcm_frames_s16(wav, maxFrames, out);
    }
    // Stereo: deinterleave, keep only the selected channel.
    if (interleaveCap_ < maxFrames * channels_) {
        delete[] interleaveBuf_;
        interleaveBuf_ = new int16_t[maxFrames * channels_];
        interleaveCap_ = maxFrames * channels_;
    }
    uint64_t got = drwav_read_pcm_frames_s16(wav, maxFrames, interleaveBuf_);
    for (uint64_t i = 0; i < got; ++i)
        out[i] = interleaveBuf_[i * channels_ + channel_];
    return got;
}

