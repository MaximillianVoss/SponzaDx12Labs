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

    if(mHelpOverlayFont != nullptr)
    {
        DeleteObject(mHelpOverlayFont);
        mHelpOverlayFont = nullptr;
    }

    if(mHelpOverlayBrush != nullptr)
    {
        DeleteObject(mHelpOverlayBrush);
        mHelpOverlayBrush = nullptr;
    }
}

bool SponzaLabsApp::Initialize()
{
    d3dUtil::EnsureProjectWorkingDirectory();

    if(!D3DApp::Initialize())
    {
        return false;
    }

    CreateHelpOverlay();

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

LRESULT SponzaLabsApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(msg == WM_CTLCOLORSTATIC && reinterpret_cast<HWND>(lParam) == mHelpOverlayWnd)
    {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB(18, 24, 31));
        SetTextColor(dc, RGB(238, 242, 247));
        return reinterpret_cast<LRESULT>(mHelpOverlayBrush);
    }

    const LRESULT result = D3DApp::MsgProc(hwnd, msg, wParam, lParam);

    if((msg == WM_SIZE || msg == WM_EXITSIZEMOVE) && mHelpOverlayWnd != nullptr)
    {
        LayoutHelpOverlay();
    }

    return result;
}

void SponzaLabsApp::ResetCameraToScene()
{
    const XMFLOAT3 target(
        mSceneFocusPoint.x,
        5.2f,
        mSceneFocusPoint.z + mSceneRadius * 0.28f);
    const XMFLOAT3 position(
        mSceneFocusPoint.x,
        5.8f,
        mSceneFocusPoint.z - mSceneRadius * 0.58f);

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
    UpdateHelpOverlay();
}

void SponzaLabsApp::CreateHelpOverlay()
{
    if(mhMainWnd == nullptr || mHelpOverlayWnd != nullptr)
    {
        return;
    }

    mHelpOverlayBrush = CreateSolidBrush(RGB(18, 24, 31));
    mHelpOverlayFont = CreateFontW(
        -17,
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

    mHelpOverlayWnd = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | SS_LEFT | SS_NOPREFIX,
        12,
        12,
        368,
        286,
        mhMainWnd,
        nullptr,
        AppInst(),
        nullptr);

    SendMessageW(mHelpOverlayWnd, WM_SETFONT, reinterpret_cast<WPARAM>(mHelpOverlayFont), TRUE);
    LayoutHelpOverlay();
    UpdateHelpOverlay();
}

void SponzaLabsApp::LayoutHelpOverlay()
{
    if(mHelpOverlayWnd == nullptr)
    {
        return;
    }

    SetWindowPos(mHelpOverlayWnd, HWND_TOP, 14, 14, 368, 286, SWP_NOACTIVATE);
}

void SponzaLabsApp::UpdateHelpOverlay()
{
    if(mHelpOverlayWnd == nullptr)
    {
        return;
    }

    const std::wstring renderMode = mRenderMode == RenderMode::Deferred ? L"Deferred" : L"Forward";
    const std::wstring overlayText =
        L"Controls\r\n"
        L"W / A / S / D   move camera\r\n"
        L"Mouse           rotate camera\r\n"
        L"1               forward render\r\n"
        L"2               deferred render\r\n"
        L"F               frustum culling: " + std::wstring(mEnableFrustumCulling ? L"ON" : L"OFF") + L"\r\n"
        L"O               octree: " + std::wstring(mEnableOctree ? L"ON" : L"OFF") + L"\r\n"
        L"T               tessellation: " + std::wstring(mEnableTessellation ? L"ON" : L"OFF") + L"\r\n"
        L"H / F1          show or hide help\r\n"
        L"Home            reset camera\r\n"
        L"Mode            " + renderMode + L"\r\n"
        L"Scatter         " + std::to_wstring(mVisibleScatterItems.size()) + L" / " + std::to_wstring(mScatterItems.size());

    SetWindowTextW(mHelpOverlayWnd, overlayText.c_str());
    ShowWindow(mHelpOverlayWnd, mShowHelpOverlay ? SW_SHOW : SW_HIDE);
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

    LayoutHelpOverlay();
}
