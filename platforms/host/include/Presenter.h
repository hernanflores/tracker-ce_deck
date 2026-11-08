#ifndef HOST_PRESENTER_H_
#define HOST_PRESENTER_H_

#include "eve/EveDevice.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

// Window that shows the 800x480 display. Frames are pre-scaled with nearest-neighbour by an integer
// factor and then scaled to the window with linear filtering ("sharp bilinear"), letterboxed.
// Must be used from the main thread.
class Presenter
{
  public:
    ~Presenter() { shutdown(); }

    bool init(double scale, bool fullscreen);
    void shutdown();
    bool active() const { return window != nullptr; }

    void present(const EveFrame& frame);
    // handle window events; false once the window has been closed
    bool pumpEvents();

  private:
    void redraw();

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* frameTex = nullptr;
    SDL_Texture* scaledTex = nullptr;
    int texW = 0, texH = 0;
    bool haveFrame = false;
};

#endif
