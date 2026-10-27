#include "Slic3r/App/Platform/SDL/SDLMainThreadDispatcher.hpp"

#include <SDL.h>

namespace Slic3r::App::Platform::SDL {

uint32_t SDLMainThreadDispatcher::wakeup_event_type()
{
    static const uint32_t type = SDL_RegisterEvents(1);
    return type;
}

void SDLMainThreadDispatcher::on_queue_insert()
{
    SDL_Event event{};
    event.type = wakeup_event_type();
    SDL_PushEvent(&event);
}

} // namespace Slic3r::App::Platform::SDL
