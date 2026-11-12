// Ported from NAADF (TU Wien, 2026), MIT. See docs/licenses/naadf.txt.
uint offsetSrc, count, offsetDst;
[vk::binding(50, 0)] StructuredBuffer<uint> srcData;
[vk::binding(21, 0)] RWStructuredBuffer<uint> dstData;

[numthreads(64, 1, 1)]
void copyData(uint3 localID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint localIndex : SV_GroupIndex, uint3 globalID : SV_DispatchThreadID)
{
    if (globalID.x >= count)
        return;
    dstData[offsetDst + globalID.x] = srcData[offsetSrc + globalID.x];
}
