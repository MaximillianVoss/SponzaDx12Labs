#include "SponzaLabsApp.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

using namespace DirectX;

namespace
{
    BoundingBox TransformBoundingBox(const BoundingBox& bounds, CXMMATRIX world)
    {
        BoundingBox transformed;
        bounds.Transform(transformed, world);
        return transformed;
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        SponzaLabsApp theApp(hInstance);
        if(!theApp.Initialize())
        {
            return 0;
        }

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
    catch(const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Std exception", MB_OK);
        return 0;
    }
}

SponzaLabsApp::SponzaLabsApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"Sponza DX12 Labs";
}

SponzaLabsApp::~SponzaLabsApp()
{
    if(md3dDevice != nullptr)
    {
        FlushCommandQueue();
    }
}

bool SponzaLabsApp::Initialize()
{
    d3dUtil::EnsureProjectWorkingDirectory();

    if(!D3DApp::Initialize())
    {
        return false;
    }

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    mGBuffer.Initialize(md3dDevice.Get());
    mGBufferInitialized = true;
    mGBuffer.OnResize(md3dDevice.Get(), mClientWidth, mClientHeight, mDepthStencilBuffer.Get());

    LoadSceneData();
    LoadTextures();
    BuildRootSignature();
    BuildLightingRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildSceneGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildScatterItems();
    ResetCameraToScene();
    UpdateWindowCaption();
    BuildOctree();
    BuildFrameResources();
    BuildPSOs();

    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* commandLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);
    FlushCommandQueue();

    return true;
}

void SponzaLabsApp::ResetCameraToScene()
{
    const XMFLOAT3 target = mSceneFocusPoint;
    const XMFLOAT3 position(
        target.x - mSceneRadius * 0.18f,
        target.y + mSceneRadius * 0.18f,
        target.z - mSceneRadius * 1.55f);

    mCamera.LookAt(position, target, XMFLOAT3(0.0f, 1.0f, 0.0f));
    mCamera.UpdateViewMatrix();
}

void SponzaLabsApp::UpdateWindowCaption()
{
    const std::wstring renderMode = mRenderMode == RenderMode::Deferred ? L"Deferred" : L"Forward";
    const std::wstring frustumState = mEnableFrustumCulling ? L"ON" : L"OFF";
    const std::wstring octreeState = mEnableOctree ? L"ON" : L"OFF";
    const std::wstring tessellationState = mEnableTessellation ? L"ON" : L"OFF";

    const std::wstring caption =
        L"Sponza DX12 Labs"
        L" | mode: " + renderMode +
        L" | frustum: " + frustumState +
        L" | octree: " + octreeState +
        L" | tess: " + tessellationState +
        L" | scatter: " + std::to_wstring(mVisibleScatterItems.size()) + L"/" + std::to_wstring(mScatterItems.size());

    SetWindowText(mhMainWnd, caption.c_str());
    mMainWndCaption = caption;
}

void SponzaLabsApp::OnResize()
{
    D3DApp::OnResize();

    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 500.0f);
    BoundingFrustum::CreateFromMatrix(mCameraFrustum, mCamera.GetProj());

    if(mGBufferInitialized)
    {
        mGBuffer.OnResize(md3dDevice.Get(), mClientWidth, mClientHeight, mDepthStencilBuffer.Get());
    }
}
