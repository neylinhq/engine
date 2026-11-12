// Ported from NAADF (TU Wien, 2026), MIT. See docs/licenses/naadf.txt.
#pragma once

#define FLATTEN_INDEX(pos, strideY, strideZ) ((pos).z * (strideZ) + (pos).y * (strideY) + (pos).x)

#include "../../settings.slang"
#include "common_constants.slang"
#include "common_render_pipeline.slang"
#include "common_ray_tracing.slang"
#include "common_color_compression.slang"
#include "common_other.slang"
