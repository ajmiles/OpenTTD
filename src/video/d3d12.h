/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file d3d12.h D3D12 video driver support. */

#ifndef VIDEO_D3D12_H
#define VIDEO_D3D12_H

#include "../core/alloc_type.hpp"
#include "../core/geometry_type.hpp"
#include "../gfx_type.h"
#include "../spriteloader/spriteloader.hpp"
#include "../misc/lrucache.hpp"

// Vista required for WRL header
#undef NTDDI_VERSION
#undef _WIN32_WINNT

#define NTDDI_VERSION    NTDDI_WIN7
#define _WIN32_WINNT     _WIN32_WINNT_WIN7

#include <wrl/client.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>

#define USE_PIX
#include "pix/pix3.h"

#define MAX_SPRITES_SUPPORTED 100000

using Microsoft::WRL::ComPtr;

class D3D12Sprite;

enum Descriptors {
	VID_TEXTURE_FLOAT,
	ANIM_TEXTURE,
	VID_TEXTURE_UINT,
	PALETTE_TEXTURE_0,
	PALETTE_TEXTURE_1,
	PALETTE_TEXTURE_2,

	SPRITE_START,
	DESCRIPTOR_COUNT = SPRITE_START + (MAX_SPRITES_SUPPORTED * ZOOM_LVL_END)
};

enum BlitType {
	SOLID_COLOUR,
	SPRITE_OPAQUE,
	SPRITE_ALPHA,
	BLIT_TYPE_COUNT
};


struct BlitRequest {
	int left;
	int top;
	int right;
	int bottom;
	int skip_left;
	int skip_top;
	uint colour;
	BlitType blitType;
	uint32_t gpuSpriteID;
	uint zoom;
	uint blitterMode;	// BlitterMode
	uint remapByteOffset;
};

struct SpriteResourceSet {
	ID3D12Resource *spriteResources[ZOOM_LVL_END];
};

class D3D12Sprite {
private:

public:
	D3D12Sprite() {};
	~D3D12Sprite() {};

	friend class D3D12Backend;
};

/** Platform-independent back-end class for D3D12 video drivers. */
class D3D12Backend : public ZeroedMemoryAllocator {
private:
	static D3D12Backend *instance; ///< Singleton instance pointer.

	static const uint SWAP_CHAIN_BACK_BUFFER_COUNT = 3;

	// DXGI / D3D12 Resources
	ComPtr<IDXGIFactory2> dxgiFactory;
	
	ComPtr<ID3D12Device4> device;
	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<IDXGISwapChain3> swapChain;
	ComPtr<ID3D12Fence> fences[SWAP_CHAIN_BACK_BUFFER_COUNT];

	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12PipelineState> finalCombinePSO;
	ComPtr<ID3D12PipelineState> blitPSO;
	ComPtr<ID3D12PipelineState> scrollXPSO, scrollYPSO;

	ComPtr<ID3D12PipelineState> blitCSPSO;

	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	uint32_t rtvDescriptorSize;

	ComPtr<ID3D12DescriptorHeap> srvHeap;
	uint32_t srvDescriptorSize;

	uint64_t nextFenceValue = 0;
	HANDLE fenceEvent;

	static const uint SIZE_OF_REMAP_BUFFER_UPLOAD_SPACE = 64 * 1024 * 1024;

	ComPtr<ID3D12Resource> vid_texture_upload[SWAP_CHAIN_BACK_BUFFER_COUNT];
	ComPtr<ID3D12Resource> palette_texture_upload[SWAP_CHAIN_BACK_BUFFER_COUNT];
	ComPtr<ID3D12Resource> anim_texture_upload[SWAP_CHAIN_BACK_BUFFER_COUNT];

	ComPtr<ID3D12Resource> remap_buffer_upload[SWAP_CHAIN_BACK_BUFFER_COUNT];
	void *remap_buffer_mapped[SWAP_CHAIN_BACK_BUFFER_COUNT];

	ComPtr<ID3D12Resource> vid_texture_default;
	ComPtr<ID3D12Resource> anim_texture_default;
	ComPtr<ID3D12Resource> vid_texture_default_GPUBlitter;
	ComPtr<ID3D12Resource> anim_texture_default_GPUBlitter;

	ComPtr<ID3D12Resource> swapChainBuffers[SWAP_CHAIN_BACK_BUFFER_COUNT];

	void *vidSurface = nullptr;
	void *animSurface = nullptr;
	void *paletteSurface = nullptr;
	bool paletteIsDirty[SWAP_CHAIN_BACK_BUFFER_COUNT];
	bool isFirstPaletteUpdate = true;

	uint surfacePitchInPixels;
	uint remapBufferSpaceUsedThisFrame = 0;

	HANDLE frameEvents[SWAP_CHAIN_BACK_BUFFER_COUNT];
	uint64_t frameEndValues[SWAP_CHAIN_BACK_BUFFER_COUNT] = {};
	ComPtr<ID3D12CommandAllocator> commandAllocators[SWAP_CHAIN_BACK_BUFFER_COUNT];
	ComPtr<ID3D12GraphicsCommandList> commandList;

	D3D12_RESOURCE_STATES swapChainResourceState = D3D12_RESOURCE_STATE_COMMON;

	std::vector<BlitRequest> blitRequests;
	std::vector<SpriteBlitRequest> spriteBlitRequests;

	LARGE_INTEGER start;
	uint32_t frameNumber = 0;

	bool swapChainBufferResizedThisFrame = false;
	bool isCreatingSwapChain = false;

	uint32_t nextGPUSpriteID = 0;
	std::vector<SpriteResourceSet> spriteResources;

	D3D12Backend();
	~D3D12Backend();

	void WaitForGPU();

	const char *Init();
	bool InitShaders();

	void InternalClearCursorCache();

	void RenderD3D12Sprite(D3D12Sprite *gl_sprite, PaletteID pal, int x, int y, ZoomLevel zoom);

private:
	D3D12_CPU_DESCRIPTOR_HANDLE GetDescriptor(Descriptors descriptor, int offsetInDescriptors = 0)
	{
		auto start = srvHeap->GetCPUDescriptorHandleForHeapStart();
		start.ptr += (descriptor + offsetInDescriptors) * srvDescriptorSize;
		return start;
	}

public:
	/** Get singleton instance of this class. */
	static inline D3D12Backend *Get()
	{
		return D3D12Backend::instance;
	}
	static const char *Create();
	static void Destroy();

	void PrepareContext();

	std::string GetDriverName();

	HRESULT CreateOrResizeSwapchain(int w, int h, bool force, HWND hwnd);
	void Present();

	void UpdatePalette(const Colour *pal, uint first, uint length);
	bool Resize(int w, int h, bool force = false);
	void Paint();

	void UpdatePaletteResource();
	void FlushSpriteBuffer();

	void DrawMouseCursor();
	void PopulateCursorCache();
	void ClearCursorCache();

	void *GetVideoBuffer();
	uint8_t *GetAnimBuffer();
	void ReleaseVideoBuffer(const Rect &update_rect);
	void ReleaseAnimBuffer(const Rect &update_rect);

	void EnqueueFillRect(int left, int top, int right, int bottom, uint8_t colour);
	void EnqueueSpriteBlit(SpriteBlitRequest *request);

	uint32_t CreateGPUSprite(const SpriteLoader::SpriteCollection &sprite);
	void ScrollBuffer(int &left, int &top, int &width, int &height, int scroll_x, int scroll_y);
};

#endif /* VIDEO_D3D12_H */
