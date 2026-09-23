#include <SDL.h>
#include <SDL_mixer.h>
#include "editor.h"
#include "effects.h"
#include "graphics_effects.h"
#include "midi.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace {
constexpr int kScreenWidth = 800;
constexpr int kScreenHeight = 480;
constexpr int kRenderWidth = 400;
constexpr int kRenderHeight = 240;

bool loadSample(const std::string& path, Sample& destination) {
    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str());
    if (!chunk) {
        std::fprintf(stderr, "Could not load '%s': %s\n", path.c_str(), Mix_GetError());
        return false;
    }
    int frequency = 0;
    Uint16 format = 0;
    int channels = 0;
    if (Mix_QuerySpec(&frequency, &format, &channels) == 0 || channels == 0) {
        Mix_FreeChunk(chunk);
        std::fprintf(stderr, "Could not query mixer format for '%s'\n", path.c_str());
        return false;
    }
    SDL_AudioStream* stream = SDL_NewAudioStream(format,
        static_cast<Uint8>(channels), frequency, AUDIO_F32SYS, 2, frequency);
    if (!stream || SDL_AudioStreamPut(stream, chunk->abuf, static_cast<int>(chunk->alen)) < 0 ||
        SDL_AudioStreamFlush(stream) < 0) {
        std::fprintf(stderr, "Could not convert '%s': %s\n", path.c_str(), SDL_GetError());
        if (stream) SDL_FreeAudioStream(stream);
        Mix_FreeChunk(chunk);
        return false;
    }
    const int available = SDL_AudioStreamAvailable(stream);
    destination.stereo.resize(static_cast<size_t>(available) / sizeof(float));
    if (SDL_AudioStreamGet(stream, destination.stereo.data(), available) < 0) {
        destination.stereo.clear();
    }
    SDL_FreeAudioStream(stream);
    Mix_FreeChunk(chunk);
    if (destination.stereo.empty()) {
        return false;
    }
    ensureStereo(destination);
    if (destination.stereo.empty()) {
        return false;
    }
    destination.name = std::filesystem::path(path).stem().string();
    return true;
}

std::filesystem::path userSampleDirectory() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return {};
    }
    return std::filesystem::path(home) / "simpler";
}

bool ensureUserSampleDirectory(std::filesystem::path& directory) {
    directory = userSampleDirectory();
    if (directory.empty()) {
        std::fprintf(stderr, "Sample directory unavailable: HOME is not set\n");
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::fprintf(stderr, "Could not create sample directory '%s': %s\n",
            directory.c_str(), error.message().c_str());
        return false;
    }
    return true;
}

void loadSamples(AppState& state, int argc, char** argv) {
    for (int i = 0; i < kPadCount; ++i) {
        state.voices.samples[i].name = kPads[i].fallbackName;
        state.voices.samples[i].stereo = makeDefaultSample(i);
    }
    for (int i = 0; i < kPadCount; ++i) {
        std::string path;
        if (argc > i + 1) {
            path = argv[i + 1];
        } else {
            for (const char* extension : {".wav", ".ogg", ".mp3", ".flac"}) {
                const std::string filename = std::to_string(i + 1) + extension;
                const std::filesystem::path userDirectory = userSampleDirectory();
                const std::filesystem::path userCandidate = userDirectory / filename;
                const std::filesystem::path projectCandidate =
                    std::filesystem::path("samples") / filename;
                if (std::filesystem::exists(userCandidate)) {
                    path = userCandidate.string();
                    break;
                }
                if (std::filesystem::exists(projectCandidate)) {
                    path = projectCandidate.string();
                    break;
                }
            }
        }
        if (!path.empty()) {
            loadSample(path, state.voices.samples[i]);
        }
    }
}

void refreshBrowser(AppState& state) {
    state.browserFiles.clear();
    std::filesystem::path sampleDirectory;
    if (ensureUserSampleDirectory(sampleDirectory)) {
        std::error_code error;
        std::filesystem::directory_iterator entries(sampleDirectory, error);
        for (const auto& entry : entries) {
            if (entry.is_regular_file()) {
                const auto extension = entry.path().extension().string();
                if (extension == ".wav" || extension == ".ogg" || extension == ".mp3" || extension == ".flac") {
                    state.browserFiles.push_back(entry.path().string());
                }
            }
        }
        if (error) {
            std::fprintf(stderr, "Could not read sample directory '%s': %s\n",
                sampleDirectory.c_str(), error.message().c_str());
        }
    }
    std::sort(state.browserFiles.begin(), state.browserFiles.end());
    state.browserSelection = std::min(state.browserSelection,
        state.browserFiles.empty() ? size_t{0} : state.browserFiles.size() - 1);
}

void setColor(SDL_Renderer* renderer, SDL_Color color, Uint8 alpha = 255) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, alpha);
}

void fillRect(SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color color, Uint8 alpha = 255) {
    setColor(renderer, color, alpha);
    SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(renderer, &rect);
}

void drawText(SDL_Renderer* renderer, const std::string& text, int x, int y, SDL_Color color, int scale = 1) {
    // Tiny 3x5 bitmap glyphs keep the UI asset-free and readable at 400x240.
    static constexpr const char* glyphs = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+/:";
    static constexpr std::array<std::array<uint8_t, 5>, 40> bitmaps{{
        {{7, 5, 7, 5, 5}}, {{6, 5, 6, 5, 6}}, {{7, 4, 4, 4, 7}}, {{6, 5, 5, 5, 6}},
        {{7, 4, 6, 4, 7}}, {{7, 4, 6, 4, 4}}, {{7, 4, 5, 5, 7}}, {{5, 5, 7, 5, 5}},
        {{7, 2, 2, 2, 7}}, {{1, 1, 1, 5, 2}}, {{5, 5, 6, 5, 5}}, {{4, 4, 4, 4, 7}},
        {{5, 7, 7, 5, 5}}, {{5, 7, 7, 7, 5}}, {{2, 5, 5, 5, 2}}, {{6, 5, 6, 4, 4}},
        {{2, 5, 5, 3, 1}}, {{6, 5, 6, 5, 5}}, {{7, 4, 7, 1, 7}}, {{7, 2, 2, 2, 2}},
        {{5, 5, 5, 5, 7}}, {{5, 5, 5, 5, 2}}, {{5, 5, 7, 7, 5}}, {{5, 5, 2, 5, 5}},
        {{5, 5, 2, 2, 2}}, {{7, 1, 2, 4, 7}}, {{7, 5, 5, 5, 7}}, {{2, 6, 2, 2, 7}},
        {{6, 1, 2, 4, 7}}, {{6, 1, 2, 1, 6}}, {{5, 5, 7, 1, 1}}, {{7, 4, 6, 1, 6}},
        {{2, 4, 6, 5, 2}}, {{7, 1, 2, 2, 2}}, {{2, 5, 2, 5, 2}}, {{0, 2, 0, 2, 0}},
        {{0, 0, 7, 0, 0}}, {{1, 2, 4, 2, 1}}, {{0, 0, 0, 0, 0}}
    }};
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(text[i])));
        const char* found = std::strchr(glyphs, c);
        if (!found) {
            continue;
        }
        const size_t index = static_cast<size_t>(found - glyphs);
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 3; ++col) {
                if (bitmaps[index][row] & (1 << (2 - col))) {
                    fillRect(renderer, x + static_cast<int>(i) * 4 * scale + col * scale,
                             y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void triggerPad(AppState& state, int pad) {
    state.selectedPad.store(pad);
    state.flashUntil[pad] = SDL_GetTicks() + 120;
    state.voices.trigger(pad, state.pitch[pad]);
}

void drawFrame(SDL_Renderer* renderer, AppState& state, uint32_t now) {
    setColor(renderer, {12, 14, 24, 255});
    SDL_RenderClear(renderer);
    fillRect(renderer, 8, 8, 384, 224, {20, 24, 40, 255});
    fillRect(renderer, 12, 12, 376, 18, {33, 39, 61, 255});
    drawText(renderer, "SIMPLER SAMPLER", 20, 18, {158, 220, 255, 255});
    drawText(renderer, state.page == 0 ? "PAGE 1/2" : "PAGE 2/2", 326, 18, {92, 211, 143, 255});

    for (int i = 0; i < kPadsPerPage; ++i) {
        const int pad = state.page * kPadsPerPage + i;
        const int col = i % 4;
        const int row = i / 4;
        const int x = 18 + col * 92;
        const int y = 42 + row * 76;
        const bool selected = state.selectedPad.load() == pad;
        const bool flashing = now < state.flashUntil[pad];
        const SDL_Color color = kPads[pad].color;
        fillRect(renderer, x + 2, y + 3, 84, 66, {5, 8, 16, 255});
        fillRect(renderer, x, y, 84, 66, color, flashing ? 255 : (selected ? 210 : 150));
        fillRect(renderer, x + 4, y + 4, 76, 58, {19, 23, 37, 255}, 190);
        if (selected) {
            fillRect(renderer, x + 4, y + 4, 76, 2, color);
            fillRect(renderer, x + 4, y + 60, 76, 2, color);
        }
        drawText(renderer, std::to_string(i + 1), x + 9, y + 9, color, 2);
        const std::string& label = state.voices.samples[pad].name.empty()
            ? std::string(kPads[pad].fallbackName) : state.voices.samples[pad].name;
        drawText(renderer, label.substr(0, 16), x + 9, y + 39, {220, 226, 240, 255});
    }

    fillRect(renderer, 18, 200, 364, 20, {10, 13, 24, 255});
    drawText(renderer, "1-8 PLAY   TAB PAGE   B BROWSER E EDIT M MIDI", 25, 207, {127, 146, 177, 255});
    if (state.settingsOpen) {
        fillRect(renderer, 24, 28, 352, 184, {7, 10, 19, 250});
        fillRect(renderer, 28, 32, 344, 16, {45, 55, 82, 255});
        drawText(renderer, "AUDIO / MIDI SETTINGS", 38, 37, {158, 220, 255, 255});
        drawText(renderer, "AUDIO OUTPUT", 38, 60, {92, 211, 143, 255});
        for (size_t i = 0; i < std::min<size_t>(state.audioDevices.size(), 4); ++i) {
            const int y = 76 + static_cast<int>(i) * 16;
            drawText(renderer, (i == state.audioDeviceSelection ? "> " : "  ")
                + state.audioDevices[i].substr(0, 34), 38, y,
                i == state.audioDeviceSelection ? SDL_Color{245, 157, 76, 255}
                    : SDL_Color{180, 190, 210, 255});
        }
        drawText(renderer, "MIDI INPUT", 205, 60, {92, 211, 143, 255});
        for (size_t i = 0; i < std::min<size_t>(state.midiDevices.size(), 4); ++i) {
            const int y = 76 + static_cast<int>(i) * 16;
            drawText(renderer, (i == state.midiDeviceSelection ? "> " : "  ")
                + state.midiDevices[i].substr(0, 20), 205, y,
                i == state.midiDeviceSelection ? SDL_Color{245, 157, 76, 255}
                    : SDL_Color{180, 190, 210, 255});
        }
        drawText(renderer, "TAB AUDIO/MIDI  UP/DOWN SELECT", 38, 178, {180, 190, 210, 255});
        drawText(renderer, "ENTER APPLY  ESC CLOSE", 38, 194, {245, 157, 76, 255});
        return;
    }
    if (state.browserOpen) {
        fillRect(renderer, 24, 28, 352, 184, {7, 10, 19, 250});
        fillRect(renderer, 28, 32, 344, 16, {45, 55, 82, 255});
        drawText(renderer, "LOAD FILE TO SELECTED PAD", 36, 37, {158, 220, 255, 255});
        if (state.browserFiles.empty()) {
            drawText(renderer, "NO FILES IN SAMPLES/", 42, 72, {245, 157, 76, 255});
        } else {
            const size_t first = state.browserSelection > 5 ? state.browserSelection - 5 : 0;
            for (size_t i = first; i < std::min(first + 6, state.browserFiles.size()); ++i) {
                const int y = 58 + static_cast<int>(i - first) * 22;
                const SDL_Color color = i == state.browserSelection
                    ? SDL_Color{92, 211, 143, 255} : SDL_Color{180, 190, 210, 255};
                if (i == state.browserSelection) {
                    fillRect(renderer, 34, y - 3, 328, 16, {26, 58, 53, 255});
                }
                drawText(renderer, std::filesystem::path(state.browserFiles[i]).filename().string().substr(0, 38),
                    40, y, color);
            }
        }
        drawText(renderer, "UP/DOWN SELECT  ENTER LOAD  ESC CLOSE", 36, 198, {127, 146, 177, 255});
    }
    if (state.editorOpen) {
        const auto& sample = state.voices.samples[state.selectedPad.load()].stereo;
        fillRect(renderer, 20, 24, 360, 194, {7, 10, 19, 250});
        fillRect(renderer, 24, 28, 352, 16, {45, 55, 82, 255});
        drawText(renderer, "SAMPLE EDITOR", 34, 33, {158, 220, 255, 255});
        fillRect(renderer, 30, 56, 340, 76, {14, 19, 32, 255});
        if (sample.size() >= 4) {
            const int width = 336;
            for (int x = 0; x < width; ++x) {
                const size_t frame = static_cast<size_t>(x) * (sample.size() / 2) / width;
                const float value = (sample[frame * 2] + sample[frame * 2 + 1]) * 0.5f;
                const int height = static_cast<int>(value * 30.0f);
                fillRect(renderer, 32 + x, 94 - std::abs(height), 1, std::max(1, std::abs(height) * 2),
                    x * 2 < state.editorStart * width / std::max(1, static_cast<int>(sample.size()) / 2) ||
                    x * 2 > state.editorEnd * width / std::max(1, static_cast<int>(sample.size()) / 2)
                        ? SDL_Color{80, 85, 105, 255} : kPads[state.selectedPad.load()].color);
            }
        }
        drawText(renderer, state.editorAdjustEnd ? "END SELECTED  TAB START" : "START SELECTED  TAB END",
            34, 144, {92, 211, 143, 255});
        drawText(renderer, "ARROWS MOVE  CTRL 1MS  SHIFT 100MS", 34, 158, {180, 190, 210, 255});
        drawText(renderer, "Z SNAP ZERO  [ START SILENCE  ] END SILENCE", 34, 172, {180, 190, 210, 255});
        drawText(renderer, "T TRIM  N NORMAL  I FADE IN  V FADE OUT", 34, 186, {180, 190, 210, 255});
        drawText(renderer, "E ECHO  L FLANGER  C COMPRESS", 34, 200, {180, 190, 210, 255});
        drawText(renderer, "O DISTORT  U UNDO", 34, 214, {180, 190, 210, 255});
        if (!state.echoPopupOpen) {
            drawText(renderer, "TIME " + std::to_string(state.echoTimeMs) + "MS POWER " +
                std::to_string(state.echoPower).substr(0, 4), 205, 200,
                {92, 211, 143, 255});
        }
    }
    if (state.echoPopupOpen) {
        fillRect(renderer, 52, 52, 296, 132, {8, 12, 22, 255});
        fillRect(renderer, 56, 56, 288, 16, {45, 55, 82, 255});
        drawText(renderer, "ECHO PARAMETERS", 66, 61, {158, 220, 255, 255});
        drawText(renderer, "TIME " + std::to_string(state.echoTimeMs) + "MS", 70, 88,
            {92, 211, 143, 255});
        drawText(renderer, "POWER " + std::to_string(state.echoPower).substr(0, 4), 70, 104,
            {92, 211, 143, 255});
        drawText(renderer, "SEMICOLON / QUOTE: TIME", 70, 126, {180, 190, 210, 255});
        drawText(renderer, "COMMA / PERIOD: POWER", 70, 140, {180, 190, 210, 255});
        drawText(renderer, "CTRL FINE  SHIFT COARSE", 70, 154, {180, 190, 210, 255});
        drawText(renderer, "ENTER OR SPACE: APPLY + PLAY", 70, 168, {245, 157, 76, 255});
        drawText(renderer, "ESCAPE: CANCEL", 70, 182, {180, 190, 210, 255});
    }
    if (state.flangerPopupOpen) {
        fillRect(renderer, 42, 42, 316, 166, {8, 12, 22, 255});
        fillRect(renderer, 46, 46, 308, 16, {45, 55, 82, 255});
        drawText(renderer, "FLANGER PARAMETERS", 56, 51, {158, 220, 255, 255});
        drawText(renderer, "DELAY " + std::to_string(state.flangerDelayMs) + "MS", 58, 78, {92, 211, 143, 255});
        drawText(renderer, "DEPTH " + std::to_string(state.flangerDepthMs).substr(0, 4) + "MS", 190, 78, {92, 211, 143, 255});
        drawText(renderer, "RATE " + std::to_string(state.flangerRateHz).substr(0, 4) + "HZ", 58, 96, {92, 211, 143, 255});
        drawText(renderer, "FEEDBACK " + std::to_string(state.flangerFeedback).substr(0, 4), 190, 96, {92, 211, 143, 255});
        drawText(renderer, "A/Z DELAY  S/X DEPTH", 58, 122, {180, 190, 210, 255});
        drawText(renderer, "D/C RATE  F/V FEEDBACK", 58, 138, {180, 190, 210, 255});
        drawText(renderer, "CTRL FINE  SHIFT COARSE", 58, 154, {180, 190, 210, 255});
        drawText(renderer, "SPACE PREVIEW  ENTER APPLY", 58, 172, {245, 157, 76, 255});
        drawText(renderer, "ESCAPE CANCEL", 58, 188, {180, 190, 210, 255});
    }
    if (state.compressorPopupOpen) {
        fillRect(renderer, 40, 42, 320, 150, {8, 12, 22, 255});
        drawText(renderer, "COMPRESSOR PARAMETERS", 52, 52, {158, 220, 255, 255});
        drawText(renderer, "THRESHOLD " + std::to_string(static_cast<int>(state.compressorThreshold)) + "DB", 54, 82, {92, 211, 143, 255});
        drawText(renderer, "RATIO " + std::to_string(state.compressorRatio).substr(0, 4), 54, 98, {92, 211, 143, 255});
        drawText(renderer, "MAKEUP " + std::to_string(state.compressorMakeup).substr(0, 4), 54, 114, {92, 211, 143, 255});
        drawText(renderer, "Q/A THRESHOLD  W/S RATIO", 54, 136, {180, 190, 210, 255});
        drawText(renderer, "E/D MAKEUP  SPACE PREVIEW", 54, 152, {180, 190, 210, 255});
        drawText(renderer, "ENTER APPLY  ESCAPE CANCEL", 54, 174, {245, 157, 76, 255});
    }
    if (state.distortionPopupOpen) {
        fillRect(renderer, 40, 42, 320, 134, {8, 12, 22, 255});
        drawText(renderer, "DISTORTION PARAMETERS", 52, 52, {158, 220, 255, 255});
        drawText(renderer, "DRIVE " + std::to_string(state.distortionDrive).substr(0, 4), 54, 82, {92, 211, 143, 255});
        drawText(renderer, "MIX " + std::to_string(state.distortionMix).substr(0, 4), 54, 98, {92, 211, 143, 255});
        drawText(renderer, "A/Z DRIVE  S/X MIX", 54, 122, {180, 190, 210, 255});
        drawText(renderer, "SPACE PREVIEW  ENTER APPLY", 54, 138, {180, 190, 210, 255});
        drawText(renderer, "ESCAPE CANCEL", 54, 154, {245, 157, 76, 255});
    }
    if (state.reversePopupOpen) {
        fillRect(renderer, 48, 52, 304, 116, {8, 12, 22, 255});
        drawText(renderer, "REVERSE PARAMETERS", 60, 62, {158, 220, 255, 255});
        drawText(renderer, "BLEND " + std::to_string(state.reverseMix).substr(0, 4), 62, 88,
            {92, 211, 143, 255});
        drawText(renderer, "A/Z BLEND DOWN/UP", 62, 110, {180, 190, 210, 255});
        drawText(renderer, "SPACE PREVIEW  ENTER APPLY", 62, 126, {180, 190, 210, 255});
        drawText(renderer, "ESCAPE CANCEL", 62, 144, {245, 157, 76, 255});
    }
    if (state.timeStretchPopupOpen) {
        fillRect(renderer, 42, 48, 316, 132, {8, 12, 22, 255});
        drawText(renderer, "TIME STRETCH PARAMETERS", 54, 58, {158, 220, 255, 255});
        drawText(renderer, "RATIO " + std::to_string(state.timeStretchRatio).substr(0, 4), 58, 86,
            {92, 211, 143, 255});
        drawText(renderer, "A/Z RATIO DOWN/UP", 58, 108, {180, 190, 210, 255});
        drawText(renderer, "SPACE PREVIEW  ENTER APPLY", 58, 124, {180, 190, 210, 255});
        drawText(renderer, "ESCAPE CANCEL", 58, 142, {245, 157, 76, 255});
    }
    if (state.fadeInPopupOpen || state.fadeOutPopupOpen) {
        const bool fadeIn = state.fadeInPopupOpen;
        fillRect(renderer, 48, 58, 304, 116, {8, 12, 22, 255});
        fillRect(renderer, 52, 62, 296, 16, {45, 55, 82, 255});
        drawText(renderer, fadeIn ? "FADE IN PARAMETERS" : "FADE OUT PARAMETERS",
            62, 67, {158, 220, 255, 255});
        drawText(renderer, "DURATION " + std::to_string(fadeIn ? state.fadeInMs : state.fadeOutMs) + "MS",
            64, 94, {92, 211, 143, 255});
        drawText(renderer, "A/Z DURATION DOWN/UP", 64, 116, {180, 190, 210, 255});
        drawText(renderer, "CTRL FINE  SHIFT COARSE", 64, 132, {180, 190, 210, 255});
        drawText(renderer, "SPACE PREVIEW  ENTER APPLY", 64, 148, {245, 157, 76, 255});
        drawText(renderer, "ESCAPE CANCEL", 64, 164, {180, 190, 210, 255});
    }

}

}  // namespace

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC);
    if (Mix_OpenAudio(44100, AUDIO_F32SYS, 2, 256) < 0) {
        std::fprintf(stderr, "Audio disabled: %s\n", Mix_GetError());
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window* window = SDL_CreateWindow("Simpler Sampler", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, kScreenWidth, kScreenHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (window && !renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!window || !renderer) {
        std::fprintf(stderr, "SDL window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Texture* lowRes = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, kRenderWidth, kRenderHeight);
    if (!lowRes) {
        std::fprintf(stderr, "Render target failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    AppState state;
    loadSamples(state, argc, argv);
    for (int i = 0; i < SDL_GetNumAudioDevices(0); ++i) {
        const char* name = SDL_GetAudioDeviceName(i, 0);
        if (name != nullptr) state.audioDevices.emplace_back(name);
    }
    Mix_SetPostMix(postMixCallback, &state);
    MidiInput midiInput(state);
    midiInput.start();
    const auto midiPorts = MidiInput::listInputPorts();
    for (const auto& port : midiPorts) {
        state.midiDevices.push_back(port.name);
    }

    uint32_t lastTick = SDL_GetTicks();
    while (state.running.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                state.running.store(false);
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                if (state.settingsOpen && event.key.keysym.sym == SDLK_ESCAPE) {
                    state.settingsOpen = false;
                } else if (state.settingsOpen && event.key.keysym.sym == SDLK_TAB) {
                    state.settingsMidiFocus = !state.settingsMidiFocus;
                } else if (state.settingsOpen && (event.key.keysym.sym == SDLK_UP ||
                    event.key.keysym.sym == SDLK_DOWN)) {
                    const int direction = event.key.keysym.sym == SDLK_UP ? -1 : 1;
                    if (state.settingsMidiFocus && !state.midiDevices.empty()) {
                        const int count = static_cast<int>(state.midiDevices.size());
                        state.midiDeviceSelection = static_cast<size_t>(
                            (static_cast<int>(state.midiDeviceSelection) + direction + count) % count);
                    } else if (!state.audioDevices.empty()) {
                        const int count = static_cast<int>(state.audioDevices.size());
                        state.audioDeviceSelection = static_cast<size_t>(
                            (static_cast<int>(state.audioDeviceSelection) + direction + count) % count);
                    }
                } else if (state.settingsOpen && event.key.keysym.sym == SDLK_RETURN) {
                    if (state.settingsMidiFocus) {
                        midiInput.connectIndex(state.midiDeviceSelection);
                    } else if (!state.audioDevices.empty()) {
                        Mix_PauseAudio(1);
                        Mix_SetPostMix(nullptr, nullptr);
                        Mix_CloseAudio();
                        if (Mix_OpenAudioDevice(44100, AUDIO_F32SYS, 2, 256, 
                                state.audioDevices[state.audioDeviceSelection].c_str(), 0) < 0) {
                            std::fprintf(stderr, "Audio device failed: %s\n", Mix_GetError());
                        }
                        Mix_SetPostMix(postMixCallback, &state);
                        Mix_PauseAudio(0);
                    }
                } else if (event.key.keysym.sym == SDLK_m && !state.editorOpen &&
                    !state.browserOpen && !state.settingsOpen) {
                    state.settingsOpen = true;
                    state.settingsMidiFocus = false;
                    state.midiDevices.clear();
                    for (const auto& port : MidiInput::listInputPorts()) {
                        state.midiDevices.push_back(port.name);
                    }
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.timeStretchPopupOpen) {
                    state.voices.samples[state.selectedPad.load()] = state.timeStretchSource;
                    if (!state.undoStack.empty()) state.undoStack.pop_back();
                    state.timeStretchPopupOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.fadeInPopupOpen) {
                    state.voices.samples[state.selectedPad.load()] = state.fadeInSource;
                    if (!state.undoStack.empty()) state.undoStack.pop_back();
                    state.fadeInPopupOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.fadeOutPopupOpen) {
                    state.voices.samples[state.selectedPad.load()] = state.fadeOutSource;
                    if (!state.undoStack.empty()) state.undoStack.pop_back();
                    state.fadeOutPopupOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.reversePopupOpen) {
                    state.voices.samples[state.selectedPad.load()] = state.reverseSource;
                    if (!state.undoStack.empty()) state.undoStack.pop_back();
                    state.reversePopupOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.distortionPopupOpen) {
                    state.voices.samples[state.selectedPad.load()] = state.distortionSource;
                    if (!state.undoStack.empty()) state.undoStack.pop_back();
                    state.distortionPopupOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.compressorPopupOpen) {
                    state.voices.samples[state.selectedPad.load()] = state.compressorSource;
                    if (!state.undoStack.empty()) state.undoStack.pop_back();
                    state.compressorPopupOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.flangerPopupOpen) {
                    state.voices.samples[state.selectedPad.load()] = state.flangerSource;
                    if (!state.undoStack.empty()) state.undoStack.pop_back();
                    state.flangerPopupOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.echoPopupOpen) {
                    state.voices.samples[state.selectedPad.load()] = state.echoSource;
                    if (!state.undoStack.empty()) state.undoStack.pop_back();
                    state.echoPopupOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.editorOpen) {
                    state.editorOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE && state.browserOpen) {
                    state.browserOpen = false;
                } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    state.running.store(false);
                } else if (state.timeStretchPopupOpen && (event.key.keysym.sym == SDLK_a ||
                    event.key.keysym.sym == SDLK_z)) {
                    const float step = (event.key.keysym.mod & KMOD_SHIFT) ? 0.5f :
                        ((event.key.keysym.mod & KMOD_CTRL) ? 0.01f : 0.1f);
                    const float direction = event.key.keysym.sym == SDLK_a ? -1.0f : 1.0f;
                    state.timeStretchRatio = std::clamp(state.timeStretchRatio + direction * step, 0.25f, 4.0f);
                } else if (state.timeStretchPopupOpen && event.key.keysym.sym == SDLK_SPACE) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.timeStretchSource;
                    applyTimeStretchToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    triggerPad(state, state.selectedPad.load());
                } else if (state.timeStretchPopupOpen && event.key.keysym.sym == SDLK_RETURN) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.timeStretchSource;
                    applyTimeStretchToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    state.timeStretchPopupOpen = false;
                } else if (state.timeStretchPopupOpen) {
                    continue;
                } else if ((state.fadeInPopupOpen || state.fadeOutPopupOpen) &&
                    (event.key.keysym.sym == SDLK_a || event.key.keysym.sym == SDLK_z)) {
                    const int step = (event.key.keysym.mod & KMOD_SHIFT) ? 100 :
                        ((event.key.keysym.mod & KMOD_CTRL) ? 1 : 10);
                    const int direction = event.key.keysym.sym == SDLK_a ? -1 : 1;
                    if (state.fadeInPopupOpen) {
                        state.fadeInMs = std::clamp(state.fadeInMs + direction * step, 1, 10000);
                    } else {
                        state.fadeOutMs = std::clamp(state.fadeOutMs + direction * step, 1, 10000);
                    }
                } else if (state.fadeInPopupOpen && event.key.keysym.sym == SDLK_SPACE) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.fadeInSource;
                    applyFadeInToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    triggerPad(state, state.selectedPad.load());
                } else if (state.fadeOutPopupOpen && event.key.keysym.sym == SDLK_SPACE) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.fadeOutSource;
                    applyFadeOutToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    triggerPad(state, state.selectedPad.load());
                } else if (state.fadeInPopupOpen && event.key.keysym.sym == SDLK_RETURN) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.fadeInSource;
                    applyFadeInToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    state.fadeInPopupOpen = false;
                } else if (state.fadeOutPopupOpen && event.key.keysym.sym == SDLK_RETURN) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.fadeOutSource;
                    applyFadeOutToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    state.fadeOutPopupOpen = false;
                } else if (state.fadeInPopupOpen || state.fadeOutPopupOpen) {
                    continue;
                } else if (state.reversePopupOpen && (event.key.keysym.sym == SDLK_a ||
                    event.key.keysym.sym == SDLK_z)) {
                    const float step = (event.key.keysym.mod & KMOD_SHIFT) ? 0.25f :
                        ((event.key.keysym.mod & KMOD_CTRL) ? 0.01f : 0.05f);
                    const float direction = event.key.keysym.sym == SDLK_a ? -1.0f : 1.0f;
                    state.reverseMix = std::clamp(state.reverseMix + direction * step, 0.0f, 1.0f);
                } else if (state.reversePopupOpen && event.key.keysym.sym == SDLK_SPACE) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.reverseSource;
                    applyReverseToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    triggerPad(state, state.selectedPad.load());
                } else if (state.reversePopupOpen && event.key.keysym.sym == SDLK_RETURN) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.reverseSource;
                    applyReverseToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    state.reversePopupOpen = false;
                } else if (state.reversePopupOpen) {
                    continue;
                } else if (state.distortionPopupOpen && (event.key.keysym.sym == SDLK_a ||
                    event.key.keysym.sym == SDLK_z || event.key.keysym.sym == SDLK_s ||
                    event.key.keysym.sym == SDLK_x)) {
                    const float step = (event.key.keysym.mod & KMOD_SHIFT) ? 0.5f :
                        ((event.key.keysym.mod & KMOD_CTRL) ? 0.01f : 0.1f);
                    if (event.key.keysym.sym == SDLK_a) state.distortionDrive = std::clamp(state.distortionDrive - step, 1.0f, 20.0f);
                    if (event.key.keysym.sym == SDLK_z) state.distortionDrive = std::clamp(state.distortionDrive + step, 1.0f, 20.0f);
                    if (event.key.keysym.sym == SDLK_s) state.distortionMix = std::clamp(state.distortionMix - step, 0.0f, 1.0f);
                    if (event.key.keysym.sym == SDLK_x) state.distortionMix = std::clamp(state.distortionMix + step, 0.0f, 1.0f);
                } else if (state.distortionPopupOpen && event.key.keysym.sym == SDLK_SPACE) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.distortionSource;
                    applyDistortionToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    triggerPad(state, state.selectedPad.load());
                } else if (state.distortionPopupOpen && event.key.keysym.sym == SDLK_RETURN) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.distortionSource;
                    applyDistortionToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    state.distortionPopupOpen = false;
                } else if (state.distortionPopupOpen) {
                    continue;
                } else if (state.compressorPopupOpen && (event.key.keysym.sym == SDLK_q ||
                    event.key.keysym.sym == SDLK_a || event.key.keysym.sym == SDLK_w ||
                    event.key.keysym.sym == SDLK_s || event.key.keysym.sym == SDLK_e ||
                    event.key.keysym.sym == SDLK_d)) {
                    const float step = (event.key.keysym.mod & KMOD_SHIFT) ? 5.0f :
                        ((event.key.keysym.mod & KMOD_CTRL) ? 0.1f : 1.0f);
                    if (event.key.keysym.sym == SDLK_q) state.compressorThreshold = std::clamp(state.compressorThreshold - step, -60.0f, 0.0f);
                    if (event.key.keysym.sym == SDLK_a) state.compressorThreshold = std::clamp(state.compressorThreshold + step, -60.0f, 0.0f);
                    if (event.key.keysym.sym == SDLK_w) state.compressorRatio = std::clamp(state.compressorRatio - step, 1.0f, 20.0f);
                    if (event.key.keysym.sym == SDLK_s) state.compressorRatio = std::clamp(state.compressorRatio + step, 1.0f, 20.0f);
                    if (event.key.keysym.sym == SDLK_e) state.compressorMakeup = std::clamp(state.compressorMakeup - step / 10.0f, 0.1f, 4.0f);
                    if (event.key.keysym.sym == SDLK_d) state.compressorMakeup = std::clamp(state.compressorMakeup + step / 10.0f, 0.1f, 4.0f);
                } else if (state.compressorPopupOpen && event.key.keysym.sym == SDLK_SPACE) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.compressorSource;
                    applyCompressorToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    triggerPad(state, state.selectedPad.load());
                } else if (state.compressorPopupOpen && event.key.keysym.sym == SDLK_RETURN) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.compressorSource;
                    applyCompressorToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    state.compressorPopupOpen = false;
                } else if (state.compressorPopupOpen) {
                    continue;
                } else if (state.flangerPopupOpen && event.key.keysym.sym == SDLK_SPACE) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.flangerSource;
                    applyFlangerToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    triggerPad(state, state.selectedPad.load());
                } else if (state.flangerPopupOpen && event.key.keysym.sym == SDLK_RETURN) {
                    Mix_PauseAudio(1);
                    state.voices.samples[state.selectedPad.load()] = state.flangerSource;
                    applyFlangerToSample(state, state.voices.samples[state.selectedPad.load()]);
                    Mix_PauseAudio(0);
                    state.flangerPopupOpen = false;
                } else if (state.flangerPopupOpen && (event.key.keysym.sym == SDLK_a ||
                    event.key.keysym.sym == SDLK_z || event.key.keysym.sym == SDLK_s ||
                    event.key.keysym.sym == SDLK_x || event.key.keysym.sym == SDLK_d ||
                    event.key.keysym.sym == SDLK_c || event.key.keysym.sym == SDLK_f ||
                    event.key.keysym.sym == SDLK_v)) {
                    const bool fine = (event.key.keysym.mod & KMOD_CTRL) != 0;
                    const bool coarse = (event.key.keysym.mod & KMOD_SHIFT) != 0;
                    const float amount = coarse ? 10.0f : (fine ? 0.1f : 1.0f);
                    const float rateAmount = coarse ? 0.5f : (fine ? 0.01f : 0.1f);
                    switch (event.key.keysym.sym) {
                    case SDLK_a: state.flangerDelayMs = std::clamp(state.flangerDelayMs - static_cast<int>(amount), 1, 50); break;
                    case SDLK_z: state.flangerDelayMs = std::clamp(state.flangerDelayMs + static_cast<int>(amount), 1, 50); break;
                    case SDLK_s: state.flangerDepthMs = std::clamp(state.flangerDepthMs - amount, 0.0f, 15.0f); break;
                    case SDLK_x: state.flangerDepthMs = std::clamp(state.flangerDepthMs + amount, 0.0f, 15.0f); break;
                    case SDLK_d: state.flangerRateHz = std::clamp(state.flangerRateHz - rateAmount, 0.05f, 10.0f); break;
                    case SDLK_c: state.flangerRateHz = std::clamp(state.flangerRateHz + rateAmount, 0.05f, 10.0f); break;
                    case SDLK_f: state.flangerFeedback = std::clamp(state.flangerFeedback - amount / 100.0f, 0.0f, 0.95f); break;
                    case SDLK_v: state.flangerFeedback = std::clamp(state.flangerFeedback + amount / 100.0f, 0.0f, 0.95f); break;
                    default: break;
                    }
                } else if (state.flangerPopupOpen) {
                    continue;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_TAB) {
                    state.editorAdjustEnd = !state.editorAdjustEnd;
                } else if (event.key.keysym.sym == SDLK_TAB) {
                    state.page = 1 - state.page;
                    state.selectedPad.store(state.page * kPadsPerPage);
                } else if (event.key.keysym.sym == SDLK_e && !state.browserOpen && !state.editorOpen) {
                    state.editorOpen = !state.editorOpen;
                    if (state.editorOpen) resetEditor(state);
                } else if (state.editorOpen && (event.key.keysym.sym == SDLK_LEFT ||
                                                event.key.keysym.sym == SDLK_RIGHT)) {
                    const int step = trimStep(event.key);
                    const int direction = event.key.keysym.sym == SDLK_LEFT ? -1 : 1;
                    const int sampleSize = static_cast<int>(
                        state.voices.samples[state.selectedPad.load()].stereo.size());
                    if (state.editorAdjustEnd) {
                        state.editorEnd = std::clamp(state.editorEnd + direction * step,
                            state.editorStart + 2, sampleSize);
                    } else {
                        state.editorStart = std::clamp(state.editorStart + direction * step,
                            0, state.editorEnd - 2);
                    }
                } else if (state.editorOpen && (event.key.keysym.sym == SDLK_EQUALS ||
                                                event.key.keysym.sym == SDLK_PLUS)) {
                    state.editorGain = std::min(2.0f, state.editorGain + 0.05f);
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_MINUS) {
                    state.editorGain = std::max(0.5f, state.editorGain - 0.05f);
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_SPACE) {
                    triggerPad(state, state.selectedPad.load());
                } else if (state.echoPopupOpen && (event.key.keysym.sym == SDLK_SEMICOLON ||
                                                event.key.keysym.sym == SDLK_QUOTE)) {
                    const int step = (event.key.keysym.mod & KMOD_SHIFT) != 0 ? 100 :
                        ((event.key.keysym.mod & KMOD_CTRL) != 0 ? 1 : 10);
                    const int direction = event.key.keysym.sym == SDLK_SEMICOLON ? -1 : 1;
                    state.echoTimeMs = std::clamp(state.echoTimeMs + direction * step, 1, 2000);
                } else if (state.echoPopupOpen && (event.key.keysym.sym == SDLK_COMMA ||
                                                event.key.keysym.sym == SDLK_PERIOD)) {
                    const float step = (event.key.keysym.mod & KMOD_SHIFT) != 0 ? 0.1f :
                        ((event.key.keysym.mod & KMOD_CTRL) != 0 ? 0.01f : 0.05f);
                    const float direction = event.key.keysym.sym == SDLK_COMMA ? -1.0f : 1.0f;
                    state.echoPower = std::clamp(state.echoPower + direction * step, 0.0f, 0.95f);
                } else if (state.echoPopupOpen && event.key.keysym.sym == SDLK_SPACE) {
                const int echoTime = state.echoTimeMs;
                const float echoPower = state.echoPower;
                Mix_PauseAudio(1);
                state.voices.samples[state.selectedPad.load()] = state.echoSource;
                applyEchoToSample(state, state.voices.samples[state.selectedPad.load()]);
                Mix_PauseAudio(0);
                state.echoTimeMs = echoTime;
                state.echoPower = echoPower;
                triggerPad(state, state.selectedPad.load());
                state.echoPopupOpen = true;
                } else if (state.echoPopupOpen && event.key.keysym.sym == SDLK_RETURN) {
                if (state.voices.samples[state.selectedPad.load()].stereo ==
                    state.echoSource.stereo) {
                    state.voices.samples[state.selectedPad.load()] = state.echoSource;
                    applyEchoToSample(state, state.voices.samples[state.selectedPad.load()]);
                }
                Mix_PauseAudio(1);
                Mix_PauseAudio(0);
                state.echoPopupOpen = false;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_RETURN) {
                    state.editorOpen = false;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_u) {
                    undoEditor(state);
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_z) {
                    applyEditor(state, 'z');
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_LEFTBRACKET) {
                    applyEditor(state, 'a', silenceFrames(event.key));
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_RIGHTBRACKET) {
                    applyEditor(state, 'd', silenceFrames(event.key));
                } else if (state.echoPopupOpen) {
                    continue;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_l) {
                    state.flangerSource = state.voices.samples[state.selectedPad.load()];
                    saveUndo(state);
                    state.flangerPopupOpen = true;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_c) {
                    state.compressorSource = state.voices.samples[state.selectedPad.load()];
                    saveUndo(state);
                    state.compressorPopupOpen = true;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_o) {
                    state.distortionSource = state.voices.samples[state.selectedPad.load()];
                    saveUndo(state);
                    state.distortionPopupOpen = true;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_i) {
                    state.fadeInSource = state.voices.samples[state.selectedPad.load()];
                    saveUndo(state);
                    state.fadeInPopupOpen = true;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_v) {
                    state.fadeOutSource = state.voices.samples[state.selectedPad.load()];
                    saveUndo(state);
                    state.fadeOutPopupOpen = true;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_r) {
                    state.reverseSource = state.voices.samples[state.selectedPad.load()];
                    saveUndo(state);
                    state.reversePopupOpen = true;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_y) {
                    state.timeStretchSource = state.voices.samples[state.selectedPad.load()];
                    saveUndo(state);
                    state.timeStretchPopupOpen = true;
                } else if (state.editorOpen && event.key.keysym.sym == SDLK_e) {
                    state.echoSource = state.voices.samples[state.selectedPad.load()];
                    saveUndo(state);
                    state.echoPopupOpen = true;
                } else if (state.editorOpen && std::strchr("tnfrgp", static_cast<char>(event.key.keysym.sym))) {
                    Mix_PauseAudio(1);
                    applyEditor(state, static_cast<char>(event.key.keysym.sym));
                    Mix_PauseAudio(0);
                } else if (state.editorOpen) {
                    continue;
                } else if (event.key.keysym.sym == SDLK_b) {
                    state.browserOpen = !state.browserOpen;
                    if (state.browserOpen || state.editorOpen) {
                        refreshBrowser(state);
                    }
                } else if (state.browserOpen && event.key.keysym.sym == SDLK_UP) {
                    if (!state.browserFiles.empty() && state.browserSelection > 0) {
                        --state.browserSelection;
                    }
                } else if (state.browserOpen && event.key.keysym.sym == SDLK_DOWN) {
                    if (!state.browserFiles.empty() && state.browserSelection + 1 < state.browserFiles.size()) {
                        ++state.browserSelection;
                    }
                } else if (state.browserOpen && event.key.keysym.sym == SDLK_RETURN) {
                    if (!state.browserFiles.empty()) {
                        Mix_PauseAudio(1);
                        loadSample(state.browserFiles[state.browserSelection],
                                    state.voices.samples[state.selectedPad.load()]);
                        state.undoStack.clear();
                        Mix_PauseAudio(0);
                        state.browserOpen = false;
                    }
                } else if (state.browserOpen) {
                    continue;
                } else if (event.key.keysym.sym >= SDLK_1 && event.key.keysym.sym <= SDLK_8) {
                    triggerPad(state, state.page * kPadsPerPage + event.key.keysym.sym - SDLK_1);
                } else if (event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_RIGHT) {
                    const int delta = event.key.keysym.sym == SDLK_LEFT ? -1 : 1;
                    const int pageStart = state.page * kPadsPerPage;
                    const int pageOffset = (state.selectedPad.load() - pageStart + delta + kPadsPerPage) % kPadsPerPage;
                    state.selectedPad.store(pageStart + pageOffset);
                } else if (event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_DOWN) {
                    const int pad = state.selectedPad.load();
                    const float delta = event.key.keysym.sym == SDLK_UP ? 0.05f : -0.05f;
                    state.pitch[pad] = std::clamp(state.pitch[pad] + delta, 0.5f, 2.0f);
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN) {
                int width = 0;
                int height = 0;
                SDL_GetRendererOutputSize(renderer, &width, &height);
                const int x = event.button.x * kRenderWidth / std::max(width, 1);
                const int y = event.button.y * kRenderHeight / std::max(height, 1);
                for (int i = 0; i < kPadsPerPage; ++i) {
                    const int padX = 18 + (i % 4) * 92;
                    const int padY = 42 + (i / 4) * 76;
                    if (x >= padX && x < padX + 84 && y >= padY && y < padY + 66) {
                        triggerPad(state, state.page * kPadsPerPage + i);
                    }
                }
            }
        }

        const uint32_t now = SDL_GetTicks();
        const uint32_t midiTriggers = state.midiTriggers.exchange(0);
        for (int pad = 0; pad < kPadCount; ++pad) {
            if ((midiTriggers & (1u << pad)) != 0) {
                triggerPad(state, pad);
            }
        }
        SDL_SetRenderTarget(renderer, lowRes);
        drawFrame(renderer, state, now);
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_RenderClear(renderer);
        int outputWidth = 0;
        int outputHeight = 0;
        SDL_GetRendererOutputSize(renderer, &outputWidth, &outputHeight);
        SDL_Rect destination{0, 0, outputWidth, outputHeight};
        SDL_RenderCopy(renderer, lowRes, nullptr, &destination);
        applyGraphicsEffects(renderer, outputWidth, outputHeight, now);
        SDL_RenderPresent(renderer);
        const uint32_t frameTime = SDL_GetTicks() - lastTick;
        if (frameTime < 16) {
            SDL_Delay(16 - frameTime);
        }
        lastTick = now;
    }

    Mix_SetPostMix(nullptr, nullptr);
    midiInput.stop();
    Mix_CloseAudio();
    Mix_Quit();
    SDL_DestroyTexture(lowRes);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
