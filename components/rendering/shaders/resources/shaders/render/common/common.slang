// Ported from NAADF (TU Wien, 2026), MIT. See docs/licenses/naadf.txt.
#pragma once

#define FLATTEN_INDEX(pos, strideY, strideZ) ((pos).z * (strideZ) + (pos).y * (strideY) + (pos).x)

#include "../../settings.slang"
#include "commonConstants.slang"
#include "commonRenderPipeline.slang"
#include "commonRayTracing.slang"
#include "commonColorCompression.slang"
#include "commonOther.slang"
