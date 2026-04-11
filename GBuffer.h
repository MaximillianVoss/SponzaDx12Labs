#pragma once

#include "Common/d3dUtil.h"

class GBuffer
{
public:
    static constexpr UINT BufferCount = 2;

    void Initialize(ID3D12Device* device);
    void OnResize(ID3D12Device* device, UINT width, UINT height, ID3D12Resource* depthStencilBuffer);

    void TransitionToRenderTargets(ID3D12GraphicsCommandList* commandList);
    void TransitionToShaderResources(ID3D12GraphicsCommandList* commandList);
    void Clear(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);
    void SetAsRenderTarget(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle);

    ID3D12DescriptorHeap* SrvHeap() const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE AlbedoSrv() const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE NormalSrv() const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE DepthSrv() const;

    DXGI_FORMAT AlbedoFormat() const;
    DXGI_FORMAT NormalFormat() const;

private:
    void BuildResources(ID3D12Device* device, UINT width, UINT height);
    void BuildDescriptors(ID3D12Device* device, ID3D12Resource* depthStencilBuffer);

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvHeap;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, BufferCount> mBuffers;

    UINT mWidth = 1;
    UINT mHeight = 1;
    UINT mRtvDescriptorSize = 0;
    UINT mSrvDescriptorSize = 0;
};
