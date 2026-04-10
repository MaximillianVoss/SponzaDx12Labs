#include "GBuffer.h"

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr DXGI_FORMAT kAlbedoFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    constexpr DXGI_FORMAT kNormalFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    constexpr DXGI_FORMAT kDepthSrvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
}

void GBuffer::Initialize(ID3D12Device* device)
{
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = BufferCount;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(mRtvHeap.ReleaseAndGetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
    srvDesc.NumDescriptors = BufferCount + 1;
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(mSrvHeap.ReleaseAndGetAddressOf())));
}

void GBuffer::OnResize(ID3D12Device* device, UINT width, UINT height, ID3D12Resource* depthStencilBuffer)
{
    mWidth = (std::max)(1u, width);
    mHeight = (std::max)(1u, height);
    BuildResources(device, mWidth, mHeight);
    BuildDescriptors(device, depthStencilBuffer);
}

void GBuffer::TransitionToRenderTargets(ID3D12GraphicsCommandList* commandList)
{
    std::array<CD3DX12_RESOURCE_BARRIER, BufferCount> barriers =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(mBuffers[0].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        CD3DX12_RESOURCE_BARRIER::Transition(mBuffers[1].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
    };
    commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void GBuffer::TransitionToShaderResources(ID3D12GraphicsCommandList* commandList)
{
    std::array<CD3DX12_RESOURCE_BARRIER, BufferCount> barriers =
    {
        CD3DX12_RESOURCE_BARRIER::Transition(mBuffers[0].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        CD3DX12_RESOURCE_BARRIER::Transition(mBuffers[1].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    };
    commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void GBuffer::Clear(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
    const float albedoClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float normalClear[4] = { 0.5f, 0.5f, 1.0f, 1.0f };

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    commandList->ClearRenderTargetView(rtv, albedoClear, 0, nullptr);
    rtv.Offset(1, mRtvDescriptorSize);
    commandList->ClearRenderTargetView(rtv, normalClear, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
}

void GBuffer::SetAsRenderTarget(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
{
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, BufferCount> rtvs =
    {
        mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), 1, static_cast<INT>(mRtvDescriptorSize))
    };

    commandList->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.data(), false, &dsvHandle);
}

ID3D12DescriptorHeap* GBuffer::SrvHeap() const
{
    return mSrvHeap.Get();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::AlbedoSrv() const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvHeap->GetGPUDescriptorHandleForHeapStart(), 0, static_cast<INT>(mSrvDescriptorSize));
}

CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::NormalSrv() const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvHeap->GetGPUDescriptorHandleForHeapStart(), 1, static_cast<INT>(mSrvDescriptorSize));
}

CD3DX12_GPU_DESCRIPTOR_HANDLE GBuffer::DepthSrv() const
{
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvHeap->GetGPUDescriptorHandleForHeapStart(), 2, static_cast<INT>(mSrvDescriptorSize));
}

DXGI_FORMAT GBuffer::AlbedoFormat() const
{
    return kAlbedoFormat;
}

DXGI_FORMAT GBuffer::NormalFormat() const
{
    return kNormalFormat;
}

void GBuffer::BuildResources(ID3D12Device* device, UINT width, UINT height)
{
    for(ComPtr<ID3D12Resource>& buffer : mBuffers)
    {
        buffer.Reset();
    }

    const DXGI_FORMAT formats[BufferCount] = { kAlbedoFormat, kNormalFormat };
    for(UINT i = 0; i < BufferCount; ++i)
    {
        auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
            formats[i],
            width,
            height,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = formats[i];
        clearValue.Color[0] = i == 0 ? 0.0f : 0.5f;
        clearValue.Color[1] = i == 0 ? 0.0f : 0.5f;
        clearValue.Color[2] = i == 0 ? 0.0f : 1.0f;
        clearValue.Color[3] = i == 0 ? 0.0f : 1.0f;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(mBuffers[i].ReleaseAndGetAddressOf())));
    }
}

void GBuffer::BuildDescriptors(ID3D12Device* device, ID3D12Resource* depthStencilBuffer)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(mSrvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    for(UINT i = 0; i < BufferCount; ++i)
    {
        rtvDesc.Format = i == 0 ? kAlbedoFormat : kNormalFormat;
        device->CreateRenderTargetView(mBuffers[i].Get(), &rtvDesc, rtvHandle);

        srvDesc.Format = rtvDesc.Format;
        device->CreateShaderResourceView(mBuffers[i].Get(), &srvDesc, srvHandle);

        rtvHandle.Offset(1, mRtvDescriptorSize);
        srvHandle.Offset(1, mSrvDescriptorSize);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Format = kDepthSrvFormat;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Texture2D.MostDetailedMip = 0;
    depthSrvDesc.Texture2D.MipLevels = 1;
    depthSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    device->CreateShaderResourceView(depthStencilBuffer, &depthSrvDesc, srvHandle);
}
