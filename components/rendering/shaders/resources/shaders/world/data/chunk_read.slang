// Logical chunk words: geometry/AADF, followed by the entity-list reference.
#pragma once

#ifdef NAADF_PACKED_CHUNKS
[vk::binding(18, 0)] Texture3D<uint2> chunks;

uint2 load_chunk(uint3 position)
{
    return chunks[position];
}
#else
[vk::binding(18, 0)] Texture3D<uint> chunks;
[vk::binding(60, 0)] Texture3D<uint> chunkEntities;

uint2 load_chunk(uint3 position)
{
    return uint2(chunks[position], chunkEntities[position]);
}
#endif
