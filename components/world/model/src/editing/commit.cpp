#include "../state.hpp"

#include <engine/foundation/parallel/parallel.hpp>

namespace engine::world::model {
namespace {

struct PreparedEdit {
    std::size_t index;
    const ChunkEdit* edit;
    PackedChunk* packed;
};

struct PreparedBatch {
    std::vector<PreparedEdit> entries;
    std::unique_ptr<PackedChunk[]> storage;
};

PreparedBatch prepare_edits(const std::unordered_map<std::size_t, ChunkEdit>& edits, bool raw) {
    PreparedBatch prepared;
    std::size_t count = 0;
    for (const auto& [index, edit] : edits) {
        static_cast<void>(index);
        count += edit.voxels ? 1 : 0;
    }
    prepared.storage = std::make_unique_for_overwrite<PackedChunk[]>(count);
    prepared.entries.reserve(edits.size());
    std::size_t next = 0;
    for (const auto& [index, edit] : edits) {
        prepared.entries.push_back(
            {index, &edit, edit.voxels ? &prepared.storage[next++] : nullptr});
    }
    foundation::parallel::for_each_index(prepared.entries.size(), [&](std::size_t index) {
        const auto& item = prepared.entries[index];
        if (item.edit->voxels) {
            pack_chunk(*item.edit->voxels, raw, *item.packed);
        }
    });
    return prepared;
}

} // namespace

void World::commit_edits() {
    if (state_->edits.empty()) {
        return;
    }
    auto prepared = prepare_edits(state_->edits, state_->pool.raw_updates);
    std::vector<std::size_t> changed;
    for (const auto& item : prepared.entries) {
        const auto index = item.index;
        const auto& edit = *item.edit;
        const auto old = state_->model.chunks[index];
        const auto next = edit.voxels ? store_chunk(state_->model, *item.packed, state_->pool, old)
                                      : (edit.material == 0 ? 0U : 0x40000000U | edit.material);
        if (!edit.voxels && state_->gpu_authoritative) {
            release_chunk(state_->model, old, state_->pool);
        }
        if (!state_->gpu_authoritative) {
            release_chunk(state_->model, old, state_->pool);
        } else {
            state_->changed_chunks.push_back(static_cast<std::uint32_t>(index));
        }
        state_->model.chunks[index] = next;
        if (!state_->gpu_authoritative || (old >> 30) == 0 || (next >> 30) == 0) {
            changed.push_back(index);
        }
    }
    state_->edits.clear();
    unique_indices(state_->changed_chunks);
    unique_indices(state_->pool.changed_blocks);
    unique_indices(state_->pool.changed_voxels);
    state_->invalidate(changed);
    ++state_->revision;
    ++state_->chunk_revision;
    ++state_->geometry_revision;
    update_bounds();
}

} // namespace engine::world::model
