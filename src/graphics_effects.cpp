#include "graphics_effects.h"

#include <cmath>

void applyGraphicsEffects(SDL_Renderer* renderer, int width, int height,
    uint32_t now) {
    if (renderer == nullptr || width <= 0 || height <= 0) {
        return;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // A single pass of wide, evenly spaced lines gives a CRT look cheaply.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 58);
    for (int y = 2; y < height; y += 4) {
        SDL_RenderDrawLine(renderer, 0, y, width - 1, y);
    }

    // Keep the sweep slow and intermittent without any GPU-specific API.
    constexpr int phosphorTail = 140;
    constexpr uint32_t sweepTravelMs = 1400;
    constexpr uint32_t sweepDrainMs = 700;
    constexpr uint32_t sweepRunMs = sweepTravelMs + sweepDrainMs;
    constexpr uint32_t sweepPauseMs = 2800;
    constexpr uint32_t sweepCycleMs = sweepRunMs + sweepPauseMs;
    const uint32_t cyclePosition = now % sweepCycleMs;
    if (cyclePosition < sweepRunMs) {
        int y = 0;
        if (cyclePosition < sweepTravelMs) {
            // Ease the visible travel at both ends so the beam does not jump
            // into or out of the screen.
            const float progress = static_cast<float>(cyclePosition)
                / static_cast<float>(sweepTravelMs);
            const float easedProgress = progress * progress
                * (3.0f - 2.0f * progress);
            y = static_cast<int>(easedProgress * height);
        } else {
            // Let the beam leave the bottom instead of cutting its trail off
            // at the last visible row.
            const float drainProgress = static_cast<float>(
                cyclePosition - sweepTravelMs) / static_cast<float>(sweepDrainMs);
            y = height + static_cast<int>(drainProgress * phosphorTail);
        }

        // A very long Gaussian tail gives the beam a pronounced phosphor
        // persistence effect while retaining a brighter scanline core.
        for (int offset = -phosphorTail; offset <= 3; ++offset) {
            const int lineY = y + offset;
            if (lineY < 0 || lineY >= height) {
                continue;
            }
            const float distance = static_cast<float>(offset);
            const float tail = std::exp(-(distance * distance) / 4200.0f);
            const float core = offset >= -1 && offset <= 1 ? 1.0f : 0.0f;
            const Uint8 alpha = static_cast<Uint8>(2.0f + 72.0f * tail
                + 18.0f * core);
            SDL_SetRenderDrawColor(renderer, 125, 190, 235, alpha);
            SDL_RenderDrawLine(renderer, 0, lineY, width - 1, lineY);
        }
    }

    // Four edge strips provide a restrained CRT vignette without per-pixel work.
    const int edgeX = width / 18;
    const int edgeY = height / 14;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 24);
    SDL_Rect left{0, 0, edgeX, height};
    SDL_Rect right{width - edgeX, 0, edgeX, height};
    SDL_Rect top{0, 0, width, edgeY};
    SDL_Rect bottom{0, height - edgeY, width, edgeY};
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &right);
    SDL_RenderFillRect(renderer, &top);
    SDL_RenderFillRect(renderer, &bottom);
}
