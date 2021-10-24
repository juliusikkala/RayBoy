#include "light.hh"

light::light(vec3 color, float radius)
: color(color), radius(radius) {}

void light::set_color(vec3 color)
{
    this->color = color;
}

vec3 light::get_color() const
{
    return color;
}

void light::set_radius(float radius)
{
    this->radius = radius;
}

float light::get_radius() const
{
    return radius;
}

directional_light::directional_light(vec3 color)
: light(color)
{
}

point_light::point_light(vec3 color, float cutoff_brightness)
: light(color), cutoff_brightness(cutoff_brightness) {}

void point_light::set_cutoff_brightness(float cutoff_brightness)
{
    this->cutoff_brightness = cutoff_brightness;
}

float point_light::get_cutoff_brightness() const
{
    return cutoff_brightness;
}

void point_light::set_cutoff_radius(float cutoff_radius)
{
    vec3 c = get_color();
    cutoff_brightness = max(max(c.x, c.y), c.z)/(cutoff_radius*cutoff_radius);
}

float point_light::get_cutoff_radius() const
{
    vec3 radius2 = get_color()/cutoff_brightness;
    return sqrt(max(max(radius2.x, radius2.y), radius2.z));
}

spotlight::spotlight(
    vec3 color,
    float cutoff_angle,
    float falloff_exponent
):  point_light(color), cutoff_angle(cutoff_angle),
    falloff_exponent(falloff_exponent)
{
}

void spotlight::set_cutoff_angle(float cutoff_angle)
{
    this->cutoff_angle = cutoff_angle;
}

float spotlight::get_cutoff_angle() const
{
    return cutoff_angle;
}

void spotlight::set_falloff_exponent(float falloff_exponent)
{
    this->falloff_exponent = falloff_exponent;
}

float spotlight::get_falloff_exponent() const
{
    return falloff_exponent;
}

void spotlight::set_inner_angle(float inner_angle, float ratio)
{
    if(inner_angle <= 0) falloff_exponent = 1.0f;
    else
    {
        float inner = cos(glm::radians(inner_angle));
        float outer = cos(glm::radians(cutoff_angle));
        falloff_exponent = log(ratio)/log(max(1.0f-inner, 0.0f)/(1.0f-outer));
    }
}
