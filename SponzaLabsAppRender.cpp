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

    void FillOverlayRect(HDC dc, const RECT& rect, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
    }
}

void SponzaLabsApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), nullptr));

    if(mRenderMode == RenderMode::Deferred)
    {
        DrawDeferredPass();
    }
    else
    {
        DrawForwardPass();
    }

    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &transition);

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* commandLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    DrawHelpOverlay();
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void SponzaLabsApp::DrawHelpOverlay() const
{
    if(!mShowHelpOverlay || mhMainWnd == nullptr)
    {
        return;
    }

    const std::wstring renderMode = mRenderMode == RenderMode::Deferred ? L"Deferred" : L"Forward";
    const std::wstring overlayText =
        L"Controls\n"
        L"W/A/S/D  move camera\n"
        L"Mouse    rotate camera\n"
        L"1        forward render\n"
        L"2        deferred render\n"
        L"F        frustum culling: " + std::wstring(mEnableFrustumCulling ? L"ON" : L"OFF") + L"\n"
        L"O        octree: " + std::wstring(mEnableOctree ? L"ON" : L"OFF") + L"\n"
        L"T        tessellation: " + std::wstring(mEnableTessellation ? L"ON" : L"OFF") + L"\n"
        L"H        hide help\n"
        L"Mode     " + renderMode + L"\n"
        L"Scatter  " + std::to_wstring(mVisibleScatterItems.size()) + L"/" + std::to_wstring(mScatterItems.size());

    HDC dc = GetDC(mhMainWnd);
    if(dc == nullptr)
    {
        return;
    }

    RECT panelRect = { 12, 12, 290, 222 };
    FillOverlayRect(dc, panelRect, RGB(18, 24, 31));

    RECT borderRect = panelRect;
    FrameRect(dc, &borderRect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(238, 242, 247));

    HFONT font = CreateFontW(
        -18,
        0,
        0,
        0,
        FW_MEDIUM,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));

    RECT textRect = { panelRect.left + 12, panelRect.top + 10, panelRect.right - 12, panelRect.bottom - 10 };
    DrawTextW(dc, overlayText.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_NOPREFIX);

    SelectObject(dc, oldFont);
    DeleteObject(font);
    ReleaseDC(mhMainWnd, dc);
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
    DrawRenderItems(mCommandList.Get(), mVisibleScatterItems);

    mCommandList->SetPipelineState(mPSOs["forwardAlpha"].Get());
    DrawRenderItems(mCommandList.Get(), mRitemLayer[static_cast<int>(RenderLayer::AlphaTested)]);

    if(mEnableTessellation)
    {
        DrawTessellationPass();
    }
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

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gbufferDesc = baseDesc;
    gbufferDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["gbufferVS"]->GetBufferPointer()),
        mShaders["gbufferVS"]->GetBufferSize()
    };
    gbufferDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["gbufferOpaquePS"]->GetBufferPointer()),
        mShaders["gbufferOpaquePS"]->GetBufferSize()
    };
    gbufferDesc.NumRenderTargets = 2;
    gbufferDesc.RTVFormats[0] = mGBuffer.AlbedoFormat();
    gbufferDesc.RTVFormats[1] = mGBuffer.NormalFormat();
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&gbufferDesc, IID_PPV_ARGS(mPSOs["gbufferOpaque"].ReleaseAndGetAddressOf())));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC gbufferAlphaDesc = gbufferDesc;
    gbufferAlphaDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["gbufferAlphaPS"]->GetBufferPointer()),
        mShaders["gbufferAlphaPS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&gbufferAlphaDesc, IID_PPV_ARGS(mPSOs["gbufferAlpha"].ReleaseAndGetAddressOf())));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC deferredDesc = {};
    deferredDesc.pRootSignature = mLightingRootSignature.Get();
    deferredDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    deferredDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    deferredDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    deferredDesc.DepthStencilState.DepthEnable = FALSE;
    deferredDesc.DepthStencilState.StencilEnable = FALSE;
    deferredDesc.SampleMask = UINT_MAX;
    deferredDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    deferredDesc.NumRenderTargets = 1;
    deferredDesc.RTVFormats[0] = mBackBufferFormat;
    deferredDesc.SampleDesc.Count = 1;
    deferredDesc.SampleDesc.Quality = 0;
    deferredDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    deferredDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["deferredVS"]->GetBufferPointer()),
        mShaders["deferredVS"]->GetBufferSize()
    };
    deferredDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["deferredPS"]->GetBufferPointer()),
        mShaders["deferredPS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&deferredDesc, IID_PPV_ARGS(mPSOs["deferred"].ReleaseAndGetAddressOf())));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tessellationDesc = baseDesc;
    tessellationDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    tessellationDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["tessVS"]->GetBufferPointer()),
        mShaders["tessVS"]->GetBufferSize()
    };
    tessellationDesc.HS =
    {
        reinterpret_cast<BYTE*>(mShaders["tessHS"]->GetBufferPointer()),
        mShaders["tessHS"]->GetBufferSize()
    };
    tessellationDesc.DS =
    {
        reinterpret_cast<BYTE*>(mShaders["tessDS"]->GetBufferPointer()),
        mShaders["tessDS"]->GetBufferSize()
    };
    tessellationDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["tessPS"]->GetBufferPointer()),
        mShaders["tessPS"]->GetBufferSize()
    };
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&tessellationDesc, IID_PPV_ARGS(mPSOs["tessellation"].ReleaseAndGetAddressOf())));
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
