#include "Common.hlsli"

Texture2D<float> SDFTexture      : register(t10);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 viewDir : TEXCOORD3;
};

[RootSignature(Renderer_RootSig)]
float4 main(VSOutput vsOutput) : SV_Target0
{
    return float4(SDFTexture.SampleLevel(defaultSampler, vsOutput.position.xy, 0).r, 0, 0, 1);
}