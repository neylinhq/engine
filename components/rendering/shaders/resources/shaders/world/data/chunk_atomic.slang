// All accesses to geometry within the in-place AADF dispatch are atomic.
// The packed view is R64_UINT over the same 8-byte texels sampled as RG32_UINT.
#pragma once

#ifdef NAADF_PACKED_CHUNKS
[vk::binding(61, 0)] RWTexture3D<uint64_t> chunkAtomic;

uint64_t load_chunk_word_atomic(__ref uint64_t word)
{
    return spirv_asm
    {
        result: $$uint64_t = OpAtomicLoad &word Device Relaxed;
    };
}

uint2 load_chunk_atomic(uint3 position)
{
    uint64_t word = load_chunk_word_atomic(chunkAtomic[position]);
    return uint2(uint(word), uint(word >> 32));
}

void store_chunk_atomic(uint3 position, uint2 words)
{
    uint64_t previous;
    uint64_t word = uint64_t(words.x) | (uint64_t(words.y) << 32);
    InterlockedExchange(chunkAtomic[position], word, previous);
}
#else
[vk::binding(18, 0)] RWTexture3D<uint> chunks;
[vk::binding(60, 0)] Texture3D<uint> chunkEntities;

uint load_chunk_word_atomic(__ref uint word)
{
    return spirv_asm
    {
        result: $$uint = OpAtomicLoad &word Device Relaxed;
    };
}

uint2 load_chunk_atomic(uint3 position)
{
    return uint2(load_chunk_word_atomic(chunks[position]), chunkEntities[position]);
}

void store_chunk_atomic(uint3 position, uint2 words)
{
    uint previous;
    InterlockedExchange(chunks[position], words.x, previous);
}
#endif
