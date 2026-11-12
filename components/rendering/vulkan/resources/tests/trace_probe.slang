// Diagnostic entry point: execute the production NAADF traversal on supplied rays.
#include "render/ray_tracing.slang"

[vk::binding(57, 0)] StructuredBuffer<float4> test_rays;
[vk::binding(58, 0)] RWStructuredBuffer<uint4> test_results;
uint ray_count;

[numthreads(64, 1, 1)]
void trace_probe(uint3 index : SV_DispatchThreadID)
{
    if (index.x >= ray_count)
    {
        return;
    }
    float3 origin = test_rays[index.x * 2].xyz;
    float3 direction = normalize(test_rays[index.x * 2 + 1].xyz);
    int3 integer_origin = int3(floor(origin));
    RayResult result;
    bool hit = shootRay(integer_origin, origin - integer_origin, direction, 2000, result);
    test_results[index.x * 2] = uint4(hit ? 1 : 0, hit ? result.type : 0,
                                     asuint(result.length), asuint(result.entity));
    test_results[index.x * 2 + 1] = hit ? uint4(asuint(result.voxelPos), result.normalComp)
                                       : uint4(0, 0, 0, 0);
}
