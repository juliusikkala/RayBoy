#ifndef RAYBOY_ECS_HH
#define RAYBOY_ECS_HH
#include "monkeroecs.hh"
#include <chrono>

using namespace monkero;

using time_ticks = int64_t;
using timestamp = int64_t;

struct update
{
    time_ticks delta;
    timestamp at;
};

class ecs_updater: public system, public emitter<update>
{
public:
    ecs_updater();

    void update(ecs& ctx);

private:
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point prev_update;
};

#endif
