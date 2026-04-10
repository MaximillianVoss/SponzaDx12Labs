#include "SponzaLabsApp.h"

#include <algorithm>
#include <numeric>

using namespace DirectX;

void SponzaLabsApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    AnimateMaterials(gt);
    UpdateVisibleScatterItems();
    UpdateObjectCBs(gt);
    UpdateMaterialBuffer(gt);
    UpdateMainPassCB(gt);
    UpdateWindowCaption();
}

void SponzaLabsApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void SponzaLabsApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void SponzaLabsApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        const float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        const float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void SponzaLabsApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if(GetAsyncKeyState('W') & 0x8000)
    {
        mCamera.Walk(10.0f * dt);
    }
    if(GetAsyncKeyState('S') & 0x8000)
    {
        mCamera.Walk(-10.0f * dt);
    }
    if(GetAsyncKeyState('A') & 0x8000)
    {
        mCamera.Strafe(-10.0f * dt);
    }
    if(GetAsyncKeyState('D') & 0x8000)
    {
        mCamera.Strafe(10.0f * dt);
    }

    if(ConsumeKeyPress('1'))
    {
        mRenderMode = RenderMode::Forward;
    }
    if(ConsumeKeyPress('2'))
    {
        mRenderMode = RenderMode::Deferred;
    }
    if(ConsumeKeyPress('F'))
    {
        mEnableFrustumCulling = !mEnableFrustumCulling;
    }
    if(ConsumeKeyPress('O'))
    {
        mEnableOctree = !mEnableOctree;
    }
    if(ConsumeKeyPress('T'))
    {
        mEnableTessellation = !mEnableTessellation;
    }
    if(ConsumeKeyPress('H') || ConsumeKeyPress(VK_F1))
    {
        mShowHelpOverlay = !mShowHelpOverlay;
        UpdateHelpOverlay();
    }
    if(ConsumeKeyPress(VK_HOME))
    {
        ResetCameraToScene();
    }

    mCamera.UpdateViewMatrix();
}

void SponzaLabsApp::AnimateMaterials(const GameTimer& gt)
{
    Material* animatedFloor = FindMaterialByName("animatedFloor");
    if(animatedFloor == nullptr)
    {
        return;
    }

    const float offset = 0.04f * gt.TotalTime();
    XMMATRIX matTransform = XMMatrixScaling(4.0f, 4.0f, 1.0f) * XMMatrixTranslation(offset, 0.0f, 0.0f);
    XMStoreFloat4x4(&animatedFloor->MatTransform, matTransform);
    animatedFloor->NumFramesDirty = gNumFrameResources;
}

void SponzaLabsApp::UpdateVisibleScatterItems()
{
    mVisibleScatterItems.clear();

    if(mScatterItems.empty())
    {
        return;
    }

    if(!mEnableFrustumCulling)
    {
        mVisibleScatterItems = mScatterItems;
        return;
    }

    XMMATRIX view = mCamera.GetView();
    XMVECTOR determinant = XMMatrixDeterminant(view);
    XMMATRIX invView = XMMatrixInverse(&determinant, view);

    BoundingFrustum worldFrustum;
    mCameraFrustum.Transform(worldFrustum, invView);

    std::vector<size_t> candidates;
    if(mEnableOctree)
    {
        mScatterOctree.Query(worldFrustum, candidates);
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    }
    else
    {
        candidates.resize(mScatterItems.size());
        std::iota(candidates.begin(), candidates.end(), 0);
    }

    for(size_t candidate : candidates)
    {
        RenderItem* item = mScatterItems[candidate];
        if(worldFrustum.Contains(item->Bounds) != DISJOINT)
        {
            mVisibleScatterItems.push_back(item);
        }
    }
}

void SponzaLabsApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currentObjectCB = mCurrFrameResource->ObjectCB.get();

    for(const auto& renderItem : mAllRitems)
    {
        if(renderItem->NumFramesDirty <= 0)
        {
            continue;
        }

        const XMMATRIX world = XMLoadFloat4x4(&renderItem->World);
        const XMMATRIX texTransform = XMLoadFloat4x4(&renderItem->TexTransform);

        ObjectConstants objectConstants;
        XMStoreFloat4x4(&objectConstants.World, XMMatrixTranspose(world));
        XMStoreFloat4x4(&objectConstants.TexTransform, XMMatrixTranspose(texTransform));
        objectConstants.MaterialIndex = renderItem->Mat->MatCBIndex;

        currentObjectCB->CopyData(renderItem->ObjCBIndex, objectConstants);
        renderItem->NumFramesDirty--;
    }
}

void SponzaLabsApp::UpdateMaterialBuffer(const GameTimer& gt)
{
    auto currentMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();

    for(const auto& pair : mMaterials)
    {
        Material* material = pair.second.get();
        if(material->NumFramesDirty <= 0)
        {
            continue;
        }

        MaterialData materialData;
        materialData.DiffuseAlbedo = material->DiffuseAlbedo;
        materialData.FresnelR0 = material->FresnelR0;
        materialData.Roughness = material->Roughness;
        XMStoreFloat4x4(&materialData.MatTransform, XMMatrixTranspose(XMLoadFloat4x4(&material->MatTransform)));
        materialData.DiffuseMapIndex = material->DiffuseSrvHeapIndex;
        materialData.NormalMapIndex = material->NormalSrvHeapIndex;
        materialData.DisplacementMapIndex = material->DisplacementSrvHeapIndex;

        currentMaterialBuffer->CopyData(material->MatCBIndex, materialData);
        material->NumFramesDirty--;
    }
}

void SponzaLabsApp::UpdateMainPassCB(const GameTimer& gt)
{
    const XMMATRIX view = mCamera.GetView();
    const XMMATRIX proj = mCamera.GetProj();
    const XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    XMVECTOR det = XMMatrixDeterminant(view);
    const XMMATRIX invView = XMMatrixInverse(&det, view);
    det = XMMatrixDeterminant(proj);
    const XMMATRIX invProj = XMMatrixInverse(&det, proj);
    det = XMMatrixDeterminant(viewProj);
    const XMMATRIX invViewProj = XMMatrixInverse(&det, viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.RenderTargetSize = XMFLOAT2(static_cast<float>(mClientWidth), static_cast<float>(mClientHeight));
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 500.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = XMFLOAT4(0.12f, 0.12f, 0.16f, 1.0f);

    for(Light& light : mMainPassCB.Lights)
    {
        light = Light{};
    }

    mMainPassCB.Lights[0].Direction = XMFLOAT3(0.35f, -0.85f, 0.25f);
    mMainPassCB.Lights[0].Strength = XMFLOAT3(0.65f, 0.62f, 0.60f);

    const std::array<XMFLOAT3, 4> pointPositions =
    {
        XMFLOAT3(-10.0f, 8.0f, -2.0f),
        XMFLOAT3(10.0f, 8.0f, -2.0f),
        XMFLOAT3(-10.0f, 8.0f, 12.0f),
        XMFLOAT3(10.0f, 8.0f, 12.0f)
    };
    const std::array<XMFLOAT3, 4> pointColors =
    {
        XMFLOAT3(1.00f, 0.45f, 0.35f),
        XMFLOAT3(0.35f, 0.60f, 1.00f),
        XMFLOAT3(0.50f, 1.00f, 0.45f),
        XMFLOAT3(1.00f, 0.85f, 0.35f)
    };

    for(UINT i = 0; i < pointPositions.size(); ++i)
    {
        Light& light = mMainPassCB.Lights[1 + i];
        light.Position = pointPositions[i];
        light.Strength = pointColors[i];
        light.FalloffStart = 4.0f;
        light.FalloffEnd = 24.0f;
    }

    Light& spotLight = mMainPassCB.Lights[5];
    spotLight.Position = mCamera.GetPosition3f();
    spotLight.Direction = XMFLOAT3(
        mCamera.GetLook().m128_f32[0],
        mCamera.GetLook().m128_f32[1],
        mCamera.GetLook().m128_f32[2]);
    spotLight.Strength = XMFLOAT3(0.9f, 0.9f, 0.8f);
    spotLight.FalloffStart = 3.0f;
    spotLight.FalloffEnd = 35.0f;
    spotLight.SpotPower = 32.0f;

    mCurrFrameResource->PassCB->CopyData(0, mMainPassCB);
}

bool SponzaLabsApp::ConsumeKeyPress(int virtualKey)
{
    const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    const bool consumed = down && !mKeyState[virtualKey];
    mKeyState[virtualKey] = down;
    return consumed;
}
