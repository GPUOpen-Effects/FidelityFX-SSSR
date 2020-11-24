#include "Utils.h"

void CopyToTexture(ID3D12GraphicsCommandList* cl, ID3D12Resource* source, ID3D12Resource* target, UINT32 width, UINT32 height)
{
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = source;
	src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	src.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = target;
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	D3D12_BOX srcBox = {};
	srcBox.left = 0;
	srcBox.top = 0;
	srcBox.front = 0;
	srcBox.right = width;
	srcBox.bottom = height;
	srcBox.back = 1;

	cl->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);
}
