#pragma once

#include "Common/d3dApp.h"
#include "Common/Camera.h"
#include "FrameResource.h"
#include "GBuffer.h"
#include "ObjModelLoader.h"
#include "Octree.h"

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class RenderMode
{
    Forward,
    Deferred
};

enum class RenderLayer : int
{
    Opaque = 0,
    AlphaTested,
    Scatter,
    Tessellation,
    Count
};

struct RenderItem
{
    RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;

    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
    DirectX::BoundingBox Bounds = {};
    int NumFramesDirty = gNumFrameResources;
    UINT ObjCBIndex = 0;
    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
    bool ParticipatesInCulling = false;
};

class SponzaLabsApp : public D3DApp
{
public:
    SponzaLabsApp(HINSTANCE hInstance);
    SponzaLabsApp(const SponzaLabsApp& rhs) = delete;
    SponzaLabsApp& operator=(const SponzaLabsApp& rhs) = delete;
    ~SponzaLabsApp();

    bool Initialize() override;

private:
    void OnResize() override;
    void Update(const GameTimer& gt) override;
    void Draw(const GameTimer& gt) override;

    void OnMouseDown(WPARAM btnState, int x, int y) override;
    void OnMouseUp(WPARAM btnState, int x, int y) override;
    void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void AnimateMaterials(const GameTimer& gt);
    void UpdateVisibleScatterItems();
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialBuffer(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);

    void LoadSceneData();
    void LoadTextures();
    void BuildRootSignature();
    void BuildLightingRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildSceneGeometry();
    void BuildMaterials();
    void BuildRenderItems();
    void BuildScatterItems();
    void BuildOctree();
    void BuildPSOs();
    void BuildFrameResources();

    void DrawRenderItems(ID3D12GraphicsCommandList* commandList, const std::vector<RenderItem*>& renderItems);
    void DrawForwardPass();
    void DrawDeferredPass();
    void DrawTessellationPass();

    int ResolveTextureIndex(const std::wstring& texturePath, const std::string& fallbackName) const;
    Material* FindMaterialByName(const std::string& name);
    Material* FindMaterialContaining(const std::string& token);
    bool ConsumeKeyPress(int virtualKey);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers() const;

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mLightingRootSignature;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, UINT> mTextureSrvLookup;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;
    std::vector<std::string> mTextureHeapOrder;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];
    std::vector<RenderItem*> mScatterItems;
    std::vector<RenderItem*> mVisibleScatterItems;

    ObjSceneData mSceneData;
    GBuffer mGBuffer;
    bool mGBufferInitialized = false;

    Camera mCamera;
    DirectX::BoundingFrustum mCameraFrustum;
    SceneOctree mScatterOctree;

    PassConstants mMainPassCB;

    RenderMode mRenderMode = RenderMode::Deferred;
    bool mEnableFrustumCulling = true;
    bool mEnableOctree = true;
    bool mEnableTessellation = true;
    std::array<bool, 256> mKeyState = {};

    POINT mLastMousePos = {};
};
