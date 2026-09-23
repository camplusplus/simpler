#include "editor.h"
#include "effects.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void ensureStereo(Sample& sample) {
    if (sample.stereo.size() % 2 != 0) {
        sample.stereo.pop_back();
    }
}

const std::array<Pad, kPadCount> kPads{{
    {"PAD 1", {236, 82, 92, 255}}, {"PAD 2", {245, 157, 76, 255}},
    {"PAD 3", {246, 218, 92, 255}}, {"PAD 4", {92, 211, 143, 255}},
    {"PAD 5", {77, 172, 232, 255}}, {"PAD 6", {139, 116, 255, 255}},
    {"PAD 7", {215, 106, 207, 255}}, {"PAD 8", {99, 225, 218, 255}},
    {"PAD 9", {236, 82, 92, 255}}, {"PAD 10", {245, 157, 76, 255}},
    {"PAD 11", {246, 218, 92, 255}}, {"PAD 12", {92, 211, 143, 255}},
    {"PAD 13", {77, 172, 232, 255}}, {"PAD 14", {139, 116, 255, 255}},
    {"PAD 15", {215, 106, 207, 255}}, {"PAD 16", {99, 225, 218, 255}}
}};

std::vector<float> makeDefaultSample(int pad, int sampleRate) {
    const int sound = pad % kPadsPerPage;
    const float duration = sound == 0 ? 0.72f : (sound == 2 ? 0.22f : 0.5f);
    const size_t frames = static_cast<size_t>(duration * sampleRate);
    std::vector<float> sample(frames * 2);
    uint32_t noise = 0x12345678u + static_cast<uint32_t>(pad);
    for (size_t frame = 0; frame < frames; ++frame) {
        const float time = static_cast<float>(frame) / sampleRate;
        const float progress = time / duration;
        const float envelope = std::max(0.0f, 1.0f - progress);
        const float frequency = sound == 0 ? 62.0f * std::max(0.25f, 1.0f - progress * 0.75f)
            : std::array<float, kPadsPerPage>{{62.0f, 190.0f, 4800.0f, 330.0f, 92.5f, 220.0f, 330.0f, 710.0f}}[sound];
        noise = noise * 1664525u + 1013904223u;
        const float noiseValue = (static_cast<float>((noise >> 8) & 0xffffu) / 32767.5f) - 1.0f;
        float value = std::sin(static_cast<float>(2.0 * kPi) * frequency * time);
        if (sound == 1 || sound == 2 || sound == 3)
            value = noiseValue * (sound == 2 ? 0.72f : 0.42f) + value * 0.28f;
        else if (sound == 7) value = noiseValue * 0.3f + value * 0.7f;
        value *= envelope * std::min(1.0f, time * 120.0f) * 0.35f;
        sample[frame * 2] = sample[frame * 2 + 1] = value;
    }
    return sample;
}

void VoiceBank::trigger(int pad, float pitchScale) {
    std::lock_guard<std::mutex> lock(mutex_);
    pad = std::clamp(pad, 0, kPadCount - 1);
    ensureStereo(samples[pad]);
    if (samples[pad].stereo.empty()) return;
    Voice& voice = voices_[nextVoice_++ % voices_.size()];
    voice = Voice{};
    voice.active = true; voice.pad = pad; voice.pitchScale = pitchScale;
}

void VoiceBank::mix(float* output, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int frame = 0; frame < frames; ++frame) {
        float left = 0.0f, right = 0.0f;
        for (Voice& voice : voices_) {
            if (!voice.active) continue;
            const auto& sample = samples[voice.pad].stereo;
            const size_t index = static_cast<size_t>(voice.position);
            if (index + 3 >= sample.size()) { voice.active = false; continue; }
            const float fraction = static_cast<float>(voice.position - index);
            left += sample[index] * (1.0f - fraction) + sample[index + 2] * fraction;
            right += sample[index + 1] * (1.0f - fraction) + sample[index + 3] * fraction;
            voice.position += voice.pitchScale * 2.0;
        }
        output[frame * 2] += left * 0.7f;
        output[frame * 2 + 1] += right * 0.7f;
    }
}

void resetEditor(AppState& state) {
    const auto& sample = state.voices.samples[state.selectedPad.load()].stereo;
    state.editorStart = 0; state.editorEnd = static_cast<int>(sample.size());
    state.editorGain = 1.0f; state.editorAdjustEnd = false;
    state.echoTimeMs = 250; state.echoPower = 0.28f;
    state.flangerDelayMs = 8; state.flangerDepthMs = 4.0f;
    state.flangerRateHz = 0.35f; state.flangerFeedback = 0.15f;
}

int trimStep(const SDL_KeyboardEvent& key) {
    const int milliseconds = (key.keysym.mod & KMOD_SHIFT) ? 100 :
        ((key.keysym.mod & KMOD_CTRL) ? 1 : 10);
    return std::max(2, milliseconds * 44100 / 1000 * 2);
}
int silenceFrames(const SDL_KeyboardEvent& key) {
    const int milliseconds = (key.keysym.mod & KMOD_SHIFT) ? 500 :
        ((key.keysym.mod & KMOD_CTRL) ? 10 : 100);
    return std::max(1, milliseconds * 44100 / 1000);
}
int nearestZeroFrame(const std::vector<float>& stereo, int frame, int direction) {
    const int frameCount = static_cast<int>(stereo.size() / 2);
    frame = std::clamp(frame, 0, frameCount - 1);
    int best = frame;
    float bestMagnitude = std::abs(stereo[frame * 2]) + std::abs(stereo[frame * 2 + 1]);
    for (int distance = 1; distance <= 2205; ++distance) {
        const int candidate = frame + distance * direction;
        if (candidate < 0 || candidate >= frameCount) break;
        const float magnitude = std::abs(stereo[candidate * 2]) + std::abs(stereo[candidate * 2 + 1]);
        if (magnitude < bestMagnitude) { best = candidate; bestMagnitude = magnitude; }
        if (magnitude < 0.002f) break;
    }
    return best;
}
void saveUndo(AppState& state) {
    const int pad = state.selectedPad.load();
    state.undoStack.push_back({pad, state.voices.samples[pad], state.editorStart, state.editorEnd,
        state.editorGain, state.echoTimeMs, state.echoPower});
    if (state.undoStack.size() > 8) state.undoStack.erase(state.undoStack.begin());
}
void undoEditor(AppState& state) {
    if (state.undoStack.empty()) return;
    auto snapshot = std::move(state.undoStack.back()); state.undoStack.pop_back();
    state.voices.samples[snapshot.pad] = std::move(snapshot.sample);
    state.selectedPad.store(snapshot.pad); state.editorStart = snapshot.start;
    state.editorEnd = snapshot.end; state.editorGain = snapshot.gain;
    state.echoTimeMs = snapshot.echoTimeMs; state.echoPower = snapshot.echoPower;
}

void applyEditor(AppState& state, char action, int silenceFrameCount) {
    Sample& sample = state.voices.samples[state.selectedPad.load()];
    if (sample.stereo.empty()) return;
    saveUndo(state);
    const int start = std::clamp(state.editorStart, 0, static_cast<int>(sample.stereo.size() - 2));
    const int end = std::clamp(state.editorEnd, start + 2, static_cast<int>(sample.stereo.size()));
    std::vector<float> edited(sample.stereo.begin() + start, sample.stereo.begin() + end);
    if (action == 'z') {
        state.editorStart = nearestZeroFrame(sample.stereo, start / 2, 1) * 2;
        state.editorEnd = nearestZeroFrame(sample.stereo, end / 2, -1) * 2;
        state.editorEnd = std::max(state.editorStart + 2,
            std::min(state.editorEnd, static_cast<int>(sample.stereo.size())));
        return;
    } else if (action == 'a' || action == 'd') {
        std::vector<float> silence(static_cast<size_t>(silenceFrameCount) * 2, 0.0f);
        if (action == 'a') silence.insert(silence.end(), sample.stereo.begin(), sample.stereo.end());
        else silence.insert(silence.begin(), sample.stereo.begin(), sample.stereo.end());
        sample.stereo = std::move(silence); resetEditor(state); return;
    } else if (action == 't') sample.stereo = std::move(edited);
    else if (action == 'r') { std::reverse(edited.begin(), edited.end()); sample.stereo = std::move(edited); }
    else if (action == 'n') {
        float peak = 0.0f; for (float value : edited) peak = std::max(peak, std::abs(value));
        if (peak > 0.0001f) for (float& value : edited) value *= 0.9f / peak;
        sample.stereo = std::move(edited);
    } else if (action == 'f') {
        const size_t frames = edited.size() / 2;
        for (size_t frame = 0; frame < frames; ++frame) {
            const float in = std::min(1.0f, static_cast<float>(frame) / 4410.0f);
            const float out = std::min(1.0f, static_cast<float>(frames - frame - 1) / 4410.0f);
            edited[frame * 2] *= in * out; edited[frame * 2 + 1] *= in * out;
        }
        sample.stereo = std::move(edited);
    } else if (action == 'e') applyEchoToSample(state, sample);
    else if (action == 'g') { for (float& value : edited) value *= state.editorGain; sample.stereo = std::move(edited); }
    else if (action == 'p') {
        const double factor = std::clamp(static_cast<double>(state.editorGain), 0.5, 2.0);
        const size_t oldFrames = edited.size() / 2;
        const size_t newFrames = std::max<size_t>(1, static_cast<size_t>(oldFrames / factor));
        std::vector<float> pitched(newFrames * 2);
        for (size_t frame = 0; frame < newFrames; ++frame) {
            const size_t index = std::min(static_cast<size_t>(frame * factor), oldFrames - 1);
            pitched[frame * 2] = edited[index * 2]; pitched[frame * 2 + 1] = edited[index * 2 + 1];
        }
        sample.stereo = std::move(pitched);
    }
    resetEditor(state);
}

void postMixCallback(void* userdata, Uint8* stream, int length) {
    auto* state = static_cast<AppState*>(userdata);
    auto* output = reinterpret_cast<float*>(stream);
    const int frames = length / static_cast<int>(sizeof(float) * 2);
    state->voices.mix(output, frames);
    for (int i = 0; i < frames * 2; ++i) output[i] = std::clamp(output[i], -0.8f, 0.8f);
}
