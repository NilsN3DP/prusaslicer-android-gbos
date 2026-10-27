#include "Slic3r/App/Platform/SDL/SDLRenderCanvas.hpp"

#include <SDL.h>
#include <GL/glew.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_opengl3.h>

#include <Slic3r/App/AppConfig.hpp>
#include <Slic3r/App/AppServices.hpp>
#include <Slic3r/App/Platform/PlatformError.hpp>
#include <Slic3r/App/Render/Context.hpp>
#include <Slic3r/App/Render/Device.hpp>
#include <Slic3r/App/Render/Init.hpp>
#include <Slic3r/App/Theme.hpp>
#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include <Slic3r/Log.hpp>

namespace Slic3r::App::Platform::SDL {

namespace {

// Prusa's KeyCode values are SDL keycodes, except that letters are upper case.
KeyCode to_key_code(SDL_Keycode sym)
{
    if (sym >= SDLK_a && sym <= SDLK_z)
        return KeyCode(sym - SDLK_a + 'A');
    return KeyCode(sym);
}

ImGuiKey to_imgui_key(SDL_Keycode sym)
{
    if (sym >= SDLK_a && sym <= SDLK_z)
        return ImGuiKey(ImGuiKey_A + (sym - SDLK_a));
    if (sym >= SDLK_0 && sym <= SDLK_9)
        return ImGuiKey(ImGuiKey_0 + (sym - SDLK_0));
    if (sym >= SDLK_F1 && sym <= SDLK_F12)
        return ImGuiKey(ImGuiKey_F1 + (sym - SDLK_F1));
    if (sym >= SDLK_KP_1 && sym <= SDLK_KP_9)
        return ImGuiKey(ImGuiKey_Keypad1 + (sym - SDLK_KP_1));

    switch (sym) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_INSERT: return ImGuiKey_Insert;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_AC_BACK: return ImGuiKey_Escape; // Android back button
    case SDLK_QUOTE: return ImGuiKey_Apostrophe;
    case SDLK_COMMA: return ImGuiKey_Comma;
    case SDLK_MINUS: return ImGuiKey_Minus;
    case SDLK_PERIOD: return ImGuiKey_Period;
    case SDLK_SLASH: return ImGuiKey_Slash;
    case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
    case SDLK_EQUALS: return ImGuiKey_Equal;
    case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
    case SDLK_BACKSLASH: return ImGuiKey_Backslash;
    case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
    case SDLK_BACKQUOTE: return ImGuiKey_GraveAccent;
    case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
    case SDLK_KP_0: return ImGuiKey_Keypad0;
    case SDLK_KP_PERIOD: return ImGuiKey_KeypadDecimal;
    case SDLK_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case SDLK_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case SDLK_KP_MINUS: return ImGuiKey_KeypadSubtract;
    case SDLK_KP_PLUS: return ImGuiKey_KeypadAdd;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_LGUI: return ImGuiKey_LeftSuper;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    case SDLK_RGUI: return ImGuiKey_RightSuper;
    default: return ImGuiKey_None;
    }
}

MouseButton to_mouse_button(uint8_t sdl_button)
{
    switch (sdl_button) {
    case SDL_BUTTON_LEFT: return MouseButton::Left;
    case SDL_BUTTON_RIGHT: return MouseButton::Right;
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    default: return MouseButton::NoButton;
    }
}

int imgui_button_index(MouseButton button)
{
    switch (button) {
    case MouseButton::Left: return 0;
    case MouseButton::Right: return 1;
    case MouseButton::Middle: return 2;
    default: return -1;
    }
}

} // namespace

namespace {

// GL_KHR_debug (core in ES 3.2): the driver (ANGLE) explains each GL error in plain text.
void GL_APIENTRY on_gl_debug_message(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message, const void*)
{
    static int s_messages = 0;
    if (severity == 0x826B /* GL_DEBUG_SEVERITY_NOTIFICATION */ || s_messages >= 200)
        return;
    ++s_messages;
    SPDLOG_WARN("GL debug (type 0x{:04x}, severity 0x{:04x}): {}", type, severity, message);
}

void enable_gl_debug_output()
{
    using Callback = void(GL_APIENTRY*)(GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar*, const void*);
    using SetCallback = void(GL_APIENTRY*)(Callback, const void*);
    auto set_callback = reinterpret_cast<SetCallback>(SDL_GL_GetProcAddress("glDebugMessageCallback"));
    if (set_callback == nullptr)
        set_callback = reinterpret_cast<SetCallback>(SDL_GL_GetProcAddress("glDebugMessageCallbackKHR"));
    if (set_callback == nullptr)
        return;
    glEnable(0x92E0 /* GL_DEBUG_OUTPUT */);
    glEnable(0x8242 /* GL_DEBUG_OUTPUT_SYNCHRONOUS */);
    set_callback(&on_gl_debug_message, nullptr);
}

} // namespace

SDLRenderCanvas::SDLRenderCanvas(SDL_Window* window) :
    m_window(window), m_start_time(Clock::now())
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    // Newest OpenGL ES first; e.g. the Android emulator stops at 3.1.
    for (int minor : {2, 1, 0}) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
        m_gl_context = SDL_GL_CreateContext(m_window);
        if (m_gl_context != nullptr)
            break;
    }
    if (m_gl_context == nullptr)
        throw PlatformError(std::string("Unable to create an OpenGL ES context: ") + SDL_GetError());
    SDL_GL_MakeCurrent(m_window, m_gl_context);
    SDL_GL_SetSwapInterval(1);
    enable_gl_debug_output();
}

SDLRenderCanvas::~SDLRenderCanvas()
{
    auto& platform_services = Biz::Platform::PlatformServices::instance();
    if (&platform_services.render_request_handler() == this)
        platform_services.set_render_request_handler(nullptr);
    if (m_initialized) {
        ImGui_ImplOpenGL3_Shutdown();
        Render::shutdown_render();
    }
    if (m_gl_context != nullptr)
        SDL_GL_DeleteContext(m_gl_context);
}

void SDLRenderCanvas::init()
{
    glGetError();
    Render::initialize_render();

    const Slic3r::Semver gl_version = Render::Context::instance().gl_version();
    if (gl_version.maj() < 3)
        throw PlatformError("OpenGL ES 3.0 or higher is required");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    init_imgui();
    ImGui_ImplOpenGL3_Init("#version 300 es");
}

void SDLRenderCanvas::init_imgui()
{
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_slic3r_sdl";
    io.ConfigMacOSXBehaviors = false;

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        SDL_SetClipboardText(text);
    };
    platform_io.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
        static std::string s_clipboard_text;
        char* text = SDL_GetClipboardText();
        s_clipboard_text = text != nullptr ? text : "";
        SDL_free(text);
        return s_clipboard_text.c_str();
    };

    AppServices::instance().theme().initialize_imgui_style();
}

void SDLRenderCanvas::render()
{
    SDL_GL_MakeCurrent(m_window, m_gl_context);
    if (!m_initialized) {
        init();
        m_initialized = true;
    }
    if (m_in_render)
        return;
    m_in_render = true;
    AbstractRenderCanvas::render();
    m_in_render = false;
    update_text_input_state();
}

void SDLRenderCanvas::idle()
{
    m_main_thread_dispatcher.dispatch_enqueued();
    if (get_and_reset_render_requested() || !m_initialized)
        render();
}

void SDLRenderCanvas::handle_event(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_QUIT:
        m_quit_requested = true;
        break;
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
            || event.window.event == SDL_WINDOWEVENT_EXPOSED
            || event.window.event == SDL_WINDOWEVENT_RESTORED)
            request_render();
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST && m_initialized)
            ImGui::GetIO().ClearInputKeys();
        break;
    case SDL_MOUSEMOTION:
        on_mouse_motion(event);
        break;
    case SDL_MOUSEBUTTONDOWN:
        on_mouse_button(event, true);
        break;
    case SDL_MOUSEBUTTONUP:
        on_mouse_button(event, false);
        break;
    case SDL_MOUSEWHEEL:
        on_mouse_wheel(event);
        break;
    case SDL_KEYDOWN:
        on_key(event, true);
        break;
    case SDL_KEYUP:
        on_key(event, false);
        break;
    case SDL_TEXTINPUT:
        on_text_input(event);
        break;
    default:
        if (event.type == SDLMainThreadDispatcher::wakeup_event_type())
            m_main_thread_dispatcher.dispatch_enqueued();
        break;
    }
}

float SDLRenderCanvas::dpi_scale() const
{
    // Android density buckets are relative to 160 dpi.
    float ddpi = 160.f;
    const int display = SDL_GetWindowDisplayIndex(m_window);
    if (SDL_GetDisplayDPI(display < 0 ? 0 : display, &ddpi, nullptr, nullptr) != 0 || ddpi <= 0.f)
        ddpi = 160.f;
    return std::max(1.f, ddpi / 160.f);
}

int SDLRenderCanvas::to_dip(int coord) const
{
    return int(coord / dpi_scale());
}

KeyModifiers SDLRenderCanvas::modifiers() const
{
    const SDL_Keymod mod = SDL_GetModState();
    KeyModifiers key_mods{0};
    if (mod & KMOD_CTRL)
        key_mods |= KeyModifiers(KeyModifier::Ctrl);
    if (mod & KMOD_SHIFT)
        key_mods |= KeyModifiers(KeyModifier::Shift);
    if (mod & KMOD_GUI)
        key_mods |= KeyModifiers(KeyModifier::Meta);
    if (mod & KMOD_ALT)
        key_mods |= KeyModifiers(KeyModifier::Alt);
    return key_mods;
}

void SDLRenderCanvas::on_mouse_motion(const SDL_Event& event)
{
    if (!m_initialized || !m_render_module->is_initialized())
        return;
    m_mouse_x = to_dip(event.motion.x);
    m_mouse_y = to_dip(event.motion.y);
    enqueue_mouse({MouseEvent::Type::Move, MouseButton::NoButton, m_mouse_x, m_mouse_y, 0, 0, modifiers()});
    ImGui::GetIO().AddMousePosEvent(float(m_mouse_x), float(m_mouse_y));
    request_render();
}

void SDLRenderCanvas::on_mouse_button(const SDL_Event& event, bool down)
{
    if (!m_initialized || !m_render_module->is_initialized())
        return;
    const MouseButton button = to_mouse_button(event.button.button);
    if (button == MouseButton::NoButton)
        return;

    m_mouse_x = to_dip(event.button.x);
    m_mouse_y = to_dip(event.button.y);

    MouseEvent::Type type = down ? MouseEvent::Type::ButtonDown : MouseEvent::Type::ButtonUp;
    enqueue_mouse({type, button, m_mouse_x, m_mouse_y, 0, 0, modifiers()});
    if (down && event.button.clicks == 2)
        enqueue_mouse({MouseEvent::Type::DoubleClick, button, m_mouse_x, m_mouse_y, 0, 0, modifiers()});

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(float(m_mouse_x), float(m_mouse_y));
    io.AddMouseButtonEvent(imgui_button_index(button), down);
    request_render();
}

void SDLRenderCanvas::on_mouse_wheel(const SDL_Event& event)
{
    if (!m_initialized || !m_render_module->is_initialized())
        return;
    const float wheel_x = event.wheel.preciseX;
    const float wheel_y = event.wheel.preciseY;
    // The wx canvas reports the raw wheel rotation (120 per notch); keep the same unit.
    enqueue_mouse({MouseEvent::Type::Wheel, MouseButton::NoButton, m_mouse_x, m_mouse_y,
                   wheel_x * 120.f, wheel_y * 120.f, modifiers()});
    ImGui::GetIO().AddMouseWheelEvent(wheel_x, wheel_y);
    request_render();
}

void SDLRenderCanvas::on_key(const SDL_Event& event, bool down)
{
    if (!m_initialized || !m_render_module->is_initialized())
        return;
    const SDL_Keycode sym = event.key.keysym.sym;

    ImGuiIO& io = ImGui::GetIO();
    const SDL_Keymod mod = SDL_GetModState();
    io.AddKeyEvent(ImGuiMod_Ctrl, (mod & KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mod & KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mod & KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mod & KMOD_GUI) != 0);
    if (const ImGuiKey key = to_imgui_key(sym); key != ImGuiKey_None)
        io.AddKeyEvent(key, down);

    const KeyCode key_code = to_key_code(sym == SDLK_AC_BACK ? SDLK_ESCAPE : sym);
    if (key_code != KeyCode::None) {
        const KeyboardEvent::Type type = down ? KeyboardEvent::Type::KeyDown : KeyboardEvent::Type::KeyUp;
        update_key_modifiers(type, key_code);
        enqueue_keyboard({type, key_code, modifiers(), down && event.key.repeat != 0});
    }
    request_render();
}

void SDLRenderCanvas::on_text_input(const SDL_Event& event)
{
    if (!m_initialized)
        return;
    ImGui::GetIO().AddInputCharactersUTF8(event.text.text);
    request_render();
}

void SDLRenderCanvas::update_text_input_state()
{
    // Shows / hides the Android soft keyboard when an ImGui text field gains / loses focus.
    const bool want = ImGui::GetIO().WantTextInput;
    if (want == m_text_input_active)
        return;
    m_text_input_active = want;
    if (want)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

void SDLRenderCanvas::on_render_requested()
{
    // The SDL loop wakes up on any event; push one so a blocking SDL_WaitEvent returns.
    SDL_Event event{};
    event.type = SDLMainThreadDispatcher::wakeup_event_type();
    SDL_PushEvent(&event);
}

bool SDLRenderCanvas::begin_frame_platform()
{
    int display_w = 0, display_h = 0;
    SDL_GL_GetDrawableSize(m_window, &display_w, &display_h);
    if (display_w <= 0 || display_h <= 0)
        return false;

    const float scale = dpi_scale();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(display_w / scale, display_h / scale);
    io.DisplayFramebufferScale = ImVec2(scale, scale);

    const float font_size_pt = AppServices::instance().app_config().get<int>("font_size");
    const float font_size_px = font_size_pt * 1.3333f;
    ImGuiStyle& style   = ImGui::GetStyle();
    style.FontScaleDpi  = 1;
    style.FontScaleMain = 1;
    style.FontSizeBase  = font_size_px;

    const int dpi = int(160.f * scale + 0.5f);
    set_screen_size({size_t(display_w), size_t(display_h), scale, dpi, font_size_px, font_size_pt});
    return true;
}

void SDLRenderCanvas::begin_imgui_frame_platform() {}

void SDLRenderCanvas::end_imgui_frame_platform() {}

void SDLRenderCanvas::end_frame_platform()
{
    // Release builds have no glCheck(); report GL errors of the first frames to find ES gaps.
    static int s_checked_frames = 0;
    if (s_checked_frames < 30) {
        ++s_checked_frames;
        for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError())
            SPDLOG_WARN("OpenGL error 0x{:04x} in frame {}", err, s_checked_frames);
    }
    SDL_GL_SwapWindow(m_window);
}

double SDLRenderCanvas::platform_time()
{
    const auto delta = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - m_start_time);
    return double(delta.count()) * 0.000001;
}

Render::Device& SDLRenderCanvas::device()
{
    return Render::Context::instance().device();
}

bool SDLRenderCanvas::is_fullscreen() const
{
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
}

void SDLRenderCanvas::set_fullscreen(bool on)
{
    SDL_SetWindowFullscreen(m_window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void SDLRenderCanvas::close_application()
{
    m_quit_requested = true;
}

} // namespace Slic3r::App::Platform::SDL
