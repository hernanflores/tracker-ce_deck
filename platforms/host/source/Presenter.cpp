#include "Presenter.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

static constexpr int kPrescale = 2;

bool Presenter::init(double scale, bool fullscreen)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
        return false;
    }
    const int w = (int)(800 * scale);
    const int h = (int)(480 * scale);
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
    window = SDL_CreateWindow("Tracker CE (desktop, unofficial)", w, h, flags);
    if (window == nullptr)
    {
        fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr)
    {
        fprintf(stderr, "cannot create renderer: %s\n", SDL_GetError());
        shutdown();
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);
    return true;
}

void Presenter::shutdown()
{
    if (scaledTex) SDL_DestroyTexture(scaledTex);
    if (frameTex) SDL_DestroyTexture(frameTex);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    scaledTex = frameTex = nullptr;
    renderer = nullptr;
    window = nullptr;
    haveFrame = false;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Presenter::present(const EveFrame& f)
{
    if (!renderer || f.width <= 0 || f.height <= 0) return;

    if (frameTex == nullptr || f.width != texW || f.height != texH)
    {
        if (scaledTex) SDL_DestroyTexture(scaledTex);
        if (frameTex) SDL_DestroyTexture(frameTex);
        texW = f.width;
        texH = f.height;
        frameTex = SDL_CreateTexture(
          renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, texW, texH);
        scaledTex = SDL_CreateTexture(renderer,
                                      SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_TARGET,
                                      texW * kPrescale,
                                      texH * kPrescale);
        if (!frameTex || !scaledTex) return;
        SDL_SetTextureScaleMode(frameTex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureScaleMode(scaledTex, SDL_SCALEMODE_LINEAR);
    }

    SDL_UpdateTexture(frameTex, nullptr, f.pixels.data(), f.width * (int)sizeof(uint32_t));
    haveFrame = true;
    redraw();
}

void Presenter::redraw()
{
    if (!haveFrame) return;

    SDL_SetRenderTarget(renderer, scaledTex);
    SDL_RenderTexture(renderer, frameTex, nullptr, nullptr);
    SDL_SetRenderTarget(renderer, nullptr);

    int ww = 0, wh = 0;
    SDL_GetRenderOutputSize(renderer, &ww, &wh);
    const float s = std::min((float)ww / texW, (float)wh / texH);
    SDL_FRect dst = { (ww - texW * s) / 2, (wh - texH * s) / 2, texW * s, texH * s };

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, scaledTex, nullptr, &dst);
    SDL_RenderPresent(renderer);
}

bool Presenter::pumpEvents()
{
    SDL_Event e;
    bool open = true;
    bool exposed = false;
    while (SDL_PollEvent(&e))
    {
        switch (e.type)
        {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: open = false; break;
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_RESIZED: exposed = true; break;
            default: break;
        }
    }
    if (exposed) redraw();
    return open;
}
