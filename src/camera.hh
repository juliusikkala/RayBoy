#ifndef RAYBOY_CAMERA_HH
#define RAYBOY_CAMERA_HH
#include "transformable.hh"
#include "animation.hh"

class camera: public dependency_components<transformable>
{
public:
    void perspective(float fov, float aspect, float near, float far = INFINITY);
    void ortho(float aspect);
    void ortho(float aspect, float near, float far);
    void ortho(float left, float right, float bottom, float top);
    void ortho(
        float left, float right, float bottom, float top,
        float near, float far
    );
    mat4 get_projection() const;
    vec3 get_clip_info() const;
    vec2 get_projection_info() const;
    vec2 get_pixels_per_unit(uvec2 target_size) const;
    struct frustum get_frustum() const;

    float get_near() const;
    float get_far() const;
    vec2 get_range() const;

    void set_aspect(float aspect);
    float get_aspect() const;

    void set_fov(float fov);
    float get_fov() const;

    // vec2(0, 0) is bottom left, vec2(1, 1) is top right. The rays are in view
    // space. The ray length is such that (origin + direction).z == far.
    // near_mul can be used to adjust the starting point of the ray, 1.0f starts
    // from near plane, 0.0f starts from camera.
    ray get_view_ray(vec2 uv, float near_mul = 1.0f) const;

private:
    void update_projection();

    mat4 projection;
    float fov;
    float near;
    float far;
    float aspect;
    struct frustum f;

    vec3 clip_info;
    vec2 projection_info;
};

#endif
