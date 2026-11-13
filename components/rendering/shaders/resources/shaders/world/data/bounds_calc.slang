// Ported from NAADF (TU Wien, 2026), MIT. See licenses/naadf.txt.
#include "../../settings.slang"
#include "bounds_common.slang"
#include "chunk_atomic.slang"

#define BoundsIdX 0
#define BoundsIdY 1
#define BoundsIdZ 2
#define BLOCK_STATE_CHILD 2
#define BLOCK_STATE_UNIFORM_EMPTY 0
#define BLOCK_STATE_UNIFORM_FULL 1

#define BOUND_INFO_GROUPS 0

struct BoundQueueInfo
{
    uint start;
    uint size;
};

uint chunkSizeX, chunkSizeY, chunkSizeZ;
uint groupSizeX, groupSizeY, groupSizeZ;

[vk::binding(9, 0)] RWStructuredBuffer<BoundQueueInfo> boundQueueInfo;
[vk::binding(8, 0)] RWStructuredBuffer<uint> boundGroupQueues;
[vk::binding(10, 0)] RWStructuredBuffer<uint> boundRefinedInfo;

[vk::binding(6, 0)] RWStructuredBuffer<uint3> boundGroupMasks;

[vk::binding(7, 0)] RWByteAddressBuffer boundGroupQueueDispatchCount;

uint boundsInitOffset, maxGroupBoundDispatch, boundGroupQueueMaxSize;

uint frameIndex;


[numthreads(64, 1, 1)]
void addInitialGroupsToBoundQueue(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint groupIndex = boundsInitOffset + (groupID.x + groupID.y * 32768) * 64 + localIndex;
    if (groupIndex >= boundGroupQueueMaxSize)
        return;
    uint3 groupPos = int3(groupIndex % groupSizeX, (groupIndex / groupSizeX) % groupSizeY, groupIndex / (groupSizeX * groupSizeY));
    
    boundGroupMasks[groupIndex] = uint3(1, 1, 1);
    boundGroupQueues[boundGroupQueueMaxSize * 0 + groupIndex] = groupPos.x | (groupPos.y << 11) | (groupPos.z << 21); // X
    boundGroupQueues[boundGroupQueueMaxSize * 1 + groupIndex] = groupPos.x | (groupPos.y << 11) | (groupPos.z << 21); // Y
    boundGroupQueues[boundGroupQueueMaxSize * 2 + groupIndex] = groupPos.x | (groupPos.y << 11) | (groupPos.z << 21); // Z
}


[numthreads(1, 1, 1)]
void prepareGroupBounds(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    // Find valid queue info
    BoundQueueInfo queueInfo;
    bool hasFoundBoundsToProcess = false;
    uint foundBoundSize = 0, foundXYZ = 0;
    for (uint i = 0; i < 32 && !hasFoundBoundsToProcess; ++i)
    {
        uint iMod = i % 32;
        for (uint xyz = 0; xyz < 3; ++xyz)
        {
            queueInfo = boundQueueInfo[BOUND_INFO_GROUPS + iMod * 3 + xyz];
            if (queueInfo.size > 0)
            {
                hasFoundBoundsToProcess = true;
                foundBoundSize = iMod;
                foundXYZ = xyz;
                break;
            }
        }
    }
    
    uint groupAmountToProcess = 0;
    if (hasFoundBoundsToProcess)
    {
        // Process group queue info
        groupAmountToProcess = min(maxGroupBoundDispatch, queueInfo.size);
        boundRefinedInfo[0] = queueInfo.start % boundGroupQueueMaxSize;
        boundRefinedInfo[1] = groupAmountToProcess;
        boundRefinedInfo[2] = foundBoundSize | (foundXYZ << 16);
        
        // Update info for next frame
        BoundQueueInfo newQueueInfo;
        newQueueInfo.start = (queueInfo.start + groupAmountToProcess) % boundGroupQueueMaxSize;
        newQueueInfo.size = queueInfo.size - groupAmountToProcess;
        boundQueueInfo[BOUND_INFO_GROUPS + foundBoundSize * 3 + foundXYZ] = newQueueInfo;
    }
    else
        boundRefinedInfo[1] = 0;
    
    // Vulkan guarantees at least 65535 groups per axis. A fixed stride also permits
    // the original UI's 262144-group budget without exceeding that limit.
    boundGroupQueueDispatchCount.Store(0, min(32768, max(1, groupAmountToProcess)));
    boundGroupQueueDispatchCount.Store(4, max(1, (groupAmountToProcess + 32767) / 32768));
    boundGroupQueueDispatchCount.Store(8, 1);
}

void addBoundsGroup(int3 chunkPos, int3 directionOffset, uint mask, uint boundsLocation, uint curBound, inout uint curChunk)
{
    if (((curChunk >> boundsLocation) & 0x1F) != curBound)
    {
        return;
    }
    int3 neighborChunkPos = chunkPos + directionOffset;
    if (any(neighborChunkPos < 0) || neighborChunkPos.x >= (int) chunkSizeX || neighborChunkPos.y >= (int) chunkSizeY || neighborChunkPos.z >= (int) chunkSizeZ)
    {
        curChunk += 1 << boundsLocation;
        return;
    }
    
    uint2 neighborWords = load_chunk_atomic(neighborChunkPos);
    uint neighbor = neighborWords.x;
    uint neighborEntities = neighborWords.y;
    
    if ((neighbor >> 30) == BLOCK_STATE_UNIFORM_EMPTY && neighborEntities == 0)
    {
        if ((checkMatchingBounds(neighbor, curChunk, 0, 5, 0x1F) & mask) == mask)
            curChunk += 1 << boundsLocation;
    }
}

[numthreads(4, 4, 4)]
void computeGroupBounds(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    // Read queue info
    uint start = boundRefinedInfo[0];
    uint count = boundRefinedInfo[1];
    uint boundInfo = boundRefinedInfo[2];
    uint boundSize = boundInfo & 0xFFFF;
    uint boundXYZ = boundInfo >> 16;
    
    uint dispatchIndex = groupID.x + groupID.y * 32768;
    // This is uniform for the entire workgroup, so no invocation skips a barrier.
    // In particular, an empty indirect dispatch must not resurrect a stale queue.
    if (dispatchIndex >= count)
        return;
    
    // Get chunk group
    uint groupPositionComp = boundGroupQueues[(boundSize * 3 + boundXYZ) * boundGroupQueueMaxSize + ((start + dispatchIndex) % boundGroupQueueMaxSize)];
    uint3 groupPosition = uint3(groupPositionComp & 0x7FF, (groupPositionComp >> 11) & 0x3FF, groupPositionComp >> 21);
    uint groupIndex = groupPosition.x + groupPosition.y * groupSizeX + groupPosition.z * groupSizeX * groupSizeY;
    if (localIndex == 0)
        boundGroupMasks[groupIndex][boundXYZ] &= ~(1 << boundSize);
    
    // Get chunks in group
    uint3 chunkPos = groupPosition * 4 + localID;
    bool isChunkActive = all(chunkPos < uint3(chunkSizeX, chunkSizeY, chunkSizeZ));
    uint curChunk = BLOCK_STATE_UNIFORM_FULL << 30;
    uint curEntities = 0;
    if (isChunkActive)
    {
        uint2 words = load_chunk_atomic(chunkPos);
        curChunk = words.x;
        curEntities = words.y;
    }
    
    uint curChunkCopy = curChunk;
    uint chunkState = curChunk >> 30;
    
    // Compute next bounds for current chunk
    if (chunkState == BLOCK_STATE_UNIFORM_EMPTY && curEntities == 0)
    {
        uint maskMinus = boundXYZ == 0 ? MASK_MX : (boundXYZ == 1 ? MASK_MY : MASK_MZ);
        uint maskPlus = boundXYZ == 0 ? MASK_PX : (boundXYZ == 1 ? MASK_PY : MASK_PZ);
        int3 dirOffsetAbs = int3(boundXYZ == 0 ? 1 : 0, boundXYZ == 1 ? 1 : 0, boundXYZ == 2 ? 1 : 0);
        
        addBoundsGroup(chunkPos, dirOffsetAbs * (-1), maskMinus, boundXYZ * 10 + 0, boundSize, curChunk);
        addBoundsGroup(chunkPos, dirOffsetAbs * (+1), maskPlus, boundXYZ * 10 + 5, boundSize, curChunk);
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    if (isChunkActive && curChunkCopy != curChunk)
    {
        store_chunk_atomic(chunkPos, uint2(curChunk, curEntities));
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    // Add chunk group to queue for next bound size
    if (localIndex == 0 && boundSize < 30)
    {
        // Check if chunk group is already in next queue
        uint nextBoundSize = boundSize + 1;
        bool isAlreadyInQueue = (boundGroupMasks[groupIndex][boundXYZ] >> (nextBoundSize)) & 0x1;
        if (!isAlreadyInQueue)
        {
            // Increase next Queue by 1
            boundGroupMasks[groupIndex][boundXYZ] |= 1 << nextBoundSize;
            uint originalQueueSize;
            InterlockedAdd(boundQueueInfo[nextBoundSize * 3 + boundXYZ].size, 1, originalQueueSize);
            
            // Store group into queue
            uint queueStartIndex = boundQueueInfo[nextBoundSize * 3 + boundXYZ].start;
            uint queueIndex = (nextBoundSize * 3 + boundXYZ) * boundGroupQueueMaxSize + ((queueStartIndex + originalQueueSize) % boundGroupQueueMaxSize);
            boundGroupQueues[queueIndex] = groupPositionComp;
        }
    }
}
