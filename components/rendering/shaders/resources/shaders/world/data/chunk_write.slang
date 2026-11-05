// Non-overlapping chunk updates. Dispatch dependencies separate these passes
// from AADF's concurrent, atomic neighborhood reads.
#pragma once

#ifdef NAADF_PACKED_CHUNKS
[vk::binding(18, 0)] RWTexture3D<uint2> chunks;

uint load_chunk_geometry(uint3 position)
{
    return chunks[position].x;
}

void store_chunk(uint3 position, uint2 words)
{
    chunks[position] = words;
}

void store_chunk_geometry(uint3 position, uint geometry)
{
    uint2 words = chunks[position];
    words.x = geometry;
    chunks[position] = words;
}

void store_chunk_entities(uint3 position, uint entities)
{
    uint2 words = chunks[position];
    words.y = entities;
    chunks[position] = words;
}
#else
[vk::binding(18, 0)] RWTexture3D<uint> chunks;
[vk::binding(60, 0)] RWTexture3D<uint> chunkEntities;

uint load_chunk_geometry(uint3 position)
{
    return chunks[position];
}

void store_chunk(uint3 position, uint2 words)
{
    chunks[position] = words.x;
    chunkEntities[position] = words.y;
}

void store_chunk_geometry(uint3 position, uint geometry)
{
    chunks[position] = geometry;
}

void store_chunk_entities(uint3 position, uint entities)
{
    chunkEntities[position] = entities;
}
#endif
