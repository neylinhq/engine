#ifndef ENGINE_WORLD_STORAGE_SRC_CODEC_CVOX_HPP
#define ENGINE_WORLD_STORAGE_SRC_CODEC_CVOX_HPP

#include <string_view>

namespace engine::world::storage {

void validate_cvox_material_name(std::string_view name);

} // namespace engine::world::storage

#endif
