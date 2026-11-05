// Ported from NAADF (TU Wien, 2026), MIT. See docs/licenses/naadf.txt.
#include "../../settings.slang"
#include "boundsCommon.slang"

#define EMPTY_BLOCK 0x0
#define BLOCK_STATE_CHILD 2
#define BLOCK_STATE_UNIFORM_EMPTY 0
#define BLOCK_STATE_UNIFORM_FULL 1

#define BOUND_INFO_GROUPS 0
#define BOUND_INFO_BLOCKS 32 * 3
#define BOUND_INFO_CHUNKS 32 * 3 + 1



struct HashValue
{
    uint voxelPointer;
    uint useCount;
    uint hashRaw;
};

struct BoundQueueInfo
{
    int start;
    int size;
};

[vk::binding(9, 0)] RWStructuredBuffer<BoundQueueInfo> boundQueueInfo;
[vk::binding(4, 0)] RWStructuredBuffer<uint> boundBlockQueues;
[vk::binding(5, 0)] RWStructuredBuffer<uint> boundChunkQueues;


#include "chunk_write.slang"
[vk::binding(3, 0)] RWStructuredBuffer<uint> blocks;
[vk::binding(56, 0)] globallycoherent RWStructuredBuffer<uint> voxels;

[vk::binding(2, 0)] RWStructuredBuffer<uint> blockVoxelCount;
[vk::binding(49, 0)] StructuredBuffer<uint> segmentVoxelBuffer;
[vk::binding(40, 0)] globallycoherent RWStructuredBuffer<HashValue> hashMap;
uint hashCoefficients[65];

uint boundBlockQueueMaxSize;
uint boundChunkQueueMaxSize;
uint hashMapSize, segmentSizeInChunks;
uint chunkOffsetX, chunkOffsetY, chunkOffsetZ;
uint sizeInChunksX, sizeInChunksY, sizeInChunksZ;
uint queueStart;

groupshared uint referenceBlock;
groupshared uint isAllBlocksEqual;
groupshared uint insertBlockIndex;

[vk::binding(38, 0)] RWStructuredBuffer<uint> gpuCpuSyncBuffer;

uint copyOffset, copyMaxCount;
uint boundsGroupOffset;
uint resolvePointers;

bool MatchesPendingBlock(uint first, uint second)
{
    for (uint i = 0; i < 32; ++i)
    {
        if (segmentVoxelBuffer[first + i] != segmentVoxelBuffer[second + i])
            return false;
    }
    return true;
}

bool MatchesPublishedBlock(uint hash, uint slot, uint first, uint second)
{
    DeviceMemoryBarrier();
    if (hashMap[slot].hashRaw != hash)
        return false;
    for (uint i = 0; i < 32; ++i)
    {
        if (segmentVoxelBuffer[first + i] != voxels[second + i])
            return false;
    }
    return true;
}

uint GetHashSlot(uint hash, uint voxelRawStart)
{
    uint hashBounds = hash & (hashMapSize - 1);
    uint count = 0;
    [allow_uav_condition][loop]
    while (count < hashMapSize)
    {
        uint originalPointer;
        
        InterlockedCompareExchange(hashMap[hashBounds].voxelPointer, EMPTY_BLOCK, 0x80000000 | voxelRawStart, originalPointer);
        
        if (originalPointer == EMPTY_BLOCK) // Found empty hash slot
        {
            uint originalIndex;
            InterlockedAdd(hashMap[hashBounds].useCount, 1);
            InterlockedAdd(blockVoxelCount[0], 64, originalIndex);
            originalIndex /= 2;
            for (int i = 0; i < 32; ++i)
            {
                voxels[originalIndex + i] = segmentVoxelBuffer[voxelRawStart + i];
            }
            hashMap[hashBounds].hashRaw = hash;
            DeviceMemoryBarrier();
            uint test;
            InterlockedExchange(hashMap[hashBounds].voxelPointer, originalIndex, test);
            return hashBounds;
        }
        bool isAllEqual;
        if ((originalPointer & 0x80000000) != 0)
        {
            // The claiming source block is immutable for the whole segment.
            // Comparing it needs neither the owner's hash metadata nor its final allocation.
            isAllEqual = MatchesPendingBlock(voxelRawStart, originalPointer & 0x7FFFFFFF);
        }
        else
        {
            isAllEqual = MatchesPublishedBlock(hash, hashBounds, voxelRawStart, originalPointer);
        }
        if (isAllEqual)
        {
            InterlockedAdd(hashMap[hashBounds].useCount, 1);
            return hashBounds;
        }
        hashBounds = (hashBounds + 1) & (hashMapSize - 1);
        count++;

    }
    return 0xFFFFFFFF; // Invalid node state is rejected by host graph validation.
}

void ResolveBlockPointers(uint3 chunkPos, uint localIndex)
{
    uint chunk = load_chunk_geometry(chunkPos);
    if ((chunk >> 30) != BLOCK_STATE_CHILD)
        return;
    uint index = (chunk & 0x3FFFFFFF) + localIndex;
    uint block = blocks[index];
    if ((block >> 30) != BLOCK_STATE_CHILD)
        return;
    uint slot = block & 0x3FFFFFFF;
    if (slot >= hashMapSize)
    {
        blocks[index] = 0xC0000000;
        return;
    }
    uint pointer = hashMap[slot].voxelPointer;
    if (pointer < 32 || (pointer & 0x8000001F) != 0 || pointer + 32 > blockVoxelCount[0] / 2)
        blocks[index] = 0xC0000000;
    else
        blocks[index] = pointer | (BLOCK_STATE_CHILD << 30);
}

[numthreads(4, 4, 4)]
void calcBlockFromRawData(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint3 chunkPos = groupID + uint3(chunkOffsetX, chunkOffsetY, chunkOffsetZ);
    if (resolvePointers != 0)
    {
        ResolveBlockPointers(chunkPos, localIndex);
        return;
    }
    uint chunkIndexInSegment = groupID.x + groupID.y * segmentSizeInChunks + groupID.z * segmentSizeInChunks * segmentSizeInChunks;
    uint voxelIndexInSegment = chunkIndexInSegment * 2048 + localIndex * 32;
    
    // Calculate hash and check if all voxels are the same type
    uint hash = hashCoefficients[0];
    uint firstVoxelTypeComp = segmentVoxelBuffer[voxelIndexInSegment];
    uint firstVoxelType = firstVoxelTypeComp & 0x7FFF;
    bool isAllSame = (firstVoxelTypeComp & 0xFFFF) == (firstVoxelTypeComp >> 16);
    for (uint i = 0; i < 32; ++i)
    {
        uint voxelComp = segmentVoxelBuffer[voxelIndexInSegment + i];
        hash += hashCoefficients[i * 2 + 1] * (voxelComp & 0x7FFF);
        hash += hashCoefficients[i * 2 + 2] * ((voxelComp >> 16) & 0x7FFF);
        isAllSame = isAllSame && (firstVoxelTypeComp == voxelComp);
    }
    
    uint block;
    if (isAllSame)
        block = firstVoxelType | (firstVoxelType == 0 ? BLOCK_STATE_UNIFORM_EMPTY : BLOCK_STATE_UNIFORM_FULL) << 30;
    else
        block = GetHashSlot(hash, voxelIndexInSegment) | BLOCK_STATE_CHILD << 30;
    
    GroupMemoryBarrierWithGroupSync();
    
    if (localIndex == 0)
    {
        referenceBlock = block;     // Thread 0 stores a reference value
        isAllBlocksEqual = 1;    // Assume all are equal initially
    }
    
    GroupMemoryBarrierWithGroupSync();

    if (block != referenceBlock || !isAllSame)
        InterlockedAnd(isAllBlocksEqual, 0);
    
    GroupMemoryBarrierWithGroupSync();
    
    if (localIndex == 0)
    {
        uint state = 0;
        if (isAllBlocksEqual != 0)
            state = firstVoxelType | (firstVoxelType == 0 ? BLOCK_STATE_UNIFORM_EMPTY : BLOCK_STATE_UNIFORM_FULL) << 30;
        else
        {
            InterlockedAdd(blockVoxelCount[1], 64, insertBlockIndex);
            state = insertBlockIndex | BLOCK_STATE_CHILD << 30;
        }
        
        store_chunk(chunkPos, uint2(state, 0));
    }
    
    GroupMemoryBarrierWithGroupSync();

    if (isAllBlocksEqual == 0)
        blocks[insertBlockIndex + localIndex] = block;
}

[numthreads(64, 1, 1)]
void chunkCopyToCpu(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint chunkIndex = copyOffset + globalID.x;
    if (chunkIndex >= copyMaxCount)
        return;
    uint3 chunkPos = int3(chunkIndex % sizeInChunksX, (chunkIndex / sizeInChunksX) % sizeInChunksY, chunkIndex / (sizeInChunksX * sizeInChunksY));
    gpuCpuSyncBuffer[globalID.x] = load_chunk_geometry(chunkPos);
}

[numthreads(64, 1, 1)]
void computeVoxelBounds(uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex)
{
    uint blockIndex = boundsGroupOffset + groupID.x;
    
    uint voxelIndex = blockIndex * 64 + localIndex;
    
    uint curVoxelPair = voxels[voxelIndex / 2];
    uint curVoxel = voxelIndex % 2 == 0 ? (curVoxelPair & 0xFFFF) : (curVoxelPair >> 16);
    uint origVoxel = curVoxel;
    uint state = curVoxel >> 15;
    
    int3 voxelPosInBlock = int3(localIndex % 4, (localIndex / 4) % 4, (localIndex / 16) % 4);
        
    cachedCell[localIndex] = curVoxel;
    ComputeBounds4(localIndex, voxelPosInBlock, 15, 0x1, curVoxel);
    
    if (state == 1)
        cachedCell[localIndex] = origVoxel;
    
    GroupMemoryBarrierWithGroupSync();
    
    if (localIndex % 2 == 0)
        voxels[voxelIndex / 2] = cachedCell[localIndex] | (cachedCell[localIndex + 1] << 16);
}

[numthreads(64, 1, 1)]
void computeBlockBounds(uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex)
{
    uint chunkIndex = boundsGroupOffset + groupID.x;
    
    uint blockIndex = chunkIndex * 64 + localIndex;
    
    uint curBlock = blocks[blockIndex];
    uint origBlock = curBlock;
    uint state = curBlock >> 30;
    
    int3 blockPosInChunk = int3(localIndex % 4, (localIndex / 4) % 4, (localIndex / 16) % 4);
        
    cachedCell[localIndex] = curBlock;
    ComputeBounds4(localIndex, blockPosInChunk, 30, 0x3, curBlock);
    
    if (state != 0)
        cachedCell[localIndex] = origBlock;
    
    GroupMemoryBarrierWithGroupSync();
    
    blocks[blockIndex] = cachedCell[localIndex];
}
