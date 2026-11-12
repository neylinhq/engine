// Ported from NAADF (TU Wien, 2026), MIT. See licenses/naadf.txt.
#include "../../render/common/common.slang"

[vk::binding(17, 0)] StructuredBuffer<uint2> chunkUpdatesDynamic;
[vk::binding(23, 0)] StructuredBuffer<EntityChunkInstance> entityChunkInstancesDynamic;
[vk::binding(24, 0)] StructuredBuffer<uint4> entityHistoryDynamic;

#include "chunk_write.slang"
[vk::binding(22, 0)] RWStructuredBuffer<EntityChunkInstance> entityChunkInstances;
[vk::binding(25, 0)] RWStructuredBuffer<uint4> entityInstancesHistory;


uint entityInstanceCount, entityChunkInstanceCount, taaIndex, updateCount;


[numthreads(64, 1, 1)]
void updateChunks(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint dispatchIndex = (groupID.x + groupID.y * 32768) * 64 + localIndex;
    if (dispatchIndex >= updateCount)
        return;
    
    uint2 update = chunkUpdatesDynamic[dispatchIndex];
    uint3 chunkPos = uint3(update.x & 0x7FF, (update.x >> 11) & 0x3FF, update.x >> 21);
    store_chunk_entities(chunkPos, update.y);
}

[numthreads(64, 1, 1)]
void copyEntityChunkInstances(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint dispatchIndex = (groupID.x + groupID.y * 32768) * 64 + localIndex;
    if (dispatchIndex >= entityChunkInstanceCount)
        return;
    
    entityChunkInstances[dispatchIndex] = entityChunkInstancesDynamic[dispatchIndex];
}

[numthreads(64, 1, 1)]
void copyEntityHistory(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint dispatchIndex = (groupID.x + groupID.y * 32768) * 64 + localIndex;
    if (dispatchIndex >= entityInstanceCount)
        return;
    
    entityInstancesHistory[taaIndex * 16384 + dispatchIndex] = entityHistoryDynamic[dispatchIndex];
}
