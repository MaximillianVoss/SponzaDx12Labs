#pragma once

#include "Common/d3dUtil.h"

namespace TgaTextureLoader
{
    void LoadTextureFromFile(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* commandList,
        Texture& texture);
}
