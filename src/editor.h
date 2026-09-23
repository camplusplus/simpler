#pragma once

#include <SDL.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

constexpr int kPadsPerPage = 8;
constexpr int kPadCount = 16;
constexpr int kMaxVoices = 16;

struct Pad {
    const char* fallbackName;
    SDL_Color color;
};
extern const std::array<Pad, kPadCount> kPads;

struct Sample {
    std::string name;
    std::vector<float> stereo;
};

void ensureStereo(Sample& sample);
std::vector<float> makeDefaultSample(int pad, int sampleRate = 44100);

struct Voice {
    bool active = false;
    int pad = 0;
    double position = 0.0;
    float pitchScale = 1.0f;
};

class VoiceBank {
public:
    std::array<Sample, kPadCount> samples;
    void trigger(int pad, float pitchScale = 1.0f);
    void mix(float* output, int frames);
private:
    std::array<Voice, kMaxVoices> voices_{};
    std::mutex mutex_;
    size_t nextVoice_ = 0;
};

struct AppState {
    std::atomic<bool> running{true};
    std::atomic<int> selectedPad{0};
    std::atomic<uint32_t> midiTriggers{0};
    int page = 0;
    bool browserOpen = false;
    bool editorOpen = false;
    bool settingsOpen = false;
    std::vector<std::string> audioDevices;
    size_t audioDeviceSelection = 0;
    size_t midiDeviceSelection = 0;
    std::vector<std::string> midiDevices;
    bool settingsMidiFocus = false;
    bool echoPopupOpen = false;
    Sample echoSource;
    bool flangerPopupOpen = false;
    Sample flangerSource;
    bool compressorPopupOpen = false;
    Sample compressorSource;
    bool distortionPopupOpen = false;
    Sample distortionSource;
    bool reversePopupOpen = false;
    Sample reverseSource;
    bool timeStretchPopupOpen = false;
    Sample timeStretchSource;
    bool fadeInPopupOpen = false;
    Sample fadeInSource;
    bool fadeOutPopupOpen = false;
    Sample fadeOutSource;
    int editorStart = 0;
    int editorEnd = 0;
    bool editorAdjustEnd = false;
    float editorGain = 1.0f;
    int echoTimeMs = 250;
    float echoPower = 0.28f;
    int flangerDelayMs = 8;
    float flangerDepthMs = 4.0f;
    float flangerRateHz = 0.35f;
    float flangerFeedback = 0.15f;
    float compressorThreshold = -18.0f;
    float compressorRatio = 4.0f;
    float compressorMakeup = 1.0f;
    float distortionDrive = 2.0f;
    float distortionMix = 0.75f;
    float reverseMix = 1.0f;
    float timeStretchRatio = 1.0f;
    int fadeInMs = 250;
    int fadeOutMs = 250;
    size_t browserSelection = 0;
    std::vector<std::string> browserFiles;
    std::array<float, kPadCount> pitch{};
    std::array<uint32_t, kPadCount> flashUntil{};
    VoiceBank voices;
    struct EditSnapshot {
        int pad = 0;
        Sample sample;
        int start = 0;
        int end = 0;
        float gain = 1.0f;
        int echoTimeMs = 250;
        float echoPower = 0.28f;
    };
    std::vector<EditSnapshot> undoStack;
    AppState() : pitch{} { pitch.fill(1.0f); }
};

void resetEditor(AppState& state);
int trimStep(const SDL_KeyboardEvent& key);
int silenceFrames(const SDL_KeyboardEvent& key);
int nearestZeroFrame(const std::vector<float>& stereo, int frame, int direction);
void saveUndo(AppState& state);
void undoEditor(AppState& state);
void applyEditor(AppState& state, char action, int silenceFrameCount = 4410);
void postMixCallback(void* userdata, Uint8* stream, int length);
