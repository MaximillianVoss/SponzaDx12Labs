#include "SponzaLabsApp.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    void TransitionResource(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
    {
        if(before == after)
        {
            return;
        }

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, before, after);
        commandList->ResourceBarrier(1, &barrier);
    }
}

void SponzaLabsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

    DrawForwardPass();

    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &transition);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* commandLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void SponzaLabsApp::DrawRenderItems(ID3D12GraphicsCommandList* commandList, const std::vector<RenderItem*>& renderItems)
{
    const UINT objectCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    ID3D12Resource* objectCB = mCurrFrameResource->ObjectCB->Resource();

    for(RenderItem* renderItem : renderItems)
    {
        const D3D12_VERTEX_BUFFER_VIEW vertexBufferView = renderItem->Geo->VertexBufferView();
        const D3D12_INDEX_BUFFER_VIEW indexBufferView = renderItem->Geo->IndexBufferView();
        commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
        commandList->IASetIndexBuffer(&indexBufferView);
        commandList->IASetPrimitiveTopology(renderItem->PrimitiveType);

        const D3D12_GPU_VIRTUAL_ADDRESS objectCBAddress =
            objectCB->GetGPUVirtualAddress() + static_cast<UINT64>(renderItem->ObjCBIndex) * objectCBByteSize;
        commandList->SetGraphicsRootConstantBufferView(0, objectCBAddress);
        commandList->DrawIndexedInstanced(renderItem->IndexCount, 1, renderItem->StartIndexLocation, renderItem->BaseVertexLocation, 0);
    }
}

void SponzaLabsApp::DrawForwardPass()
{
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    auto backBufferTransition = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &backBufferTransition);

    const float clearColor[] = { 0.06f, 0.08f, 0.11f, 1.0f };
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f,
        0,
        0,
        nullptr);

    auto currentBackBufferView = CurrentBackBufferView();
    auto depthStencilView = DepthStencilView();
    mCommandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    mCommandList->SetGraphicsRootConstantBufferView(1, mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootShaderResourceView(2, mCurrFrameResource->MaterialBuffer->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    mCommandList->SetPipelineState(mPSOs["forwardOpaque"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Opaque)]);

    mCommandList->SetPipelineState(mPSOs["forwardAlpha"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::AlphaTested)]);
}

void SponzaLabsApp::DrawDeferredPass()
{
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    auto backBufferTransition = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &backBufferTransition);

    TransitionResource(
        mCommandList.Get(),
        mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    mGBuffer.TransitionToRenderTargets(mCommandList.Get());
    mGBuffer.Clear(mCommandList.Get(), DepthStencilView());
    mGBuffer.SetAsRenderTarget(mCommandList.Get(), DepthStencilView());

    ID3D12DescriptorHeap* sceneDescriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(sceneDescriptorHeaps), sceneDescriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    mCommandList->SetGraphicsRootConstantBufferView(1, mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootShaderResourceView(2, mCurrFrameResource->MaterialBuffer->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    mCommandList->SetPipelineState(mPSOs["gbufferOpaque"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Opaque)]);
    DrawRenderItems(mCommandList.Get(), mVisibleScatterItems);

    mCommandList->SetPipelineState(mPSOs["gbufferAlpha"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::AlphaTested)]);

    mGBuffer.TransitionToShaderResources(mCommandList.Get());
    TransitionResource(
        mCommandList.Get(),
        mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    const float clearColor[] = { 0.03f, 0.04f, 0.06f, 1.0f };
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), clearColor, 0, nullptr);
    auto currentBackBufferView = CurrentBackBufferView();
    mCommandList->OMSetRenderTargets(1, &currentBackBufferView, true, nullptr);

    ID3D12DescriptorHeap* lightingDescriptorHeaps[] = { mGBuffer.SrvHeap() };
    mCommandList->SetDescriptorHeaps(_countof(lightingDescriptorHeaps), lightingDescriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mLightingRootSignature.Get());
    mCommandList->SetGraphicsRootConstantBufferView(0, mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootDescriptorTable(1, mGBuffer.AlbedoSrv());
    mCommandList->SetPipelineState(mPSOs["deferred"].Get());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList->DrawInstanced(3, 1, 0, 0);

    TransitionResource(
        mCommandList.Get(),
        mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);

    if(mEnableTessellation)
    {
        DrawTessellationPass();
    }
}

void SponzaLabsApp::DrawTessellationPass()
{
    auto currentBackBufferView = CurrentBackBufferView();
    auto depthStencilView = DepthStencilView();
    mCommandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
    mCommandList->SetGraphicsRootConstantBufferView(1, mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootShaderResourceView(2, mCurrFrameResource->MaterialBuffer->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetPipelineState(mPSOs["tessellation"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::Tessellation)]);
}

void SponzaLabsApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC baseDesc = {};
    baseDesc.InputLayout = { mInputLayout.data(), static_cast<UINT>(mInputLayout.size()) };
    baseDesc.pRootSignature = mRootSignature.Get();
    baseDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    baseDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    baseDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    baseDesc.SampleMask = UINT_MAX;
    baseDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    baseDesc.NumRenderTargets = 1;
    baseDesc.RTVFormats[0] = mBackBufferFormat;
    baseDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    baseDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    baseDesc.DSVFormat = mDepthStencilFormat;
    baseDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["forwardVS"]->GetBufferPointer()),
        mShaders["forwardVS"]->GetBufferSize()
    };
    baseDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["forwardOpaquePS"]->GetBufferPointer()),
        mShaders["forwardOpaquePS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&baseDesc, IID_PPV_ARGS(mPSOs["forwardOpaque"].ReleaseAndGetAddressOf())));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaDesc = baseDesc;
    alphaDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["forwardAlphaPS"]->GetBufferPointer()),
        mShaders["forwardAlphaPS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaDesc, IID_PPV_ARGS(mPSOs["forwardAlpha"].ReleaseAndGetAddressOf())));
}

void SponzaLabsApp::BuildFrameResources()
{
    mFrameResources.clear();

    const UINT objectCount = static_cast<UINT>(mAllRitems.size());
    const UINT materialCount = static_cast<UINT>(mMaterials.size());

    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            md3dDevice.Get(),
            1,
            objectCount,
            materialCount));
    }
}
