// Ported from NAADF (TU Wien, 2026), MIT. See docs/licenses/naadf.txt.
#include "../common/common.slang"

struct VertexShaderOutput
{
    float4 Position : SV_POSITION;
};

[vk::binding(48, 0)] StructuredBuffer<float4> sampleAccumulated;

[vk::binding(59, 0)] StructuredBuffer<uint> rayStepCount;

int screenWidth, screenHeight;
uint outputWidth, outputHeight;
float exposure, toneMappingFac;
bool showRayStep;


VertexShaderOutput MainVS(uint vertex_id : SV_VertexID)
{
    VertexShaderOutput output;
    float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.Position = float4(position * 2.0f - 1.0f, 0.0f, 1.0f);
    return output;
}

float4 MainPS(VertexShaderOutput input) : SV_Target
{
    int2 pixelPos = min(int2(input.Position.xy * float2(screenWidth, screenHeight) / float2(max(1u, outputWidth), max(1u, outputHeight))), int2(screenWidth - 1, screenHeight - 1));
    int pixelIndex = pixelPos.x + pixelPos.y * screenWidth;
    
    float4 curSamples = sampleAccumulated[pixelIndex];
    
    float3 curColor = curSamples.rgb;
    
    if (showRayStep)
    {
        uint raySteps = rayStepCount[pixelIndex];
        float intensity = raySteps * 0.01f;
        curColor = float3(intensity, intensity, intensity);
    }
    
#ifdef HDR
    curColor /= 10.0f;
#endif // HDR
    
    // Tone mapping
    float luminace = curColor.r * 0.2126 + curColor.g * 0.7152 + curColor.b * 0.0722;
    float3 tv = curColor / (toneMappingFac + curColor);
    float3 colorNormalized = lerp(curColor / (exposure + luminace), tv, tv);
    
#ifdef HDR
    colorNormalized *= 10.0f;
#endif // HDR
    
    return float4(colorNormalized, 1);
}
