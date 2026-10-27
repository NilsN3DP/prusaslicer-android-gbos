#pragma once

#include <chrono>
#include <memory>

#include "Slic3r/App/Platform/AbstractRenderCanvas.hpp"
#include "Slic3r/App/Platform/SDL/SDLMainThreadDispatcher.hpp"

union SDL_Event;
struct SDL_Window;

namespace Slic3r::App::Platform::SDL {

/**
 * SDL2 implementation of the platform render canvas (Android: OpenGL ES 3 via EGL).
 *
 * The owner pumps SDL events into `handle_event()` and calls `idle()` once per loop
 * iteration; the canvas renders only when a frame was requested.
 */
class SDLRenderCanvas : public Platform::AbstractRenderCanvas
{
public:
    explicit SDLRenderCanvas(SDL_Window* window);
    ~SDLRenderCanvas() override;

    SDLRenderCanvas(const SDLRenderCanvas&)            = delete;
    SDLRenderCanvas& operator=(const SDLRenderCanvas&) = delete;

    void render() override;

    void handle_event(const SDL_Event& event);
    void idle();

    bool has_fullscreen() const override { return true; }
    bool is_fullscreen() const override;
    void set_fullscreen(bool on) override;
    void close_application() override;

    bool quit_requested() const { return m_quit_requested; }

protected:
    void on_render_requested() override;
    bool begin_frame_platform() override;
    void begin_imgui_frame_platform() override;
    void end_imgui_frame_platform() override;
    void end_frame_platform() override;
    double platform_time() override;
    Render::Device& device() override;

private:
    void init();
    void init_imgui();
    void on_mouse_button(const SDL_Event& event, bool down);
    void on_mouse_motion(const SDL_Event& event);
    void on_mouse_wheel(const SDL_Event& event);
    void on_key(const SDL_Event& event, bool down);
    void on_text_input(const SDL_Event& event);
    void update_text_input_state();
    float dpi_scale() const;
    int to_dip(int coord) const;
    KeyModifiers modifiers() const;

    using Clock = std::chrono::steady_clock;

    SDL_Window* m_window{nullptr};
    void* m_gl_context{nullptr};
    std::chrono::time_point<Clock> m_start_time;
    bool m_initialized{false};
    bool m_in_render{false};
    bool m_quit_requested{false};
    bool m_text_input_active{false};
};

} // namespace Slic3r::App::Platform::SDL
