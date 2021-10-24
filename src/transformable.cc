#include "transformable.hh"
#define REVISION ++revision;

basic_transformable::basic_transformable()
:   orientation(1,0,0,0), position(0), scaling(1), revision(1)
{
}

basic_transformable::basic_transformable(const basic_transformable& other)
:   orientation(other.orientation), position(other.position),
    scaling(other.scaling), revision(other.revision)
{}

void basic_transformable::rotate(float angle, vec3 axis, vec3 local_origin)
{
    quat rotation = angleAxis(radians(angle), axis);
    orientation = normalize(rotation * orientation);
    position += local_origin + rotation * -local_origin;
    REVISION;
}

void basic_transformable::rotate(vec3 axis_magnitude, vec3 local_origin)
{
    // TODO: Do something smarter than this.
    float l = length(axis_magnitude);
    if(l == 0) return;
    rotate(l*360.0f, axis_magnitude/l, local_origin);
}

void basic_transformable::rotate(float angle, vec2 local_origin)
{
    rotate(angle, vec3(0,0,-1), vec3(local_origin, 0));
}

void basic_transformable::rotate_local(
    float angle,
    vec3 axis,
    vec3 local_origin
){
    axis = orientation * axis;
    rotate(angle, axis, local_origin);
}

void basic_transformable::rotate(quat rotation)
{
    orientation = normalize(rotation * orientation);
    REVISION;
}

void basic_transformable::set_orientation(float angle)
{
    orientation = angleAxis(radians(angle), vec3(0,0,1));
    REVISION;
}

void basic_transformable::set_orientation(float angle, vec3 axis)
{
    orientation = angleAxis(radians(angle), normalize(axis));
    REVISION;
}

void basic_transformable::set_orientation(quat orientation)
{
    this->orientation = orientation;
    REVISION;
}

void basic_transformable::set_orientation(float pitch, float yaw, float roll)
{
    this->orientation = quat(
        vec3(
            radians(pitch),
            radians(yaw),
            radians(roll)
        )
    );
    REVISION;
}

quat basic_transformable::get_orientation() const { return orientation; }
vec3 basic_transformable::get_orientation_euler() const
{ return degrees(eulerAngles(orientation)); }

void basic_transformable::translate(vec2 offset)
{
    this->position.x += offset.x;
    this->position.y += offset.y;
    REVISION;
}

void basic_transformable::translate(vec3 offset)
{
    this->position += offset;
    REVISION;
}

void basic_transformable::translate_local(vec2 offset)
{
    translate_local(vec3(offset, 0));
}

void basic_transformable::translate_local(vec3 offset)
{
    this->position += orientation * offset;
    REVISION;
}

void basic_transformable::set_position(vec2 position)
{
    this->position.x = position.x;
    this->position.y = position.y;
    REVISION;
}

void basic_transformable::set_position(vec3 position)
{
    this->position = position;
    REVISION;
}

void basic_transformable::set_depth(float depth)
{
    this->position.z = depth;
    REVISION;
}

vec3 basic_transformable::get_position() const { return position; }

void basic_transformable::scale(float scale)
{
    this->scaling *= scale;
    REVISION;
}

void basic_transformable::scale(vec2 scale)
{
    this->scaling.x *= scale.x;
    this->scaling.y *= scale.y;
    REVISION;
}

void basic_transformable::scale(vec3 scale)
{
    this->scaling *= scale;
    REVISION;
}

void basic_transformable::set_scaling(vec2 scaling)
{
    this->scaling.x = scaling.x;
    this->scaling.y = scaling.y;
    REVISION;
}

void basic_transformable::set_scaling(vec3 scaling)
{
    this->scaling = scaling;
    REVISION;
}
vec2 basic_transformable::get_size() const { return scaling; }
vec3 basic_transformable::get_scaling() const { return scaling; }

void basic_transformable::set_transform(const mat4& transform)
{
    decompose_matrix(transform, position, scaling, orientation);
    REVISION;
}

mat4 basic_transformable::get_transform() const
{
    mat4 rot = glm::toMat4(orientation);
    return mat4(
        rot[0]*scaling.x,
        rot[1]*scaling.y,
        rot[2]*scaling.z,
        vec4(position, 1)
    );
}

void basic_transformable::lookat(
    vec3 pos,
    vec3 up,
    vec3 forward,
    float angle_limit
){
    vec3 dir = pos - position;
    quat target = quat_lookat(dir, up, forward);

    if(angle_limit < 0) orientation = target;
    else orientation = rotate_towards(orientation, target, angle_limit);
    REVISION;
}

void basic_transformable::lookat(
    const basic_transformable* other,
    vec3 up,
    vec3 forward,
    float angle_limit
){
    lookat(other->position, up, forward, angle_limit);
}

transformable::transformable(transformable* parent)
:   cached_revision(0),
    cached_parent_revision(0),
    parent(parent),
    cached_transform(1)
{}

const mat4& transformable::get_global_transform() const 
{
    update_cached_transform();
    return cached_transform;
}

vec3 transformable::get_global_position() const
{
    return get_matrix_translation(get_global_transform());
}

quat transformable::get_global_orientation() const
{
    return get_matrix_orientation(get_global_transform());
}

vec3 transformable::get_global_orientation_euler() const
{
    return degrees(eulerAngles(get_global_orientation()));
}

vec3 transformable::get_global_scaling() const
{
    return get_matrix_scaling(get_global_transform());
}

void transformable::set_global_orientation(float angle, vec3 axis)
{
    set_global_orientation(angleAxis(radians(angle), normalize(axis)));
}

void transformable::set_global_orientation(
    float pitch, float yaw, float roll
){
    set_global_orientation(vec3(pitch, yaw, roll));
}

void transformable::set_global_orientation(vec3 euler_angles)
{
    set_global_orientation(quat(radians(euler_angles)));
}

void transformable::set_global_orientation(quat orientation)
{
    if(parent)
        orientation = inverse(parent->get_global_orientation()) * orientation;
    this->orientation = orientation;
    REVISION;
}

void transformable::set_global_position(vec3 pos)
{
    position = pos;
    if(parent)
        position = vec3(
            affineInverse(parent->get_global_transform()) * vec4(pos, 1)
        );
    else position = pos;
    REVISION;
}

void transformable::set_global_scaling(vec3 size)
{
    scaling = size;
    if(parent) scaling /= parent->get_global_scaling();
    REVISION;
}

void transformable::set_parent(
    transformable* parent,
    bool keep_transform
){
    if(keep_transform)
    {
        mat4 transform = get_global_transform();
        if(parent)
            transform =
                affineInverse(parent->get_global_transform()) * transform;
        decompose_matrix(transform, position, scaling, orientation);
    }
    this->parent = parent;
    REVISION;
    // Setting cached_parent_revision is unnecessary, since the changed revision
    // should already force cache invalidation.
}

transformable* transformable::get_parent() const
{
    return parent;
}

void transformable::lookat(
    vec3 pos,
    vec3 up,
    vec3 forward,
    float angle_limit,
    vec3 lock_axis
){
    vec3 eye = get_global_position();
    vec3 dir = pos - eye;

    if(lock_axis != vec3(0))
    {
        dir -= lock_axis*dot(dir, lock_axis);
        dir = normalize(dir);
    }

    quat global_orientation = quat_lookat(dir, up, forward);
    quat target = global_orientation;

    if(parent)
        target = inverse(parent->get_global_orientation()) * target;

    if(angle_limit < 0) orientation = target;
    else orientation = rotate_towards(orientation, target, angle_limit);
    REVISION;
}

void transformable::lookat(
    const transformable* other,
    vec3 up,
    vec3 forward,
    float angle_limit,
    vec3 lock_axis
){
    lookat(other->get_global_position(), up, forward, angle_limit, lock_axis);
}

void transformable::align_to_view(
    vec3 global_view_dir,
    vec3 global_view_up_dir,
    vec3 up,
    vec3 lock_axis
){
    if(lock_axis != vec3(0))
    {
        // This works by projecting global_view_dir to lock_axis, then removing
        // the "contribution" by that axis from global_view_dir.
        global_view_dir -= lock_axis*dot(global_view_dir, lock_axis);
        global_view_dir = normalize(global_view_dir);
    }

    if(fabs(dot(global_view_dir, up)) > 0.999f)
        up = global_view_up_dir;

    vec3 face_axis = vec3(0,0,1);
    if(parent)
    { // If there is a parent, transform the face axis into world space.
        mat3 norm_mat = glm::inverseTranspose(
            mat3(parent->get_global_transform())
        );
        face_axis = norm_mat * face_axis;
    }

    set_orientation(quat_lookat(global_view_dir, up, -face_axis));
}

uint16_t transformable::update_cached_transform() const
{
    if(parent)
    {
        uint16_t parent_revision = parent->update_cached_transform();
        if(
            cached_revision != revision ||
            cached_parent_revision != parent_revision
        ){
            cached_transform = parent->cached_transform * get_transform();
            cached_parent_revision = parent_revision;
            // Local revision must change if parent transform has changed. This
            // ensures that further children update properly.
            cached_revision = ++revision;
        }
    }
    else
    {
        if(cached_revision != revision)
        {
            cached_transform = get_transform();
            cached_revision = ++revision;
        }
    }
    return revision;
}

void transformable_orphan_handler::handle(
    ecs& ctx, const remove_component<transformable>& e
){
    // If the parent has been removed, the children are removed too.
    ctx([&](entity id, transformable& t){
        if(t.get_parent() == e.data)
            ctx.remove(id);
    });
}
