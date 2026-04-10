#include "TgaTextureLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "ThirdParty/stb_image.h"

using Microsoft::WRL::ComPtr;

namespace
{
    std::string WideToUtf8(const std::wstring& value)
    {
        if(value.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(size > 0 ? size - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), size, nullptr, nullptr);
        return utf8;
    }
}

void TgaTextureLoader::LoadTextureFromFile(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* commandList,
    Texture& texture)
{
    int width = 0;
    int height = 0;
    int channels = 0;

    const std::string utf8Path = WideToUtf8(texture.Filename);
    stbi_uc* pixels = stbi_load(utf8Path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if(pixels == nullptr)
    {
        throw std::runtime_error("Failed to load texture: " + utf8Path);
    }

    const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    const UINT64 rowPitch = static_cast<UINT64>(width) * 4;
    const UINT64 slicePitch = rowPitch * static_cast<UINT64>(height);

    auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        format,
        static_cast<UINT64>(width),
        static_cast<UINT>(height),
        1,
        1);

    CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(texture.Resource.ReleaseAndGetAddressOf())));

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Resource.Get(), 0, 1);

    CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
    auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(texture.UploadHeap.ReleaseAndGetAddressOf())));

    D3D12_SUBRESOURCE_DATA subresourceData = {};
    subresourceData.pData = pixels;
    subresourceData.RowPitch = static_cast<LONG_PTR>(rowPitch);
    subresourceData.SlicePitch = static_cast<LONG_PTR>(slicePitch);

    UpdateSubresources(commandList, texture.Resource.Get(), texture.UploadHeap.Get(), 0, 0, 1, &subresourceData);

    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Resource.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    commandList->ResourceBarrier(1, &transition);

    stbi_image_free(pixels);
}
