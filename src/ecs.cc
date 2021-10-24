#include "ecs.hh"

ecs_updater::ecs_updater()
{
    start = std::chrono::steady_clock::now();
    prev_update = start;
}

void ecs_updater::update(ecs& ctx)
{
    auto now = std::chrono::steady_clock::now();
    struct update u = {
        std::chrono::duration_cast<std::chrono::microseconds>(now-prev_update).count(),
        std::chrono::duration_cast<std::chrono::microseconds>(now-start).count(),
    };
    prev_update = now;
    emit(ctx, u);
}
