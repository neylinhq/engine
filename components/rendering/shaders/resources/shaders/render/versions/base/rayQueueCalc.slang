// Ported from NAADF (TU Wien, 2026), MIT. See docs/licenses/naadf.txt.

#include "../../common/common.slang"

[vk::binding(29, 0)] StructuredBuffer<uint4> firstHitData;
[vk::binding(46, 0)] RWStructuredBuffer<uint> pixelsToRender;
[vk::binding(52, 0)] StructuredBuffer<uint2> taaSampleAccum;
[vk::binding(39, 0)] RWByteAddressBuffer groupCount;

uint screenWidth, screenHeight, frameIndex;
bool skipSamples;

bool shouldRay(uint2 pos, float accum)
{
    if (!skipSamples)
        return true;
    
    float fac = accum / 2.0f;
    uint modSize = round(clamp(fac * 2, 0, 3) + 1);
    return ((frameIndex * 4 + pos.x + pos.y) % modSize) == 0;

}

[numthreads(64, 1, 1)]
void calcRayQueue(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint ID = globalID.x;
    
    uint2 pixelPos = uint2(ID % screenWidth, ID / screenWidth);
    bool shouldAdd = false;
    if (ID < screenWidth * screenHeight)
    {
        shouldAdd = (firstHitData[ID].z & 0x7FFF) != 0 && shouldRay(pixelPos, f16tof32(taaSampleAccum[ID].x & 0xFFFF));
    }
    int index = addToCounterAddressBuffer(groupCount, localIndex, shouldAdd);
    
    if (shouldAdd)
        pixelsToRender[index] = pixelPos.x | (pixelPos.y << 16);
}

[numthreads(1, 1, 1)]
void calcRayQueueStore(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    uint groupCountValue = groupCount.Load(0);
    groupCount.Store(12, groupCountValue);
    groupCount.Store(0, (groupCountValue + 63) / 64);
}
