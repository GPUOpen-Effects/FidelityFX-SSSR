#pragma once
#include <d3d12.h>

void CopyToTexture(ID3D12GraphicsCommandList* cl, ID3D12Resource* source, ID3D12Resource* target, UINT32 width, UINT32 height);