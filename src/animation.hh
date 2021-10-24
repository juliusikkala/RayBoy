#ifndef RAYBOY_ANIMATION_HH
#define RAYBOY_ANIMATION_HH
#include "transformable.hh"
#include "ecs.hh"
#include <vector>
#include <unordered_map>

namespace easing
{
    inline double linear(double t)
    {
        return t;
    }
    inline double ease(double t)
    {
        return cubic_bezier({0.25, 0.1}, {0.25, 1.0}, t);
    }
    inline double ease_in(double t)
    {
        return cubic_bezier({0.42, 0.0}, {1.0, 1.0}, t);
    }
    inline double ease_out(double t)
    {
        return cubic_bezier({0.0, 0.0}, {0.58, 1.0}, t);
    }
    inline double ease_in_out(double t)
    {
        return cubic_bezier({0.42, 0.0}, {0.58, 1.0}, t);
    }
}

// You can define custom mixers for your time if the required operators
// aren't available or don't work. They should have the exact same function
// as this, just linearly mix between the begin and end values. Easing is
// done by a separate easing function that is applied to 't' before passing
// it to this.
template<typename T>
struct numeric_mixer
{
    T operator()(const T& begin, const T& end, double t) const
    {
        return begin * (1.0 - t) + end * t;
    }
};

template<glm::length_t L, typename T, glm::qualifier Q>
struct numeric_mixer<glm::vec<L, T, Q>>
{
    using V = glm::vec<L, T, Q>;
    V operator()(const V& begin, const V& end, double t) const
    {
        return begin * T(1.0 - t) + end * T(t);
    }
};

template<typename T, glm::qualifier Q>
struct numeric_mixer<glm::qua<T, Q>>
{
    using V = glm::qua<T, Q>;
    V operator()(const V& begin, const V& end, double t) const
    {
        return glm::slerp(begin, end, T(t));
    }
};

template<typename T, typename M = numeric_mixer<T>>
class animated_variable
{
public:
    using easing_func_type = double(*)(double);

    animated_variable(
        easing_func_type easing_func = easing::linear
    ): easing_func(easing_func), duration(0), time(0)
    {
    }

    animated_variable(
        T initial_value,
        easing_func_type easing_func = easing::linear
    ):  animated_variable(easing_func)
    {
        begin_value = initial_value;
        end_value = initial_value;
    }

    void transition(T begin_value, T end_value, time_ticks duration)
    {
        this->begin_value = begin_value;
        this->end_value = end_value;
        time = 0;
        this->duration = duration;
    }

    void transition(
        T begin_value,
        T end_value,
        time_ticks duration,
        easing_func_type easing_func
    ){
        transition(begin_value, end_value, duration);
        this->easing_func = easing_func;
    }

    void to(T target_value, time_ticks duration)
    {
        begin_value = M()(begin_value, end_value, easing_func(1.0));
        end_value = target_value;
        time = 0;
        this->duration = duration;
    }

    void to(
        T target_value,
        time_ticks duration,
        easing_func_type easing_func
    ){
        to(target_value, duration);
        this->easing_func = easing_func;
    }

    void smooth(T target_value, time_ticks duration)
    {
        begin_value = **this;
        end_value = target_value;
        time = 0;
        this->duration = duration;
    }

    void smooth(
        T target_value,
        time_ticks duration,
        easing_func_type easing_func
    ){
        smooth(target_value, duration);
        this->easing_func = easing_func;
    }

    void set(T target_value)
    {
        begin_value = end_value = target_value;
        duration = time = 0;
    }

    T get_begin() const { return begin_value; }
    T get_end() const { return end_value; }

    void update(time_ticks dt) { time = min(time + dt, duration); }

    double ratio() const { return easing_func(progress()); }

    double progress() const
    {
        if(duration == 0) return 1.0;
        return (double)time/(double)duration;
    }

    bool finished() const
    {
        return duration == 0 || time >= duration;
    }

    operator T() const { return **this; }
    T operator*() const
    {
        return M()(begin_value, end_value, ratio());
    }

private:
    T begin_value;
    T end_value;

    easing_func_type easing_func;

    time_ticks duration;
    time_ticks time;
};


class animation
{
public:
    template<typename T>
    struct sample
    {
        time_ticks timestamp;
        T data;
        // These are only used if the interpolation is CUBICSPLINE.
        T in_tangent;
        T out_tangent;
    };

    enum interpolation
    {
        LINEAR = 0,
        STEP,
        CUBICSPLINE
    };

    animation();

    void set_position(
        interpolation position_interpolation,
        std::vector<sample<vec3>>&& position
    );

    void set_scaling(
        interpolation scaling_interpolation,
        std::vector<sample<vec3>>&& scaling
    );

    void set_orientation(
        interpolation orientation_interpolation,
        std::vector<sample<quat>>&& orientation
    );

    void apply(transformable& node, time_ticks time) const;
    time_ticks get_loop_time() const;

private:
    void determine_loop_time();

    template<typename T>
    T interpolate(
        time_ticks time,
        const std::vector<sample<T>>& data,
        interpolation interp
    ) const;

    time_ticks loop_time;
    interpolation position_interpolation;
    std::vector<sample<vec3>> position;
    interpolation scaling_interpolation;
    std::vector<sample<vec3>> scaling;
    interpolation orientation_interpolation;
    std::vector<sample<quat>> orientation;
}; 

using animation_pool = std::unordered_map<std::string /*name*/, animation>;

// You can use this to provide the animation functions to your class, as long as
// you implement the following member functions:
//   time_ticks set_animation(const std::string& name);
//   void apply_animation(time_ticks time);
template<typename Derived>
class animation_controller
{
public:
    animation_controller();

    // Starts playing the queued animation at the next loop point, or
    // immediately if there are no playing animations. Returns a reference to
    // this object for chaining purposes.
    animation_controller& queue(const std::string& name, bool loop = false);
    void play(const std::string& name, bool loop = false);
    void pause(bool paused = true);
    // The animation can be unpaused and still not play simply when there is
    // no animation in the queue left to play.
    bool is_playing() const;
    bool is_paused() const;
    // Drops queued animations and ends the looping of the current animation.
    void finish();
    // Drops queued animations and instantly stops current animation as well.
    void stop();
    const std::string& get_playing_animation_name() const;
    time_ticks get_animation_time() const;

    void update(time_ticks dt);

private:
    struct animation_step
    {
        std::string name;
        bool loop;
    };
    std::vector<animation_step> animation_queue;
    time_ticks timer;
    time_ticks loop_time;
    bool paused;
};

class animation_updater;

struct animated:
    public animation_controller<animated>,
    public dependency_components<transformable>,
    public dependency_systems<animation_updater>
{
friend class animation_controller<animated>;
public:
    animated(const animation_pool* pool = nullptr);

    const animation_pool* pool;
    const animation* cur_anim;

protected:
    time_ticks set_animation(const std::string& name);
    void apply_animation(time_ticks time);
};

// Animation controls
void queue_animation(
    ecs& ctx, entity id, const std::string& name, bool loop = false
);
void play_animation(
    ecs& ctx, entity id, const std::string& name, bool loop = false
);
void pause_animation(ecs& ctx, entity id, bool paused);
void finish_animation(ecs& ctx, entity id);

class animation_updater: public system, public receiver<update>
{
public:
    void handle(ecs& ctx, const update& e) override;
};

#include "animation.tcc"
#endif
