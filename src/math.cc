#include "math.hh"
#include <sstream>

namespace
{

vec2 circle_projection_range(vec2 dir, float r, float p, float big)
{
    float d2 = dot(dir, dir);
    float r2 = r * r;

    if(d2 <= r2) { return vec2(-1, 1) * big; }

    float len = sqrt(d2 - r2);
    vec2 n = dir / dir.y;

    vec2 h(-n.y, n.x);
    h *= r / len;

    vec2 up = n + h;
    float top = up.x / fabs(up.y) * p;

    vec2 down = n - h;
    float bottom = down.x / fabs(down.y) * p;

    if(dir.x > 0 && dir.y <= r)
    {
        bottom = big;
        if(dir.y <= 0) top = -top;
    }

    if(dir.x < 0 && dir.y <= r)
    {
        top = -big;
        if(dir.y <= 0) bottom = -bottom;
    }

    return vec2(top, bottom);
}

}

bool point_in_rect(vec2 p, vec2 o, vec2 sz)
{
    return o.x <= p.x && p.x <= o.x + sz.x && o.y <= p.y && p.y <= o.y + sz.y;
}

bool point_in_triangle(vec2 a, vec2 b, vec2 c, vec2 p)
{
    vec3 bc = barycentric(a, b, c, p);
    return bc.x >= 0 && bc.y >= 0 && bc.x + bc.y <= 1;
}

vec3 barycentric(vec2 a, vec2 b, vec2 c, vec2 p)
{
    float inv_area = 0.5f/signed_area(a, b, c);
    float s = (a.y*c.x - a.x*c.y - p.x*(a.y - c.y) + p.y*(a.x - c.x))*inv_area;
    float t = (a.x*b.y - a.y*b.x + p.x*(a.y - b.y) - p.y*(a.x - b.x))*inv_area;
    float u = 1 - s - t;
    return vec3(s, t, u);
}

float signed_area(vec2 a, vec2 b, vec2 c)
{
    return 0.5f * (
        a.x*b.y - b.x*a.y +
        b.x*c.y - c.x*b.y +
        c.x*a.y - a.x*c.y
    );
}

vec3 hsv_to_rgb(vec3 hsv)
{
    vec3 c = vec3(5,3,1) + hsv.x/60.0f;
    vec3 k = vec3(fmod(c.x, 6.0f), fmod(c.y, 6.0f), fmod(c.z, 6.0f));
    return hsv.z - hsv.z*hsv.y*clamp(min(k, 4.0f-k), vec3(0.0f), vec3(1.0f));
}

float circle_sequence(unsigned n)
{
    unsigned denom = n + 1;
    denom--;
    denom |= denom >> 1;
    denom |= denom >> 2;
    denom |= denom >> 4;
    denom |= denom >> 8;
    denom |= denom >> 16;
    denom++;
    unsigned num = 1 + (n - denom/2)*2;
    return num/(float)denom;
}

vec3 generate_color(int32_t index, float saturation, float value)
{
    return hsv_to_rgb(vec3(
        360*circle_sequence(index),
        saturation,
        value
    ));
}

void decompose_matrix(
    const glm::mat4& transform,
    glm::vec3& translation,
    glm::vec3& scaling,
    glm::quat& orientation
){
    translation = transform[3];
    scaling = glm::vec3(
        glm::length(transform[0]),
        glm::length(transform[1]),
        glm::length(transform[2])
    );
    orientation = glm::quat(glm::mat4(
        transform[0]/scaling.x,
        transform[1]/scaling.y,
        transform[2]/scaling.z,
        glm::vec4(0,0,0,1)
    ));
}

glm::vec3 get_matrix_translation(const glm::mat4& transform)
{
    return transform[3];
}

glm::vec3 get_matrix_scaling(const glm::mat4& transform)
{
    return glm::vec3(
        glm::length(transform[0]),
        glm::length(transform[1]),
        glm::length(transform[2])
    );
}

glm::quat get_matrix_orientation(const glm::mat4& transform)
{
    return glm::quat(glm::mat4(
        glm::normalize(transform[0]),
        glm::normalize(transform[1]),
        glm::normalize(transform[2]),
        glm::vec4(0,0,0,1)
    ));
}

glm::quat rotate_towards(
    glm::quat orig,
    glm::quat dest,
    float angle_limit
){
    angle_limit = glm::radians(angle_limit);

    float cos_theta = dot(orig, dest);
    if(cos_theta > 0.999999f)
    {
        return dest;
    }

    if(cos_theta < 0)
    {
        orig = orig * -1.0f;
        cos_theta *= -1.0f;
    }

    float theta = acos(cos_theta);
    if(theta < angle_limit) return dest;
    return glm::mix(orig, dest, angle_limit/theta);
}

glm::quat quat_lookat(
    glm::vec3 dir,
    glm::vec3 up,
    glm::vec3 forward
){
    dir = glm::normalize(dir);
    up = glm::normalize(up);
    forward = glm::normalize(forward);

    glm::quat towards = glm::rotation(
        forward,
        glm::vec3(0,0,-1)
    );
    return glm::quatLookAt(dir, up) * towards;
}

bool solve_quadratic(float a, float b, float c, float& x0, float& x1)
{
    float D = b * b - 4 * a * c;
    float sD = sqrt(D) * sign(a);
    float denom = -0.5f/a;
    x0 = (b + sD) * denom;
    x1 = (b - sD) * denom;
    return !std::isnan(sD);
}

void solve_cubic_roots(
    double a, double b, double c, double d,
    std::complex<double>& r1,
    std::complex<double>& r2,
    std::complex<double>& r3
){
    double d1 = 2*b*b*b - 9*a*b*c + 27*a*a*d;
    double d2 = b*b - 3*a*c;
    auto d3 = sqrt(std::complex<double>(d1*d1 - 4*d2*d2*d2));

    double k = 1/(3*a);

    auto p1 = std::pow(0.5*(d1+d3), 1/3.0f);
    auto p2 = std::pow(0.5*(d1-d3), 1/3.0f);

    std::complex<double> c1(0.5, 0.5*sqrt(3));
    std::complex<double> c2(0.5, -0.5*sqrt(3));

    r1 = k*(-b - p1 - p2).real();
    r2 = k*(-b + c1*p1 + c2*p2);
    r3 = k*(-b + c2*p1 + c1*p2);
}

double cubic_bezier(dvec2 p1, dvec2 p2, double t)
{
    // x = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
    //   = (3*P1 - 3*P2 + 1)*t^3 + (-6*P1 + 3*P2)*t^2 + (3*P1)*t
    //   when P0=(0,0) and P3=(1,1)

    std::complex<double> r1;
    std::complex<double> r2;
    std::complex<double> r3;
    solve_cubic_roots(
        3.*p1.x-3.*p2.x+1., 3.*p2.x-6.*p1.x, 3.*p1.x, -t,
        r1, r2, r3
    );

    double xt = r1.real();
    double best = 0;
    if(r1.real() < 0) best = -r1.real();
    else if(r1.real() > 1) best = r1.real()-1;

    if(abs(r2.imag()) < 0.00001)
    {
        double cost = 0;
        if(r2.real() < 0) cost = -r2.real();
        else if(r2.real() > 1) cost = r2.real()-1;
        if(cost < best)
        {
            best = cost;
            xt = r2.real();
        }
    }

    if(abs(r3.imag()) < 0.00001)
    {
        double cost = 0;
        if(r3.real() < 0) cost = -r3.real();
        else if(r3.real() > 1) cost = r3.real()-1;
        if(cost < best)
        {
            best = cost;
            xt = r3.real();
        }
    }

    return (3.*p1.y-3.*p2.y+1.)*xt*xt*xt
        + (3.*p2.y-6.*p1.y)*xt*xt
        + (3.*p1.y)*xt;
}

bool intersect_sphere(
    vec3 pos,
    vec3 dir,
    vec3 origin,
    float radius,
    float& t0,
    float& t1
){
    vec3 L = pos - origin;
    float a = dot(dir, dir);
    float b = 2*dot(dir, L);
    float c = dot(L, L) - radius * radius;

    if(!solve_quadratic(a, b, c, t0, t1)) return false;
    if(t1 < 0) return false;
    if(t0 < 0) t0 = 0;

    return true;
}

unsigned next_power_of_two(unsigned n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n++;
    return n;
}

unsigned factorize(unsigned n)
{
    // Divisible by two
    if((n&1)==0) return 2;

    unsigned last = floor(sqrt(n));
    for(unsigned i = 3; i <= last; ++i)
        if((n % i) == 0) return i;

    return 0;
}

mat4 sphere_projection_quad_matrix(
    vec3 pos,
    float r,
    float near,
    float far,
    bool use_near_radius,
    float big
){
    float d = -pos.z;

    if(use_near_radius) d = glm::max(d - r, near);
    else d = glm::min(d + r, far);

    vec2 w = circle_projection_range(vec2(pos.x, -pos.z), r, d, big);
    vec2 h = circle_projection_range(vec2(pos.y, -pos.z), r, d, big);

    glm::vec2 center = glm::vec2(w.x + w.y, h.x + h.y) / 2.0f;
    glm::vec2 scale = glm::vec2(fabs(w.y - w.x), fabs(h.y - h.x)) / 2.0f;

    return glm::translate(vec3(center, -d)) * glm::scale(vec3(scale, 0));
}

template<typename T, class F>
void mitchell_best_candidate(
    std::vector<T>& samples,
    F&& sample_generator,
    unsigned candidate_count,
    unsigned count
){
    if(count < samples.size()) return;

    samples.reserve(count);
    count -= samples.size();

    while(count--)
    {
        T farthest = T(0);
        float farthest_distance = 0;

        for(unsigned i = 0; i < candidate_count; ++i)
        {
            T candidate = sample_generator();
            float candidate_distance = INFINITY;

            for(const T& sample: samples)
            {
                float dist = glm::distance(candidate, sample);
                if(dist < candidate_distance) candidate_distance = dist;
            }

            if(candidate_distance > farthest_distance)
            {
                farthest_distance = candidate_distance;
                farthest = candidate;
            }
        }

        samples.push_back(farthest);
    }
}

void mitchell_best_candidate(
    std::vector<vec2>& samples,
    float r,
    unsigned candidate_count,
    unsigned count
){
    mitchell_best_candidate(
        samples,
        [r](){return glm::diskRand(r);},
        candidate_count,
        count
    );
}

void mitchell_best_candidate(
    std::vector<vec2>& samples,
    float w,
    float h,
    unsigned candidate_count,
    unsigned count
){
    mitchell_best_candidate(
        samples,
        [w, h](){
            return glm::linearRand(glm::vec2(-w/2, -h/2), glm::vec2(w/2, h/2));
        },
        candidate_count,
        count
    );
}

void mitchell_best_candidate(
    std::vector<vec3>& samples,
    float r,
    unsigned candidate_count,
    unsigned count
){
    mitchell_best_candidate(
        samples,
        [r](){return glm::ballRand(r);},
        candidate_count,
        count
    );
}

std::vector<vec2> grid_samples(
    unsigned w,
    unsigned h,
    float step
){
    std::vector<vec2> samples;
    samples.resize(w*h);

    glm::vec2 start(
        (w-1)/-2.0f,
        (h-1)/-2.0f
    );

    for(unsigned i = 0; i < h; ++i)
        for(unsigned j = 0; j < w; ++j)
            samples[i*w+j] = start + vec2(i, j) * step;

    return samples;
}

std::vector<float> generate_gaussian_kernel(
    int radius,
    float sigma
){
    std::vector<float> result;
    result.reserve(radius * 2 + 1);

    for(int i = -radius; i <= radius; ++i)
    {
        float f = i/sigma;
        float weight = exp(-f*f/2.0f)/(sigma * sqrt(2*M_PI));
        result.push_back(weight);
    }
    return result;
}

vec3 pitch_yaw_to_vec(float pitch, double yaw)
{
    pitch = glm::radians(pitch);
    yaw = glm::radians(yaw);
    float c = cos(pitch);
    return vec3(c * cos(yaw), sin(pitch), c * sin(-yaw));
}

uvec2 string_to_resolution(const std::string& str)
{
    std::stringstream ss(str);
    uvec2 res;
    ss >> res.x;
    ss.ignore(1);
    ss >> res.y;
    if(!ss) return uvec2(640, 360);
    return res;
}

unsigned calculate_mipmap_count(uvec2 size)
{
    return (unsigned)std::floor(std::log2(std::max(size.x, size.y)))+1u;
}

unsigned ravel_tex_coord(uvec3 p, uvec3 size)
{
    return p.z * size.x * size.y + p.y * size.x + p.x;
}

ray operator*(const mat4& mat, const ray& r)
{
    ray res;
    res.o = mat * vec4(r.o, 1.0f);
    res.dir = inverseTranspose(mat) * vec4(r.dir, 0);
    return res;
}

bool flipped_winding_order(const mat3& transform)
{
    return determinant(transform) < 0;
}

