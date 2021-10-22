#include "reaper.hh"
#include <utility>

void reaper::start_frame()
{
    frame_counter++;
}

void reaper::finish_frame()
{
    finish_counter++;
    if(counts.size() != 0 && counts.front().first <= finish_counter)
    {
        size_t count = counts.front().second;
        for(size_t i = 0; i < count; ++i)
            queue[i]();
        queue.erase(queue.begin(), queue.begin()+count);
        counts.pop_front();
    }
}

void reaper::flush()
{
    for(std::function<void()>& entry: queue)
    {
        entry();
    }

    queue.clear();
    counts.clear();
    frame_counter = 0;
    finish_counter = 0;
}

void reaper::at_finish(std::function<void()>&& cleanup)
{
    if(counts.size() == 0 || frame_counter != counts.back().first)
    {
        counts.push_back({frame_counter, 0});
    }
    counts.back().second++;
    queue.emplace_back(std::move(cleanup));
}
