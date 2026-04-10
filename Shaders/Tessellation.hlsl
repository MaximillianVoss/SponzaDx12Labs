#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 4
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 1
#endif

#include "Common.hlsl"

struct HullInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct HullOutput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct PatchTess
{
    float EdgeTess[4] : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};

struct DomainOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC : TEXCOORD;
};

HullInput VS(HullInput input)
{
    return input;
}

PatchTess ConstantHS(InputPatch<HullInput, 4> patch, uint patchId : SV_PrimitiveID)
{
    PatchTess tess = (PatchTess)0.0f;

    float3 center = 0.25f * (patch[0].PosL + patch[1].PosL + patch[2].PosL + patch[3].PosL);
    float3 centerW = mul(float4(center, 1.0f), gWorld).xyz;
    float distanceToEye = distance(centerW, gEyePosW);
    float tessFactor = lerp(12.0f, 2.0f, saturate((distanceToEye - 5.0f) / 60.0f));

    [unroll]
    for(int i = 0; i < 4; ++i)
    {
        tess.EdgeTess[i] = tessFactor;
    }

    tess.InsideTess[0] = tessFactor;
    tess.InsideTess[1] = tessFactor;
    return tess;
}

[domain("quad")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("ConstantHS")]
HullOutput HS(InputPatch<HullInput, 4> patch, uint controlPointId : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)
{
    HullOutput output;
    output.PosL = patch[controlPointId].PosL;
    output.NormalL = patch[controlPointId].NormalL;
    output.TexC = patch[controlPointId].TexC;
    output.TangentU = patch[controlPointId].TangentU;
    return output;
}

float3 BilinearFloat3(float3 c00, float3 c10, float3 c01, float3 c11, float2 uv)
{
    return lerp(lerp(c00, c10, uv.x), lerp(c01, c11, uv.x), uv.y);
}

float2 BilinearFloat2(float2 c00, float2 c10, float2 c01, float2 c11, float2 uv)
{
    return lerp(lerp(c00, c10, uv.x), lerp(c01, c11, uv.x), uv.y);
}

[domain("quad")]
DomainOutput DS(PatchTess patchTess, float2 uv : SV_DomainLocation, const OutputPatch<HullOutput, 4> patch)
{
    DomainOutput output = (DomainOutput)0.0f;
    MaterialData matData = gMaterialData[gMaterialIndex];

    float3 posL = BilinearFloat3(patch[0].PosL, patch[1].PosL, patch[2].PosL, patch[3].PosL, uv);
    float3 normalL = normalize(BilinearFloat3(patch[0].NormalL, patch[1].NormalL, patch[2].NormalL, patch[3].NormalL, uv));
    float3 tangentL = normalize(BilinearFloat3(patch[0].TangentU, patch[1].TangentU, patch[2].TangentU, patch[3].TangentU, uv));
    float2 texC = BilinearFloat2(patch[0].TexC, patch[1].TexC, patch[2].TexC, patch[3].TexC, uv);

    float4 texCoords = mul(float4(texC, 0.0f, 1.0f), gTexTransform);
    output.TexC = mul(texCoords, matData.MatTransform).xy;

    float displacement = gTextureMaps[matData.NormalMapIndex].SampleLevel(gsamAnisotropicWrap, output.TexC, 0.0f).a;
    posL += normalL * ((displacement - 0.5f) * 0.5f);

    float4 posW = mul(float4(posL, 1.0f), gWorld);
    output.PosW = posW.xyz;
    output.PosH = mul(posW, gViewProj);
    output.NormalW = mul(normalL, (float3x3)gWorld);
    output.TangentW = mul(tangentL, (float3x3)gWorld);
    return output;
}

float4 PS(DomainOutput pin) : SV_Target
{
    MaterialData matData = gMaterialData[gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    diffuseAlbedo *= gTextureMaps[matData.DiffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);

    float4 normalMapSample = gTextureMaps[matData.NormalMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
    float3 bumpedNormalW = NormalSampleToWorldSpace(normalMapSample.rgb, normalize(pin.NormalW), pin.TangentW);

    float3 toEyeW = normalize(gEyePosW - pin.PosW);
    float4 ambient = gAmbientLight * diffuseAlbedo;
    Material mat = { diffuseAlbedo, matData.FresnelR0, 1.0f - matData.Roughness };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW, normalize(bumpedNormalW), toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;
    litColor.a = diffuseAlbedo.a;
    return litColor;
}
