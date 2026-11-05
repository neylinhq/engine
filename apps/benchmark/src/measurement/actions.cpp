#include "session.hpp"
namespace engine::app::benchmark {
namespace {
void apply(Session& session, const Event& event) {
    switch (event.operation) {
    case Operation::brush:
        world::editing::apply_brush(session.world, event.brush);
        break;
    case Operation::fill:
        world::editing::flood_fill(session.world, event.voxel, event.material);
        break;
    case Operation::stamp:
        world::editing::stamp_model(
            session.world, session.world.models()[session.models.at(event.model)], event.voxel);
        break;
    case Operation::spawn: {
        const auto type = session.entity_types.at(event.model);
        session.entities[event.entity] =
            session.world.spawn_entity(type, event.position / session.scenario.voxel_size);
        break;
    }
    case Operation::move_entity:
        session.world.set_entity_position(session.entities.at(event.entity),
                                          event.position / session.scenario.voxel_size);
        break;
    case Operation::clear_entities:
        session.world.clear_entities();
        session.entities.clear();
        break;
    }
    session.world.commit_edits();
}
} // namespace
void apply_events(Session& session, FrameSample& sample) {
    const auto& events = session.scenario.events;
    while (session.next_event < events.size() && events[session.next_event].frame == sample.frame) {
        const auto index = session.next_event++;
        const auto before = session.world.view().revision;
        const auto start = Clock::now();
        apply(session, events[index]);
        const auto duration = elapsed(start);
        session.results.events.push_back({index, sample.frame, duration, before,
                                          session.world.view().revision,
                                          session.world.entities().size()});
        sample.edit_ms += duration;
        ++sample.events;
    }
}
} // namespace engine::app::benchmark
