#include "SponzaLabsApp.h"
#include "Common/GeometryGenerator.h"
#include "TgaTextureLoader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    constexpr UINT kTextureRegisterCount = 128;

    const D3D_SHADER_MACRO gLightingDefines[] =
    {
        "NUM_DIR_LIGHTS", "1",
        "NUM_POINT_LIGHTS", "4",
        "NUM_SPOT_LIGHTS", "1",
        nullptr, nullptr
    };

    const D3D_SHADER_MACRO gAlphaLightingDefines[] =
    {
        "NUM_DIR_LIGHTS", "1",
        "NUM_POINT_LIGHTS", "4",
        "NUM_SPOT_LIGHTS", "1",
        "ALPHA_TEST", "1",
        nullptr, nullptr
    };

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    }

    bool ContainsInsensitive(const std::string& haystack, const std::string& needle)
    {
        return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
    }

    std::string WideToUtf8(const std::wstring& value)
    {
        if(value.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(size > 0 ? size : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), size, nullptr, nullptr);
        if(!result.empty())
        {
            result.pop_back();
        }
        return result;
    }

    void CreateSolidColorTexture(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        Texture& texture,
        const std::array<std::uint8_t, 4>& rgba)
    {
        const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
        auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, 1, 1, 1, 1);

        CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(device->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(texture.Resource.ReleaseAndGetAddressOf())));

        CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(GetRequiredIntermediateSize(texture.Resource.Get(), 0, 1));
        ThrowIfFailed(device->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &uploadBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(texture.UploadHeap.ReleaseAndGetAddressOf())));

        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = rgba.data();
        subresourceData.RowPitch = 4;
        subresourceData.SlicePitch = 4;

        UpdateSubresources(commandList, texture.Resource.Get(), texture.UploadHeap.Get(), 0, 0, 1, &subresourceData);

        auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
            texture.Resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_GENERIC_READ);
        commandList->ResourceBarrier(1, &transition);
    }

    BoundingBox TransformBoundingBox(const BoundingBox& bounds, CXMMATRIX world)
    {
        BoundingBox transformed;
        bounds.Transform(transformed, world);
        return transformed;
    }
}

void SponzaLabsApp::LoadSceneData()
{
    const std::filesystem::path scenePath = d3dUtil::ResolveProjectPath(std::filesystem::path(L"Assets") / L"Sponza-master" / L"sponza.obj");
    mSceneData = ObjModelLoader::LoadFromFile(scenePath);
}

void SponzaLabsApp::LoadTextures()
{
    auto addGeneratedTexture = [&](const std::string& name, const std::array<std::uint8_t, 4>& rgba)
    {
        auto texture = std::make_unique<Texture>();
        texture->Name = name;
        texture->Filename = AnsiToWString("<generated>");
        CreateSolidColorTexture(md3dDevice.Get(), mCommandList.Get(), *texture, rgba);
        mTextureHeapOrder.push_back(name);
        mTextures[name] = std::move(texture);
    };

    addGeneratedTexture("defaultDiffuseMap", { 255, 255, 255, 255 });
    addGeneratedTexture("defaultNormalMap", { 128, 128, 255, 255 });

    std::set<std::wstring> uniqueTexturePaths;
    for(const ObjMaterialInfo& material : mSceneData.Materials)
    {
        if(!material.DiffuseTexturePath.empty())
        {
            uniqueTexturePaths.insert(material.DiffuseTexturePath);
        }
        if(!material.NormalTexturePath.empty())
        {
            uniqueTexturePaths.insert(material.NormalTexturePath);
        }
    }

    for(const std::wstring& texturePath : uniqueTexturePaths)
    {
        auto texture = std::make_unique<Texture>();
        texture->Filename = texturePath;
        texture->Name = WideToUtf8(texturePath);
        TgaTextureLoader::LoadTextureFromFile(md3dDevice.Get(), mCommandList.Get(), *texture);
        mTextureHeapOrder.push_back(texture->Name);
        mTextures[texture->Name] = std::move(texture);
    }
}

void SponzaLabsApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE textureTable;
    textureTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kTextureRegisterCount, 0, 0);

    CD3DX12_ROOT_PARAMETER rootParameters[4];
    rootParameters[0].InitAsConstantBufferView(0);
    rootParameters[1].InitAsConstantBufferView(1);
    rootParameters[2].InitAsShaderResourceView(0, 1);
    rootParameters[3].InitAsDescriptorTable(1, &textureTable);

    const auto staticSamplers = GetStaticSamplers();
    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
        _countof(rootParameters),
        rootParameters,
        static_cast<UINT>(staticSamplers.size()),
        staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSignature.GetAddressOf(),
        errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        OutputDebugStringA(reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSignature->GetBufferPointer(),
        serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.ReleaseAndGetAddressOf())));
}

void SponzaLabsApp::BuildLightingRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE gbufferTable;
    gbufferTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);

    CD3DX12_ROOT_PARAMETER rootParameters[2];
    rootParameters[0].InitAsConstantBufferView(0);
    rootParameters[1].InitAsDescriptorTable(1, &gbufferTable, D3D12_SHADER_VISIBILITY_PIXEL);

    const auto staticSamplers = GetStaticSamplers();
    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(
        _countof(rootParameters),
        rootParameters,
        static_cast<UINT>(staticSamplers.size()),
        staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSignature;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(
        &rootSignatureDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSignature.GetAddressOf(),
        errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        OutputDebugStringA(reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSignature->GetBufferPointer(),
        serializedRootSignature->GetBufferSize(),
        IID_PPV_ARGS(mLightingRootSignature.ReleaseAndGetAddressOf())));
}

void SponzaLabsApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
    descriptorHeapDesc.NumDescriptors = kTextureRegisterCount;
    descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(mSrvDescriptorHeap.ReleaseAndGetAddressOf())));

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    for(UINT index = 0; index < mTextureHeapOrder.size(); ++index)
    {
        Texture* texture = mTextures[mTextureHeapOrder[index]].get();
        srvDesc.Format = texture->Resource->GetDesc().Format;
        srvDesc.Texture2D.MipLevels = texture->Resource->GetDesc().MipLevels;
        md3dDevice->CreateShaderResourceView(texture->Resource.Get(), &srvDesc, handle);
        mTextureSrvLookup[texture->Name] = index;
        handle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    Texture* fallbackTexture = mTextures["defaultDiffuseMap"].get();
    srvDesc.Format = fallbackTexture->Resource->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = fallbackTexture->Resource->GetDesc().MipLevels;
    for(UINT index = static_cast<UINT>(mTextureHeapOrder.size()); index < kTextureRegisterCount; ++index)
    {
        md3dDevice->CreateShaderResourceView(fallbackTexture->Resource.Get(), &srvDesc, handle);
        handle.Offset(1, mCbvSrvUavDescriptorSize);
    }
}

void SponzaLabsApp::BuildShadersAndInputLayout()
{
    mShaders["forwardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", gLightingDefines, "VS", "vs_5_1");
    mShaders["forwardOpaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", gLightingDefines, "PS", "ps_5_1");
    mShaders["forwardAlphaPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", gAlphaLightingDefines, "PS", "ps_5_1");

    mShaders["gbufferVS"] = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["gbufferOpaquePS"] = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", nullptr, "PS", "ps_5_1");
    mShaders["gbufferAlphaPS"] = d3dUtil::CompileShader(L"Shaders\\GBuffer.hlsl", gAlphaLightingDefines, "PS", "ps_5_1");

    mShaders["deferredVS"] = d3dUtil::CompileShader(L"Shaders\\DeferredLighting.hlsl", gLightingDefines, "VS", "vs_5_1");
    mShaders["deferredPS"] = d3dUtil::CompileShader(L"Shaders\\DeferredLighting.hlsl", gLightingDefines, "PS", "ps_5_1");

    mShaders["tessVS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", gLightingDefines, "VS", "vs_5_1");
    mShaders["tessHS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", gLightingDefines, "HS", "hs_5_1");
    mShaders["tessDS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", gLightingDefines, "DS", "ds_5_1");
    mShaders["tessPS"] = d3dUtil::CompileShader(L"Shaders\\Tessellation.hlsl", gLightingDefines, "PS", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void SponzaLabsApp::BuildSceneGeometry()
{
    {
        auto geometry = std::make_unique<MeshGeometry>();
        geometry->Name = "sponzaGeo";

        const UINT vertexBufferByteSize = static_cast<UINT>(mSceneData.Vertices.size() * sizeof(Vertex));
        const UINT indexBufferByteSize = static_cast<UINT>(mSceneData.Indices.size() * sizeof(std::uint32_t));

        ThrowIfFailed(D3DCreateBlob(vertexBufferByteSize, geometry->VertexBufferCPU.GetAddressOf()));
        std::memcpy(geometry->VertexBufferCPU->GetBufferPointer(), mSceneData.Vertices.data(), vertexBufferByteSize);

        ThrowIfFailed(D3DCreateBlob(indexBufferByteSize, geometry->IndexBufferCPU.GetAddressOf()));
        std::memcpy(geometry->IndexBufferCPU->GetBufferPointer(), mSceneData.Indices.data(), indexBufferByteSize);

        geometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), mSceneData.Vertices.data(), vertexBufferByteSize, geometry->VertexBufferUploader);
        geometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), mSceneData.Indices.data(), indexBufferByteSize, geometry->IndexBufferUploader);

        geometry->VertexByteStride = sizeof(Vertex);
        geometry->VertexBufferByteSize = vertexBufferByteSize;
        geometry->IndexFormat = DXGI_FORMAT_R32_UINT;
        geometry->IndexBufferByteSize = indexBufferByteSize;

        for(const ObjSubmeshInfo& submesh : mSceneData.Submeshes)
        {
            SubmeshGeometry drawArgs;
            drawArgs.IndexCount = submesh.IndexCount;
            drawArgs.StartIndexLocation = submesh.StartIndexLocation;
            drawArgs.BaseVertexLocation = submesh.BaseVertexLocation;
            drawArgs.Bounds = submesh.Bounds;
            geometry->DrawArgs[submesh.Name] = drawArgs;
        }

        mGeometries[geometry->Name] = std::move(geometry);
    }

    {
        GeometryGenerator geometryGenerator;
        GeometryGenerator::MeshData box = geometryGenerator.CreateBox(1.0f, 1.0f, 1.0f, 0);

        std::vector<Vertex> vertices(box.Vertices.size());
        for(size_t i = 0; i < box.Vertices.size(); ++i)
        {
            vertices[i].Pos = box.Vertices[i].Position;
            vertices[i].Normal = box.Vertices[i].Normal;
            vertices[i].TexC = box.Vertices[i].TexC;
            vertices[i].TangentU = box.Vertices[i].TangentU;
        }

        auto geometry = std::make_unique<MeshGeometry>();
        geometry->Name = "scatterBoxGeo";

        const UINT vertexBufferByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
        const UINT indexBufferByteSize = static_cast<UINT>(box.Indices32.size() * sizeof(std::uint32_t));

        ThrowIfFailed(D3DCreateBlob(vertexBufferByteSize, geometry->VertexBufferCPU.GetAddressOf()));
        std::memcpy(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertexBufferByteSize);

        ThrowIfFailed(D3DCreateBlob(indexBufferByteSize, geometry->IndexBufferCPU.GetAddressOf()));
        std::memcpy(geometry->IndexBufferCPU->GetBufferPointer(), box.Indices32.data(), indexBufferByteSize);

        geometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vertices.data(), vertexBufferByteSize, geometry->VertexBufferUploader);
        geometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), box.Indices32.data(), indexBufferByteSize, geometry->IndexBufferUploader);

        geometry->VertexByteStride = sizeof(Vertex);
        geometry->VertexBufferByteSize = vertexBufferByteSize;
        geometry->IndexFormat = DXGI_FORMAT_R32_UINT;
        geometry->IndexBufferByteSize = indexBufferByteSize;

        SubmeshGeometry submesh;
        submesh.IndexCount = static_cast<UINT>(box.Indices32.size());
        submesh.StartIndexLocation = 0;
        submesh.BaseVertexLocation = 0;
        BoundingBox::CreateFromPoints(submesh.Bounds, static_cast<UINT>(vertices.size()), &vertices[0].Pos, sizeof(Vertex));
        geometry->DrawArgs["box"] = submesh;

        mGeometries[geometry->Name] = std::move(geometry);
    }

    {
        std::array<Vertex, 4> vertices =
        {
            Vertex{ XMFLOAT3(-4.0f, 0.4f, -4.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
            Vertex{ XMFLOAT3( 4.0f, 0.4f, -4.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
            Vertex{ XMFLOAT3(-4.0f, 0.4f,  4.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
            Vertex{ XMFLOAT3( 4.0f, 0.4f,  4.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f), XMFLOAT3(1.0f, 0.0f, 0.0f) }
        };
        const std::array<std::uint32_t, 4> indices = { 0, 1, 2, 3 };

        auto geometry = std::make_unique<MeshGeometry>();
        geometry->Name = "tessPatchGeo";

        const UINT vertexBufferByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
        const UINT indexBufferByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

        ThrowIfFailed(D3DCreateBlob(vertexBufferByteSize, geometry->VertexBufferCPU.GetAddressOf()));
        std::memcpy(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertexBufferByteSize);

        ThrowIfFailed(D3DCreateBlob(indexBufferByteSize, geometry->IndexBufferCPU.GetAddressOf()));
        std::memcpy(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), indexBufferByteSize);

        geometry->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), vertices.data(), vertexBufferByteSize, geometry->VertexBufferUploader);
        geometry->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(), indices.data(), indexBufferByteSize, geometry->IndexBufferUploader);

        geometry->VertexByteStride = sizeof(Vertex);
        geometry->VertexBufferByteSize = vertexBufferByteSize;
        geometry->IndexFormat = DXGI_FORMAT_R32_UINT;
        geometry->IndexBufferByteSize = indexBufferByteSize;

        SubmeshGeometry submesh;
        submesh.IndexCount = 4;
        submesh.StartIndexLocation = 0;
        submesh.BaseVertexLocation = 0;
        BoundingBox::CreateFromPoints(submesh.Bounds, static_cast<UINT>(vertices.size()), &vertices[0].Pos, sizeof(Vertex));
        geometry->DrawArgs["patch"] = submesh;

        mGeometries[geometry->Name] = std::move(geometry);
    }
}

void SponzaLabsApp::BuildMaterials()
{
    UINT materialIndex = 0;
    for(const ObjMaterialInfo& materialInfo : mSceneData.Materials)
    {
        auto material = std::make_unique<Material>();
        material->Name = materialInfo.Name;
        material->MatCBIndex = materialIndex++;
        material->DiffuseSrvHeapIndex = ResolveTextureIndex(materialInfo.DiffuseTexturePath, "defaultDiffuseMap");
        material->NormalSrvHeapIndex = ResolveTextureIndex(materialInfo.NormalTexturePath, "defaultNormalMap");
        material->DiffuseAlbedo = materialInfo.DiffuseAlbedo;
        material->FresnelR0 = materialInfo.FresnelR0;
        material->Roughness = materialInfo.Roughness;
        mMaterials[material->Name] = std::move(material);
    }

    Material* sourceFloor = FindMaterialContaining("floor");
    if(sourceFloor == nullptr)
    {
        sourceFloor = mMaterials.begin()->second.get();
    }

    auto animatedFloor = std::make_unique<Material>();
    animatedFloor->Name = "animatedFloor";
    animatedFloor->MatCBIndex = materialIndex++;
    animatedFloor->DiffuseSrvHeapIndex = sourceFloor->DiffuseSrvHeapIndex;
    animatedFloor->NormalSrvHeapIndex = sourceFloor->NormalSrvHeapIndex;
    animatedFloor->DiffuseAlbedo = sourceFloor->DiffuseAlbedo;
    animatedFloor->FresnelR0 = XMFLOAT3(0.08f, 0.08f, 0.08f);
    animatedFloor->Roughness = 0.3f;
    mMaterials[animatedFloor->Name] = std::move(animatedFloor);

    Material* sourceBricks = FindMaterialContaining("bricks");
    if(sourceBricks == nullptr)
    {
        sourceBricks = mMaterials.begin()->second.get();
    }

    auto scatterBricks = std::make_unique<Material>();
    scatterBricks->Name = "scatterBricks";
    scatterBricks->MatCBIndex = materialIndex++;
    scatterBricks->DiffuseSrvHeapIndex = sourceBricks->DiffuseSrvHeapIndex;
    scatterBricks->NormalSrvHeapIndex = sourceBricks->NormalSrvHeapIndex;
    scatterBricks->DiffuseAlbedo = XMFLOAT4(0.95f, 0.95f, 0.95f, 1.0f);
    scatterBricks->FresnelR0 = XMFLOAT3(0.04f, 0.04f, 0.04f);
    scatterBricks->Roughness = 0.45f;
    mMaterials[scatterBricks->Name] = std::move(scatterBricks);
}

void SponzaLabsApp::BuildRenderItems()
{
    UINT objectIndex = 0;
    MeshGeometry* sponzaGeometry = mGeometries["sponzaGeo"].get();

    for(const ObjSubmeshInfo& submesh : mSceneData.Submeshes)
    {
        auto renderItem = std::make_unique<RenderItem>();
        renderItem->ObjCBIndex = objectIndex++;
        renderItem->Mat = FindMaterialByName(submesh.MaterialName);
        renderItem->Geo = sponzaGeometry;
        renderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        renderItem->IndexCount = submesh.IndexCount;
        renderItem->StartIndexLocation = submesh.StartIndexLocation;
        renderItem->BaseVertexLocation = submesh.BaseVertexLocation;
        renderItem->Bounds = submesh.Bounds;

        RenderLayer layer = RenderLayer::Opaque;
        for(const ObjMaterialInfo& materialInfo : mSceneData.Materials)
        {
            if(materialInfo.Name == submesh.MaterialName && materialInfo.AlphaTested)
            {
                layer = RenderLayer::AlphaTested;
                break;
            }
        }

        mRitemLayer[static_cast<int>(layer)].push_back(renderItem.get());
        mAllRitems.push_back(std::move(renderItem));
    }

    auto tessellationItem = std::make_unique<RenderItem>();
    tessellationItem->ObjCBIndex = objectIndex++;
    tessellationItem->Mat = FindMaterialByName("animatedFloor");
    tessellationItem->Geo = mGeometries["tessPatchGeo"].get();
    tessellationItem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST;
    tessellationItem->IndexCount = mGeometries["tessPatchGeo"]->DrawArgs["patch"].IndexCount;
    tessellationItem->StartIndexLocation = mGeometries["tessPatchGeo"]->DrawArgs["patch"].StartIndexLocation;
    tessellationItem->BaseVertexLocation = mGeometries["tessPatchGeo"]->DrawArgs["patch"].BaseVertexLocation;
    tessellationItem->Bounds = mGeometries["tessPatchGeo"]->DrawArgs["patch"].Bounds;
    mRitemLayer[static_cast<int>(RenderLayer::Tessellation)].push_back(tessellationItem.get());
    mAllRitems.push_back(std::move(tessellationItem));
}

void SponzaLabsApp::BuildScatterItems()
{
    const SubmeshGeometry& boxSubmesh = mGeometries["scatterBoxGeo"]->DrawArgs["box"];
    MeshGeometry* boxGeometry = mGeometries["scatterBoxGeo"].get();
    Material* scatterMaterial = FindMaterialByName("scatterBricks");

    UINT nextObjectIndex = static_cast<UINT>(mAllRitems.size());
    const int gridHalfExtent = 12;
    const float spacing = 3.2f;

    for(int z = -gridHalfExtent; z <= gridHalfExtent; ++z)
    {
        for(int x = -gridHalfExtent; x <= gridHalfExtent; ++x)
        {
            auto renderItem = std::make_unique<RenderItem>();
            renderItem->ObjCBIndex = nextObjectIndex++;
            renderItem->Mat = scatterMaterial;
            renderItem->Geo = boxGeometry;
            renderItem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            renderItem->IndexCount = boxSubmesh.IndexCount;
            renderItem->StartIndexLocation = boxSubmesh.StartIndexLocation;
            renderItem->BaseVertexLocation = boxSubmesh.BaseVertexLocation;
            renderItem->ParticipatesInCulling = true;

            const float scaleY = 0.5f + (static_cast<float>((x + z + 40) % 4) * 0.35f);
            const float translationX = x * spacing;
            const float translationZ = z * spacing;
            const float translationY = 0.5f * scaleY;

            const XMMATRIX world = XMMatrixScaling(0.75f, scaleY, 0.75f) * XMMatrixTranslation(translationX, translationY, translationZ);
            XMStoreFloat4x4(&renderItem->World, world);
            renderItem->Bounds = TransformBoundingBox(boxSubmesh.Bounds, world);

            mScatterItems.push_back(renderItem.get());
            mRitemLayer[static_cast<int>(RenderLayer::Scatter)].push_back(renderItem.get());
            mAllRitems.push_back(std::move(renderItem));
        }
    }
}

void SponzaLabsApp::BuildOctree()
{
    if(mScatterItems.empty())
    {
        return;
    }

    std::vector<SceneOctree::Entry> entries;
    entries.reserve(mScatterItems.size());
    std::vector<XMFLOAT3> corners;
    corners.reserve(mScatterItems.size() * 2);

    for(size_t i = 0; i < mScatterItems.size(); ++i)
    {
        entries.push_back({ mScatterItems[i]->Bounds, i });

        corners.push_back(XMFLOAT3(
            mScatterItems[i]->Bounds.Center.x - mScatterItems[i]->Bounds.Extents.x,
            mScatterItems[i]->Bounds.Center.y - mScatterItems[i]->Bounds.Extents.y,
            mScatterItems[i]->Bounds.Center.z - mScatterItems[i]->Bounds.Extents.z));
        corners.push_back(XMFLOAT3(
            mScatterItems[i]->Bounds.Center.x + mScatterItems[i]->Bounds.Extents.x,
            mScatterItems[i]->Bounds.Center.y + mScatterItems[i]->Bounds.Extents.y,
            mScatterItems[i]->Bounds.Center.z + mScatterItems[i]->Bounds.Extents.z));
    }

    BoundingBox sceneBounds;
    BoundingBox::CreateFromPoints(sceneBounds, static_cast<UINT>(corners.size()), corners.data(), sizeof(XMFLOAT3));
    mScatterOctree.Build(sceneBounds, std::move(entries), 6, 32);
}

Material* SponzaLabsApp::FindMaterialByName(const std::string& name)
{
    auto found = mMaterials.find(name);
    return found != mMaterials.end() ? found->second.get() : nullptr;
}

Material* SponzaLabsApp::FindMaterialContaining(const std::string& token)
{
    for(auto& pair : mMaterials)
    {
        if(ContainsInsensitive(pair.first, token))
        {
            return pair.second.get();
        }
    }
    return nullptr;
}

int SponzaLabsApp::ResolveTextureIndex(const std::wstring& texturePath, const std::string& fallbackName) const
{
    if(!texturePath.empty())
    {
        const auto found = mTextureSrvLookup.find(WideToUtf8(texturePath));
        if(found != mTextureSrvLookup.end())
        {
            return static_cast<int>(found->second);
        }
    }

    const auto fallback = mTextureSrvLookup.find(fallbackName);
    return fallback != mTextureSrvLookup.end() ? static_cast<int>(fallback->second) : 0;
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> SponzaLabsApp::GetStaticSamplers() const
{
    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1,
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4,
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        0.0f,
        8);

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5,
        D3D12_FILTER_ANISOTROPIC,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        0.0f,
        8);

    return { pointWrap, pointClamp, linearWrap, linearClamp, anisotropicWrap, anisotropicClamp };
}
