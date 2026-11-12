// Ported from NAADF (TU Wien, 2026), MIT. See licenses/naadf.txt.
#include "../../settings.slang"
#include "bounds_common.slang"

#define BLOCK_STATE_CHILD 2
#define BLOCK_STATE_UNIFORM_EMPTY 0
#define BLOCK_STATE_UNIFORM_FULL 1

struct BoundQueueInfo
{
    uint start;
    uint size;
};


uint chunkSizeX, chunkSizeY, chunkSizeZ;
uint groupSizeX, groupSizeY, groupSizeZ;

uint boundGroupQueueMaxSize;
uint changedChunkCount;
uint changedGroupCount, changedBlockCount, changedVoxelCount;


#include "chunk_write.slang"
[vk::binding(3, 0)] RWStructuredBuffer<uint> blocks;
[vk::binding(56, 0)] RWStructuredBuffer<uint> voxels;

[vk::binding(13, 0)] StructuredBuffer<uint2> changedGroupsDynamic;
[vk::binding(12, 0)] StructuredBuffer<uint2> changedChunksDynamic;
[vk::binding(11, 0)] StructuredBuffer<uint> changedBlocksDynamic;
[vk::binding(14, 0)] StructuredBuffer<uint> changedVoxelsDynamic;

[vk::binding(9, 0)] RWStructuredBuffer<BoundQueueInfo> boundQueueInfo;
[vk::binding(8, 0)] RWStructuredBuffer<uint> boundGroupQueues;
[vk::binding(6, 0)] RWStructuredBuffer<uint3> boundGroupMasks;

groupshared uint lowestBoundsShared[3];

[numthreads(4, 4, 4)]
void applyGroupChange(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint dispatchIndex = groupID.x + groupID.y * 32768;
    if (dispatchIndex >= changedGroupCount)
        return;
    if (localIndex < 3)
    {
        lowestBoundsShared[localIndex] = 31;
    }
    GroupMemoryBarrierWithGroupSync();
    uint2 change = changedGroupsDynamic[dispatchIndex];
    uint3 groupPosition = uint3(change.x & 0x7FF, (change.x >> 11) & 0x3FF, change.x >> 21);
    uint groupIndex = groupPosition.x + groupPosition.y * groupSizeX + groupPosition.z * groupSizeX * groupSizeY;
    uint3 chunkPos = groupPosition * 4 + localID;
    bool isChunkActive = all(chunkPos < uint3(chunkSizeX, chunkSizeY, chunkSizeZ));
    uint curChunk = BLOCK_STATE_UNIFORM_FULL << 30;
    if (isChunkActive)
        curChunk = load_chunk_geometry(chunkPos);
    uint chunkState = curChunk >> 30;
    bool isResetCompletely = change.y >> 30;
    
    uint lowestX = isResetCompletely ? 0 : 31;
    uint lowestY = isResetCompletely ? 0 : 31;
    uint lowestZ = isResetCompletely ? 0 : 31;
    
    if (isChunkActive && chunkState == BLOCK_STATE_UNIFORM_EMPTY)
    {
        uint newChunk = chunkState;

        uint changeBoundXM = (change.y & 0x1F) + localID.x;
        uint changeBoundXP = ((change.y >> 5) & 0x1F) + (3 - localID.x);
        uint changeBoundYM = ((change.y >> 10) & 0x1F) + localID.y;
        uint changeBoundYP = ((change.y >> 15) & 0x1F) + (3 - localID.y);
        uint changeBoundZM = ((change.y >> 20) & 0x1F) + localID.z;
        uint changeBoundZP = ((change.y >> 25) & 0x1F) + (3 - localID.z);
        uint changeAll = min(min(changeBoundXM, changeBoundXP), min(min(changeBoundYM, changeBoundYP), min(changeBoundZM, changeBoundZP)));
        
        uint newBoundXM = min(curChunk & 0x1F, changeAll);
        uint newBoundXP = min((curChunk >> 5) & 0x1F, changeAll);
        uint newBoundYM = min((curChunk >> 10) & 0x1F, changeAll);
        uint newBoundYP = min((curChunk >> 15) & 0x1F, changeAll);
        uint newBoundZM = min((curChunk >> 20) & 0x1F, changeAll);
        uint newBoundZP = min((curChunk >> 25) & 0x1F, changeAll);
        if (!isResetCompletely)
            newChunk |= newBoundXM | (newBoundXP << 5) | (newBoundYM << 10) | (newBoundYP << 15) | (newBoundZM << 20) | (newBoundZP << 25);
        
        lowestX = min(lowestX, min(newBoundXM, newBoundXP));
        lowestY = min(lowestY, min(newBoundYM, newBoundYP));
        lowestZ = min(lowestZ, min(newBoundZM, newBoundZP));
        
        store_chunk_geometry(chunkPos, newChunk);
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    InterlockedMin(lowestBoundsShared[0], lowestX);
    InterlockedMin(lowestBoundsShared[1], lowestY);
    InterlockedMin(lowestBoundsShared[2], lowestZ);
    
    GroupMemoryBarrierWithGroupSync();
    // Add chunk group to queue for next bound size
    if (localIndex < 3)
    {
        // Check if chunk group is already in next queue
        uint xyz = localIndex;
        uint nextBoundSize = lowestBoundsShared[localIndex];
        if (isResetCompletely)
            nextBoundSize = 0;
        bool isAlreadyInQueue = (boundGroupMasks[groupIndex][xyz] >> (nextBoundSize)) & 0x1;
        if (!isAlreadyInQueue && nextBoundSize < 31)
        {
            // Increase next Queue by 1
            boundGroupMasks[groupIndex][xyz] |= 1 << nextBoundSize;
            uint originalQueueSize;
            InterlockedAdd(boundQueueInfo[nextBoundSize * 3 + xyz].size, 1, originalQueueSize);
            
            // Store group into queue
            uint queueStartIndex = boundQueueInfo[nextBoundSize * 3 + xyz].start;
            uint queueIndex = (nextBoundSize * 3 + xyz) * boundGroupQueueMaxSize + ((queueStartIndex + originalQueueSize) % boundGroupQueueMaxSize);
            boundGroupQueues[queueIndex] = change.x;
        }
    }
}

[numthreads(64, 1, 1)]
void applyChunkChange(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint dispatchIndex = (groupID.x + groupID.y * 32768) * 64 + localIndex;
    if (dispatchIndex >= changedChunkCount)
        return;
    
    uint2 change = changedChunksDynamic[dispatchIndex];
    uint3 chunkPos = uint3(change.x & 0x7FF, (change.x >> 11) & 0x3FF, change.x >> 21);
    store_chunk_geometry(chunkPos, change.y);
}

[numthreads(4, 4, 4)]
void applyBlockChange(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint dispatchIndex = groupID.x + groupID.y * 32768;
    if (dispatchIndex >= changedBlockCount)
        return;
    uint changePointer = changedBlocksDynamic[dispatchIndex * (64 + 1)];
    
    uint curBlock = changedBlocksDynamic[dispatchIndex * (64 + 1) + 1 + localIndex];
    cachedCell[localIndex] = curBlock;
    
    uint3 blockPosInChunk = int3(localIndex % 4, (localIndex / 4) % 4, (localIndex / 16) % 4);
    ComputeBounds4(localIndex, blockPosInChunk, 30, 0x3, curBlock);
    
    if ((curBlock >> 30) != 0)
        cachedCell[localIndex] = curBlock;
    
    GroupMemoryBarrierWithGroupSync();
    
    blocks[changePointer + localIndex] = cachedCell[localIndex];
}

[numthreads(4, 4, 4)]
void applyVoxelChange(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint dispatchIndex = groupID.x + groupID.y * 32768;
    if (dispatchIndex >= changedVoxelCount)
        return;
    uint changePointer = changedVoxelsDynamic[dispatchIndex * (32 + 1)];
    
    uint curVoxelPair = changedVoxelsDynamic[dispatchIndex * (32 + 1) + 1 + localIndex / 2];
    uint curVoxel = localIndex % 2 == 0 ? (curVoxelPair & 0xFFFF) : (curVoxelPair >> 16);
    cachedCell[localIndex] = curVoxel;
    
    uint3 voxelPosInBlock = int3(localIndex % 4, (localIndex / 4) % 4, (localIndex / 16) % 4);
    ComputeBounds4(localIndex, voxelPosInBlock, 15, 0x1, curVoxel);
    
    if (curVoxel >> 15)
        cachedCell[localIndex] = curVoxel;
    
    GroupMemoryBarrierWithGroupSync();
    
    if (localIndex < 32)
        voxels[changePointer + localIndex] = cachedCell[localIndex * 2] | (cachedCell[localIndex * 2 + 1] << 16);
}
