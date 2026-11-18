#pragma once
// What the Android host's parts share: the window, the app's lifecycle and the event routing
#include <cstdint>
#include <vector>

union SDL_Event;
struct SDL_Window;

namespace input {
struct Pad;
}

namespace host {

SDL_Window* Window();
// the app is in front with a window to draw to; the renderer and the game wait while it is not
bool Foreground();
// counts the native windows Android gave the app: the renderer makes its surface again when this changes
uint32_t SurfaceEpoch();
// the game thread, before a present: returns once the app is in front again
void WaitForeground();
// the window's size in pixels
void WindowPixels(int& w, int& h);

}  // namespace host

// the render resolution following the frame rate (dynamic_resolution.cpp)
namespace dynres {
uint32_t Percent();      // the render resolution in use, percent of the screen's: RENDER RESOLUTION's while it is off
void GpuWait(double ms);  // game thread, after waiting for the frame before: how long the GPU held it
void Frame(double fps);   // vulkan thread, each present: a step down or up as the frame rate and the GPU ask
}  // namespace dynres

// controllers through SDL (input.cpp): the main thread hands over their plug events
namespace input {
void OnPadEvent(const SDL_Event& e);
}

// the touch controls and SDL's keyboard and mouse (touch.cpp)
namespace touch {
// main thread, every input event; true when the touch controls took it
bool OnEvent(const SDL_Event& e);
// the touch controls' buttons and stick onto port 0's pad, before the game reads it
void Apply(int port, input::Pad& pad);
// a fresh tap of the weapon arrows since the last call: -1 the weapon before, 1 the weapon after, 0 for none
int TakeWeaponArrow();
// inside the ImGui frame: the controls over the picture
void Draw();
// any touch on the screen in the last few seconds: the controls show
bool Shown();
// main thread, each turn of the event loop: what the input asks of the window (the on-screen keyboard)
void Pump();
// the view turns as the mouse turns it, in mouse counts, while the game is in play
void AddLook(float dx, float dy);
bool LookActive();  // in play, in front, no menu or panel up
bool Firing();      // a fire button of the touch controls, or a controller's right trigger, is held
// a controller was used: the game's hints name its buttons again, not the touch controls
void PadUsed();
// the layout's settings in the game's options (src/common/menu.cpp), saved at once
const char* LayoutName();  // the preset's, or CUSTOM
void StepLayout(int step);
uint32_t ControlsOpacity();  // percent
void StepOpacity(int step);
uint32_t ControlsSize();  // percent of each control's own size
void StepSize(int step);
void EditLayout();  // the controls over the screen to drag where they go
const char* StickModeName();  // FLOATING (under the thumb) or FIXED (in its place)
void StepStickMode(int step);
}  // namespace touch

// ImGui's input from SDL's events (ui.cpp), main thread, under app::UiLock
namespace ui {
void OnEvent(const SDL_Event& e);
// render thread, before ImGui::NewFrame: the display size and the frame time
void NewFrame(float width, float height);
}  // namespace ui
