#pragma once

#include "FrameResource.h"

#include <filesystem>

struct ObjMaterialInfo
{
    std::string Name;
    std::wstring DiffuseTexturePath;
    std::wstring NormalTexturePath;
    std::wstring DisplacementTexturePath;
    std::wstring AlphaTexturePath;
    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 FresnelR0 = { 0.04f, 0.04f, 0.04f };
    float Roughness = 0.45f;
    bool AlphaTested = false;
};

struct ObjSubmeshInfo
{
    std::string Name;
    std::string MaterialName;
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    INT BaseVertexLocation = 0;
    DirectX::BoundingBox Bounds;
};

struct ObjSceneData
{
    std::vector<Vertex> Vertices;
    std::vector<std::uint32_t> Indices;
    std::vector<ObjMaterialInfo> Materials;
    std::vector<ObjSubmeshInfo> Submeshes;
    DirectX::BoundingBox Bounds;
};

namespace ObjModelLoader
{
    ObjSceneData LoadFromFile(const std::filesystem::path& objPath);
}
