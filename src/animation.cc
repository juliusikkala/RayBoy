#include "animation.hh"

animation::animation()
:   loop_time(0), position_interpolation(LINEAR),
    scaling_interpolation(LINEAR), orientation_interpolation(LINEAR)
{
}

void animation::set_position(
    interpolation position_interpolation,
    std::vector<sample<vec3>>&& position
){
    this->position_interpolation = position_interpolation;
    this->position = std::move(position);
    determine_loop_time();
}

void animation::set_scaling(
    interpolation scaling_interpolation,
    std::vector<sample<vec3>>&& scaling
){
    this->scaling_interpolation = scaling_interpolation;
    this->scaling = std::move(scaling);
    determine_loop_time();
}

void animation::set_orientation(
    interpolation orientation_interpolation,
    std::vector<sample<quat>>&& orientation
){
    this->orientation_interpolation = orientation_interpolation;
    this->orientation = std::move(orientation);
    determine_loop_time();
}

void animation::apply(transformable& node, time_ticks time) const
{
    if(position.size())
        node.set_position(interpolate(time, position, position_interpolation));
    if(scaling.size())
        node.set_scaling(interpolate(time, scaling, scaling_interpolation));
    if(orientation.size())
    {
        quat o = interpolate(time, orientation, orientation_interpolation);
        if(orientation_interpolation == CUBICSPLINE)
            o = normalize(o);
        node.set_orientation(o);
    }
}

time_ticks animation::get_loop_time() const
{
    return loop_time;
}

void animation::determine_loop_time()
{
    loop_time = 0;
    if(position.size())
        loop_time = std::max(position.back().timestamp, loop_time);
    if(scaling.size())
        loop_time = std::max(scaling.back().timestamp, loop_time);
    if(orientation.size())
        loop_time = std::max(orientation.back().timestamp, loop_time);
}

animated::animated(const animation_pool* pool)
: pool(pool), cur_anim(nullptr)
{
}

time_ticks animated::set_animation(const std::string& name)
{
    time_ticks loop_time = 0;
    if(pool)
    {
        auto it = pool->find(name);
        if(it != pool->end())
        {
            cur_anim = &it->second;
            loop_time = max(loop_time, cur_anim->get_loop_time());
        }
    }

    return loop_time;
}

void animated::apply_animation(time_ticks)
{
    // Do nothing here, we apply the animation manually in animation_updater.
}

void queue_animation(ecs& ctx, entity id, const std::string& name, bool loop)
{
    animated* a = ctx.get<animated>(id);
    if(a)
    {
        a->queue(name, loop);
    }
}

void play_animation(ecs& ctx, entity id, const std::string& name, bool loop)
{
    animated* a = ctx.get<animated>(id);
    if(a)
    {
        a->play(name, loop);
    }
}

void pause_animation(ecs& ctx, entity id, bool paused)
{
    animated* a = ctx.get<animated>(id);
    if(a) a->pause(paused);
}

void finish_animation(ecs& ctx, entity id)
{
    animated* a = ctx.get<animated>(id);
    if(a) a->finish();
}

void animation_updater::handle(ecs& ctx, const update& e)
{
    ctx([&](transformable& t, animated& a){
        a.update(e.delta);
        if(a.is_playing())
        {
            if(a.cur_anim) a.cur_anim->apply(t, a.get_animation_time());
        }
    });
}
