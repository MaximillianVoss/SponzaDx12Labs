#include "SponzaLabsApp.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

using namespace DirectX;

namespace
{
    constexpr wchar_t kHelpOverlayClassName[] = L"SponzaHelpOverlayWindow";
    constexpr int kHelpOverlayWidth = 368;
    constexpr int kHelpOverlayHeight = 320;

    BoundingBox TransformBoundingBox(const BoundingBox& bounds, CXMMATRIX world)
    {
        BoundingBox transformed;
        bounds.Transform(transformed, world);
        return transformed;
    }

    LRESULT CALLBACK HelpOverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch(msg)
        {
        case WM_NCCREATE:
        {
            return TRUE;
        }

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
        {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd, &ps);

            RECT rect = {};
            GetClientRect(hwnd, &rect);
            HBRUSH brush = CreateSolidBrush(RGB(18, 24, 31));
            FillRect(dc, &rect, brush);
            FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            DeleteObject(brush);

            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(238, 242, 247));

            HFONT font = CreateFontW(
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
            HFONT oldFont = static_cast<HFONT>(SelectObject(dc, font));

            RECT textRect = { 12, 10, rect.right - 12, rect.bottom - 10 };
            const int textLength = GetWindowTextLengthW(hwnd);
            std::wstring text(textLength, L'\0');
            GetWindowTextW(hwnd, text.data(), textLength + 1);
            DrawTextW(dc, text.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_NOPREFIX);

            SelectObject(dc, oldFont);
            DeleteObject(font);

            EndPaint(hwnd, &ps);
            return 0;
        }
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
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
    mMainWndCaption = L"Sponza DX12 Lab 1";
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

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    LoadSceneData();
    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildSceneGeometry();
    BuildMaterials();
    BuildRenderItems();
    ResetCameraToScene();
    UpdateWindowCaption();
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
        3.0f,
        mSceneFocusPoint.z + mSceneRadius * 0.10f);
    const XMFLOAT3 position(
        mSceneFocusPoint.x - mSceneRadius * 0.02f,
        1.9f,
        mSceneFocusPoint.z - mSceneRadius * 0.18f);

    mCamera.LookAt(position, target, XMFLOAT3(0.0f, 1.0f, 0.0f));
    mCamera.UpdateViewMatrix();
}

void SponzaLabsApp::UpdateWindowCaption()
{
    mMainWndCaption = L"Sponza DX12 Lab 1";
}

void SponzaLabsApp::CreateHelpOverlay()
{
    if(mhMainWnd == nullptr || mHelpOverlayWnd != nullptr)
    {
        return;
    }

    static bool classRegistered = false;
    if(!classRegistered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = HelpOverlayWndProc;
        wc.hInstance = AppInst();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kHelpOverlayClassName;
        RegisterClassW(&wc);
        classRegistered = true;
    }

    mHelpOverlayWnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        kHelpOverlayClassName,
        L"",
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        kHelpOverlayWidth,
        kHelpOverlayHeight,
        mhMainWnd,
        nullptr,
        AppInst(),
        nullptr);

    LayoutHelpOverlay();
    UpdateHelpOverlay();
}

void SponzaLabsApp::LayoutHelpOverlay()
{
    if(mHelpOverlayWnd == nullptr)
    {
        return;
    }

    RECT windowRect = {};
    GetWindowRect(mhMainWnd, &windowRect);

    SetWindowPos(
        mHelpOverlayWnd,
        HWND_TOPMOST,
        windowRect.left + 14,
        windowRect.top + 38,
        kHelpOverlayWidth,
        kHelpOverlayHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

    mHelpOverlayText = overlayText;
    SetWindowTextW(mHelpOverlayWnd, overlayText.c_str());
    InvalidateRect(mHelpOverlayWnd, nullptr, TRUE);
    ShowWindow(mHelpOverlayWnd, mShowHelpOverlay ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void SponzaLabsApp::OnResize()
{
    D3DApp::OnResize();

    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 500.0f);
    BoundingFrustum::CreateFromMatrix(mCameraFrustum, mCamera.GetProj());
}
