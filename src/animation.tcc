#ifndef ICROP_ANIMATION_TCC
#define ICROP_ANIMATION_TCC
#include <algorithm>

template<typename T>
T animation::interpolate(
    time_ticks time,
    const std::vector<sample<T>>& data,
    interpolation interp
) const
{
    auto it = std::upper_bound(
        data.begin(), data.end(), time,
        [](timestamp time, const sample<T>& s){ return time < s.timestamp; }
    );
    if(it == data.end()) return data.back().data;
    if(it == data.begin()) return data.front().data;

    auto prev = it-1;
    float frame_ticks = it->timestamp-prev->timestamp;
    float ratio = (time-prev->timestamp)/frame_ticks;
    switch(interp)
    {
    default:
    case LINEAR:
        return numeric_mixer<T>()(prev->data, it->data, ratio);
    case STEP:
        return prev->data;
    case CUBICSPLINE:
        {
            // Scale factor has to use seconds unfortunately.
            float scale = frame_ticks * 0.000001f;
            return cubic_spline(
                prev->data,
                prev->out_tangent*scale,
                it->data,
                it->in_tangent*scale,
                ratio
            );
        }
    }
}

template<typename Derived>
animation_controller<Derived>::animation_controller()
: timer(0), loop_time(0), paused(false)
{
}

template<typename Derived>
animation_controller<Derived>& animation_controller<Derived>::queue(
    const std::string& name, bool loop
){
    animation_queue.push_back({name, loop});
    if(animation_queue.size() == 1)
    {
        timer = 0;
        loop_time = static_cast<Derived*>(this)->set_animation(name);
    }
    return *this;
}

template<typename Derived>
void animation_controller<Derived>::play(const std::string& name, bool loop)
{
    animation_queue.clear();
    animation_queue.push_back({name, loop});
    timer = 0;
    loop_time = static_cast<Derived*>(this)->set_animation(name);
}

template<typename Derived>
void animation_controller<Derived>::pause(bool paused)
{
    this->paused = paused;
}

template<typename Derived>
bool animation_controller<Derived>::is_playing() const
{
    return !animation_queue.empty() && !paused;
}

template<typename Derived>
bool animation_controller<Derived>::is_paused() const
{
    return this->paused;
}

template<typename Derived>
void animation_controller<Derived>::finish()
{
    if(animation_queue.size() != 0)
    {
        animation_queue.resize(1);
        animation_queue.front().loop = false;
    }
}

template<typename Derived>
void animation_controller<Derived>::stop()
{
    animation_queue.clear();
    timer = 0;
    loop_time = 0;
}

template<typename Derived>
const std::string&
animation_controller<Derived>::get_playing_animation_name() const
{
    static const std::string empty_dummy("");
    if(animation_queue.size() == 0) return empty_dummy;
    return animation_queue.front().name;
}

template<typename Derived>
time_ticks animation_controller<Derived>::get_animation_time() const
{
    return timer;
}

template<typename Derived>
void animation_controller<Derived>::update(time_ticks dt)
{
    if(!is_playing()) return;

    timer += dt;

    animation_step& cur_step = animation_queue.front();

    // If we have a waiting animation, check if the timer rolled over the
    // looping point.
    if(animation_queue.size() > 1)
    {
        if(timer >= loop_time)
        {
            timer -= loop_time;
            animation_queue.erase(animation_queue.begin());
            loop_time = static_cast<Derived*>(this)->set_animation(
                animation_queue.front().name
            );
        }
    }
    // If there's nothing waiting, then keep looping.
    else if(cur_step.loop)
        timer %= loop_time;
    // If we're past the end of a non-looping animation, stop animating.
    else if(timer >= loop_time)
    {
        animation_queue.erase(animation_queue.begin());
        loop_time = 0;
        timer = 0;
        return;
    }

    static_cast<Derived*>(this)->apply_animation(timer);
}

#endif
