// Ported from NAADF (TU Wien, 2026), MIT. See docs/licenses/naadf.txt.
#define GroupSizeX 64
#define EMPTY_BLOCK 0x0

struct HashValue
{
    uint voxelPointer;
    uint useCount;
    uint hashRaw;
};

int oldSize, newSize;
uint mapCopyOffset;
[vk::binding(45, 0)] StructuredBuffer<HashValue> oldMap;
[vk::binding(44, 0)] RWStructuredBuffer<HashValue> newMap;

uint hashCoefficients[65];
uint voxelsToHash[32];
[vk::binding(47, 0)] RWStructuredBuffer<uint> resultHash;

[numthreads(GroupSizeX, 1, 1)]
void copyMap(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint ID = mapCopyOffset + globalID.x;
    
    if (ID >= oldSize)
        return;
    
    HashValue hashValue = oldMap[ID];
    if (hashValue.voxelPointer != EMPTY_BLOCK)
    {
        uint newHashBound = hashValue.hashRaw & (newSize - 1);
        for (uint count = 0; count < uint(newSize); ++count)
        {
            uint originalPointer;
            InterlockedCompareExchange(newMap[newHashBound].voxelPointer, EMPTY_BLOCK, hashValue.voxelPointer, originalPointer);
            if (originalPointer == EMPTY_BLOCK)
            {
                newMap[newHashBound].hashRaw = hashValue.hashRaw;
                newMap[newHashBound].useCount = hashValue.useCount;
                return;
            }
            newHashBound = (newHashBound + 1) & (newSize - 1);
        }
        // Capacity is reserved by the host. A missing entry fails readback validation.
    }
}

[numthreads(1, 1, 1)]
void testHash(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint hash = hashCoefficients[0];
    for (uint i = 0; i < 32; ++i)
    {
        uint voxelComp = voxelsToHash[i];
        hash += hashCoefficients[i * 2 + 1] * (voxelComp & 0x7FFF);
        hash += hashCoefficients[i * 2 + 2] * ((voxelComp >> 16) & 0x7FFF);
    }
    resultHash[0] = hash;

}
