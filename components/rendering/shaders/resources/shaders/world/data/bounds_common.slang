// Ported from NAADF (TU Wien, 2026), MIT. See licenses/naadf.txt.

#ifndef __COMMON_BOUNDS__
#define __COMMON_BOUNDS__


#define MASK_MX 0x3D //0b111101
#define MASK_PX 0x3E //0b111110
#define MASK_MY 0x37 //0b110111
#define MASK_PY 0x3B //0b111011
#define MASK_MZ 0x1F //0b011111
#define MASK_PZ 0x2F //0b101111

groupshared uint cachedCell[64];

uint checkMatchingBounds(uint neighbor, uint curVoxel, const uint shiftOffset, const uint shiftCount, const uint shiftMask)
{
    uint mask = 0;
    mask |= (((neighbor >> (shiftOffset + shiftCount * 0)) & shiftMask) >= ((curVoxel >> (shiftOffset + shiftCount * 0)) & shiftMask)) << 0; //-x
    mask |= (((neighbor >> (shiftOffset + shiftCount * 1)) & shiftMask) >= ((curVoxel >> (shiftOffset + shiftCount * 1)) & shiftMask)) << 1; //+x
    mask |= (((neighbor >> (shiftOffset + shiftCount * 2)) & shiftMask) >= ((curVoxel >> (shiftOffset + shiftCount * 2)) & shiftMask)) << 2; //-y
    mask |= (((neighbor >> (shiftOffset + shiftCount * 3)) & shiftMask) >= ((curVoxel >> (shiftOffset + shiftCount * 3)) & shiftMask)) << 3; //+y
    mask |= (((neighbor >> (shiftOffset + shiftCount * 4)) & shiftMask) >= ((curVoxel >> (shiftOffset + shiftCount * 4)) & shiftMask)) << 4; //-z
    mask |= (((neighbor >> (shiftOffset + shiftCount * 5)) & shiftMask) >= ((curVoxel >> (shiftOffset + shiftCount * 5)) & shiftMask)) << 5; //+z
    
    return mask;
}

void addBoundsVoxelsOrBlocks(uint localIndex, uint mask, int directionOffset, uint boundsLocation, const uint stateLocation, const uint stateMask, inout uint curVoxel)
{
    uint neighbor = cachedCell[localIndex + directionOffset];
    if (((neighbor >> stateLocation) & stateMask) == 0)
    {
        if ((checkMatchingBounds(neighbor, curVoxel, 0, 2, 0x3) & mask) == mask)
            curVoxel += 1 << boundsLocation;
    }
}

void ComputeBounds4(int localIndex, int3 posInVolume, const uint stateLocation, const uint stateMask, uint curCell)
{
    bool isEmpty = ((curCell >> stateLocation) & stateMask) == 0;
    GroupMemoryBarrierWithGroupSync();
    for (int i = 0; i < 3; i++)
    {
        if (isEmpty && posInVolume.x > 0)
            addBoundsVoxelsOrBlocks(localIndex, MASK_MX, -1, 0, stateLocation, stateMask, curCell);
        if (isEmpty && posInVolume.x + 1 < 4)
            addBoundsVoxelsOrBlocks(localIndex, MASK_PX, 1, 2, stateLocation, stateMask, curCell);
        // Every invocation must finish reading the previous axis before any writes it.
        GroupMemoryBarrierWithGroupSync();
        cachedCell[localIndex] = curCell;
        GroupMemoryBarrierWithGroupSync();

        if (isEmpty && posInVolume.y > 0)
            addBoundsVoxelsOrBlocks(localIndex, MASK_MY, -4, 4, stateLocation, stateMask, curCell);
        if (isEmpty && posInVolume.y + 1 < 4)
            addBoundsVoxelsOrBlocks(localIndex, MASK_PY, 4, 6, stateLocation, stateMask, curCell);
        GroupMemoryBarrierWithGroupSync();
        cachedCell[localIndex] = curCell;
        GroupMemoryBarrierWithGroupSync();

        if (isEmpty && posInVolume.z > 0)
            addBoundsVoxelsOrBlocks(localIndex, MASK_MZ, -16, 8, stateLocation, stateMask, curCell);
        if (isEmpty && posInVolume.z + 1 < 4)
            addBoundsVoxelsOrBlocks(localIndex, MASK_PZ, 16, 10, stateLocation, stateMask, curCell);
        GroupMemoryBarrierWithGroupSync();
        cachedCell[localIndex] = curCell;
        GroupMemoryBarrierWithGroupSync();
    }
}


#endif // __COMMON_BOUNDS__
