// Exercises production helpers without adding runtime shader entry points.
#include "../../../shaders/resources/shaders/render/base/render_sample_refine.slang"
#include "../../../shaders/resources/shaders/render/atmosphere/atmosphere_precomputed.slang"

[vk::binding(57, 0)] RWStructuredBuffer<uint4> probeOutput;
[vk::binding(58, 0)] StructuredBuffer<float4> probeInputs;
uint probeMathMode;

[numthreads(1, 1, 1)]
void probeProjection(uint3 globalID : SV_DispatchThreadID)
{
    float3 pos = probeInputs[globalID.x].xyz;
    float3 ndc;
    bool ndcValid = getNdcProjection(pos, camMatrix, ndc);
    int2 screenPos, screenPosBucket;
    bool refinedValid = getRefinedScreenCoordinates(pos, screenPos, screenPosBucket);
    uint screenIndex = 0xFFFFFFFFu;
    uint bucketIndex = 0xFFFFFFFFu;
    if (refinedValid)
    {
        screenIndex = screenPos.x + screenPos.y * screenWidth;
        bucketIndex = screenPosBucket.x + screenPosBucket.y * globalIlumBucketSizeX;
    }
    probeOutput[globalID.x] = uint4(ndcValid, refinedValid, screenIndex, bucketIndex);
}

[numthreads(1, 1, 1)]
void probeAtmosphere(uint3 globalID : SV_DispatchThreadID)
{
    float3 absorption = float3(1, 1, 1);
    float3 light = float3(0, 0, 0);
    applyAtmosphere(float3(0, 0, 0), probeInputs[globalID.x].xyz, absorption, light);
    uint finite = all(isfinite(light)) && all(isfinite(absorption));
    probeOutput[globalID.x] = uint4(asuint(light), finite);
}

[numthreads(1, 1, 1)]
void probeMath(uint3 globalID : SV_DispatchThreadID)
{
    float4 input = probeInputs[globalID.x];
    if (probeMathMode == 1)
    {
        int mirrored = mirrorPixel(asint(input.x), asuint(input.y));
        probeOutput[globalID.x] = uint4(asuint(mirrored), 0, 0, 0);
        return;
    }
    float3 reflectance = getReflectanceFresnel(float3(input.x), input.y);
    float positive = gaussianF(input.z, input.w);
    float negative = gaussianF(-input.z, input.w);
    uint finite = all(isfinite(reflectance)) && isfinite(positive) && isfinite(negative);
    probeOutput[globalID.x] = uint4(asuint(reflectance.x), asuint(positive), asuint(negative), finite);
}
