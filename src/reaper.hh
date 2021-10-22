#ifndef RAYBOY_REAPER_HH
#define RAYBOY_REAPER_HH

#include "volk.h"
#include <functional>
#include <deque>

// Handles deallocating resources once they're no longer used in any in-flight
// frame.
class reaper
{
public:
    void start_frame();
    void finish_frame();
    void flush();

    void at_finish(std::function<void()>&& cleanup);

private:
    std::deque<std::function<void()>> queue;
    std::deque<std::pair<uint64_t, size_t>> counts;
    uint64_t frame_counter = 0;
    uint64_t finish_counter = 0;
};

#endif
