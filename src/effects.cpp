#include "effects.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
std::vector<float> selected(const AppState& state, const Sample& sample) {
    const int start = std::clamp(state.editorStart, 0, static_cast<int>(sample.stereo.size()) - 2);
    const int end = std::clamp(state.editorEnd, start + 2, static_cast<int>(sample.stereo.size()));
    return {sample.stereo.begin() + start, sample.stereo.begin() + end};
}
}

void applyEchoToSample(AppState& state, Sample& sample) {
    ensureStereo(sample);
    if (sample.stereo.size() < 2) return;
    auto edited = selected(state, sample);
    const int delay = std::max(2, state.echoTimeMs * 44100 / 1000 * 2);
    for (size_t i = static_cast<size_t>(delay); i < edited.size(); ++i) {
        edited[i] += edited[i - delay] * state.echoPower;
    }
    sample.stereo = std::move(edited);
}

void applyFlangerToSample(AppState& state, Sample& sample) {
    ensureStereo(sample);
    if (sample.stereo.size() < 2) return;
    const auto source = selected(state, sample);
    const size_t frames = source.size() / 2;
    const int baseDelay = std::max(1, state.flangerDelayMs * 44100 / 1000);
    const int depth = std::max(0, static_cast<int>(state.flangerDepthMs * 44100 / 1000));
    std::vector<float> edited(source.size());
    for (size_t frame = 0; frame < frames; ++frame) {
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846 *
            state.flangerRateHz * frame / 44100.0);
        const int delay = std::clamp(baseDelay + static_cast<int>(std::sin(phase) * depth), 0,
            static_cast<int>(frame));
        const size_t delayed = (frame - delay) * 2;
        edited[frame * 2] = source[frame * 2] * 0.65f +
            source[delayed] * (0.35f + state.flangerFeedback);
        edited[frame * 2 + 1] = source[frame * 2 + 1] * 0.65f +
            source[delayed + 1] * (0.35f + state.flangerFeedback);
    }
    sample.stereo = std::move(edited);
}

void applyCompressorToSample(AppState& state, Sample& sample) {
    ensureStereo(sample);
    const float threshold = std::pow(10.0f, state.compressorThreshold / 20.0f);
    for (float& value : sample.stereo) {
        const float sign = value < 0.0f ? -1.0f : 1.0f;
        const float magnitude = std::abs(value);
        const float compressed = magnitude > threshold
            ? threshold + (magnitude - threshold) / std::max(1.0f, state.compressorRatio)
            : magnitude;
        value = sign * std::min(1.0f, compressed * state.compressorMakeup);
    }
}

void applyDistortionToSample(AppState& state, Sample& sample) {
    ensureStereo(sample);
    for (float& value : sample.stereo) {
        const float driven = std::tanh(value * state.distortionDrive);
        value = value * (1.0f - state.distortionMix) + driven * state.distortionMix;
    }
}

void applyFadeInToSample(AppState& state, Sample& sample) {
    ensureStereo(sample);
    const auto range = selected(state, sample);
    const size_t frames = range.size() / 2;
    const size_t fadeFrames = std::min(frames,
        static_cast<size_t>(std::max(1, state.fadeInMs) * 44100 / 1000));
    const int start = std::clamp(state.editorStart, 0,
        static_cast<int>(sample.stereo.size()) - 2);
    for (size_t frame = 0; frame < fadeFrames; ++frame) {
        const float gain = static_cast<float>(frame) /
            static_cast<float>(std::max<size_t>(1, fadeFrames - 1));
        sample.stereo[start + frame * 2] *= gain;
        sample.stereo[start + frame * 2 + 1] *= gain;
    }
}

void applyFadeOutToSample(AppState& state, Sample& sample) {
    ensureStereo(sample);
    const auto range = selected(state, sample);
    const size_t frames = range.size() / 2;
    const size_t fadeFrames = std::min(frames,
        static_cast<size_t>(std::max(1, state.fadeOutMs) * 44100 / 1000));
    const int start = std::clamp(state.editorStart, 0,
        static_cast<int>(sample.stereo.size()) - 2);
    const size_t fadeStart = frames - fadeFrames;
    for (size_t frame = fadeStart; frame < frames; ++frame) {
        const float gain = static_cast<float>(frames - 1 - frame) /
            static_cast<float>(std::max<size_t>(1, fadeFrames - 1));
        sample.stereo[start + frame * 2] *= gain;
        sample.stereo[start + frame * 2 + 1] *= gain;
    }
}

void applyReverseToSample(AppState& state, Sample& sample) {
    ensureStereo(sample);
    if (sample.stereo.size() < 2) return;
    const auto original = selected(state, sample);
    std::vector<float> reversed(original.size());
    for (size_t frame = 0; frame < original.size() / 2; ++frame) {
        reversed[frame * 2] = original[original.size() - 2 - frame * 2];
        reversed[frame * 2 + 1] = original[original.size() - 1 - frame * 2];
    }
    for (size_t i = 0; i < reversed.size(); ++i) {
        reversed[i] = original[i] * (1.0f - state.reverseMix) + reversed[i] * state.reverseMix;
    }
    sample.stereo = std::move(reversed);
}

void applyTimeStretchToSample(AppState& state, Sample& sample) {
    ensureStereo(sample);
    if (sample.stereo.size() < 2) return;
    const auto source = selected(state, sample);
    const size_t sourceFrames = source.size() / 2;
    const float ratio = std::clamp(state.timeStretchRatio, 0.25f, 4.0f);
    const size_t outputFrames = std::max<size_t>(1, static_cast<size_t>(sourceFrames * ratio));
    std::vector<float> stretched(outputFrames * 2);
    for (size_t frame = 0; frame < outputFrames; ++frame) {
        const double position = frame / ratio;
        const size_t index = std::min(static_cast<size_t>(position), sourceFrames - 1);
        const size_t next = std::min(index + 1, sourceFrames - 1);
        const float fraction = static_cast<float>(position - index);
        stretched[frame * 2] = source[index * 2] * (1.0f - fraction) + source[next * 2] * fraction;
        stretched[frame * 2 + 1] = source[index * 2 + 1] * (1.0f - fraction) +
            source[next * 2 + 1] * fraction;
    }
    sample.stereo = std::move(stretched);
}
