#ifndef RAYBOY_TRANSFORMABLE_HH
#define RAYBOY_TRANSFORMABLE_HH
#include "math.hh"
#include "ecs.hh"

class basic_transformable
{
public:
    basic_transformable();
    basic_transformable(const basic_transformable& other);

    void rotate(float angle, vec3 axis, vec3 local_origin = vec3(0));
    void rotate(vec3 axis_magnitude, vec3 local_origin = vec3(0));
    void rotate(float angle, vec2 local_origin = vec2(0));
    void rotate_local(float angle, vec3 axis, vec3 local_origin = vec3(0));

    void rotate(quat rotation);
    void set_orientation(float angle);
    void set_orientation(float angle, vec3 axis);
    void set_orientation(float pitch, float yaw, float roll = 0);
    void set_orientation(quat orientation = quat());
    quat get_orientation() const;
    vec3 get_orientation_euler() const;

    void translate(vec2 offset); 
    void translate(vec3 offset); 
    void translate_local(vec2 offset);
    void translate_local(vec3 offset);
    void set_position(vec2 position);
    void set_position(vec3 position = vec3(0));
    void set_depth(float depth = 0);
    vec3 get_position() const;

    void scale(float scale);
    void scale(vec2 scale);
    void scale(vec3 scale);
    void set_scaling(vec2 size);
    void set_scaling(vec3 size = vec3(1));
    vec2 get_size() const;
    vec3 get_scaling() const;

    void set_transform(const mat4& transform);
    mat4 get_transform() const;

    void lookat(
        vec3 pos,
        vec3 up = vec3(0,1,0),
        vec3 forward = vec3(0,0,-1),
        float angle_limit = -1
    );
    void lookat(
        const basic_transformable* other,
        vec3 up = vec3(0,1,0),
        vec3 forward = vec3(0,0,-1),
        float angle_limit = -1
    );

protected:
    quat orientation;
    vec3 position, scaling;
    mutable uint16_t revision;
};

class transformable_orphan_handler;

class transformable:
    public basic_transformable,
    public ptr_component,
    public dependency_systems<transformable_orphan_handler>
{
public:
    transformable(transformable* parent = nullptr);

    const mat4& get_global_transform() const;

    vec3 get_global_position() const;
    quat get_global_orientation() const;
    vec3 get_global_orientation_euler() const;
    vec3 get_global_scaling() const;

    void set_global_orientation(float angle, vec3 axis);
    void set_global_orientation(float pitch, float yaw, float roll = 0);
    void set_global_orientation(vec3 euler_angles);
    void set_global_orientation(quat orientation = quat());
    void set_global_position(vec3 pos = vec3(0));
    void set_global_scaling(vec3 size = vec3(1));

    void set_parent(
        transformable* parent = nullptr,
        bool keep_transform = false
    );
    transformable* get_parent() const;

    void lookat(
        vec3 pos,
        vec3 up = vec3(0,1,0),
        vec3 forward = vec3(0,0,-1),
        float angle_limit = -1,
        vec3 lock_axis = vec3(0)
    );
    void lookat(
        const transformable* other,
        vec3 up = vec3(0,1,0),
        vec3 forward = vec3(0,0,-1),
        float angle_limit = -1,
        vec3 lock_axis = vec3(0)
    );

    void align_to_view(
        vec3 global_view_dir,
        vec3 global_view_up_dir,
        vec3 up = vec3(0,1,0),
        vec3 lock_axis = vec3(0)
    );
    
    uint16_t update_cached_transform() const;

protected:
    mutable uint16_t cached_revision;
    mutable uint16_t cached_parent_revision;
    transformable* parent;

private:
    mutable mat4 cached_transform;
};

class transformable_orphan_handler:
    public system,
    public receiver<remove_component<transformable>>
{
public:
    void handle(ecs& ctx, const remove_component<transformable>& e);
};

#endif
