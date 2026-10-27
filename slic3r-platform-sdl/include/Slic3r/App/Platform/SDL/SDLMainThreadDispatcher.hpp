#pragma once

#include <Slic3r/App/Platform/StdMainThreadDispatcher.hpp>

#include <cstdint>

namespace Slic3r::App::Platform::SDL {

class SDLMainThreadDispatcher : public StdMainThreadDispatcher
{
public:
    /// SDL user event pushed to wake the event loop when work is queued.
    static uint32_t wakeup_event_type();

private:
    void on_queue_insert() override;
};

} // namespace Slic3r::App::Platform::SDL
