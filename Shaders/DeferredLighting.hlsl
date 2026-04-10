#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 4
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 1
#endif

#include "LightingUtil.hlsl"

Texture2D gAlbedoTexture : register(t0);
Texture2D gNormalTexture : register(t1);
Texture2D gDepthTexture : register(t2);

SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearClamp : register(s3);

cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    Light gLights[MaxLights];
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VertexOut VS(uint vertexId : SV_VertexID)
{
    VertexOut vout;

    float2 texcoords[3] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f)
    };

    float2 positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };

    vout.PosH = float4(positions[vertexId], 0.0f, 1.0f);
    vout.TexC = texcoords[vertexId];
    return vout;
}

float3 ReconstructWorldPosition(float2 texC, float depth)
{
    float2 ndcXY = float2(texC.x * 2.0f - 1.0f, (1.0f - texC.y) * 2.0f - 1.0f);
    float4 clipPos = float4(ndcXY, depth, 1.0f);
    float4 worldPos = mul(clipPos, gInvViewProj);
    return worldPos.xyz / worldPos.w;
}

float4 PS(VertexOut pin) : SV_Target
{
    float4 albedo = gAlbedoTexture.Sample(gsamLinearClamp, pin.TexC);
    if(albedo.a <= 0.001f)
    {
        discard;
    }

    float4 normalRoughness = gNormalTexture.Sample(gsamLinearClamp, pin.TexC);
    float depth = gDepthTexture.Sample(gsamPointClamp, pin.TexC).r;
    float3 normalW = normalize(normalRoughness.xyz * 2.0f - 1.0f);
    float roughness = saturate(normalRoughness.w);
    float3 worldPos = ReconstructWorldPosition(pin.TexC, depth);
    float3 toEye = normalize(gEyePosW - worldPos);

    float4 ambient = gAmbientLight * albedo;
    Material mat = { albedo, float3(0.08f, 0.08f, 0.08f), 1.0f - roughness };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, worldPos, normalW, toEye, shadowFactor);
    float4 litColor = ambient + directLight;
    litColor.a = 1.0f;
    return litColor;
}
