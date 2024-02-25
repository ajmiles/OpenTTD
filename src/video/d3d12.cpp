/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file d3d12.cpp D3D12 video driver support. */

#include "../stdafx.h"

#if defined(_WIN32)
#	include <windows.h>
#endif

#if defined(_MSC_VER)
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#endif /* defined(_MSC_VER) */

#include "d3d12.h"
#include "../core/geometry_func.hpp"
#include "../core/mem_func.hpp"
#include "../core/math_func.hpp"
#include "../core/mem_func.hpp"
#include "../gfx_func.h"
#include "../debug.h"
#include "../blitter/factory.hpp"
#include "../zoom_func.h"

#include "../table/sprites.h"


#include "../safeguards.h"

// Shaders
// If these are missing, run CompileD3D12Shaders.bat in src/video from a command line that can
// see 'dxc.exe', such as "x64 Native Tools Command Prompt for VS 2022"
#include "mainVS.csh"
#include "mainPS.csh"
#include "DrawVS.csh"
#include "ROVPS.csh"
#include "ScrollXCS.csh"
#include "ScrollYCS.csh"
#include "BlitCS.csh"

/** A simple 2D vertex with just position and texture. */
struct Simple2DVertex {
	float x, y;
	float u, v;
};

/** Maximum number of cursor sprites to cache. */
static const int MAX_CACHED_CURSORS = 48;

/* static */ D3D12Backend *D3D12Backend::instance = nullptr;

/**
 * Create and initialize the singleton back-end class.
 * @param get_proc Callback to get an D3D12 function from the OS driver.
 * @return nullptr on success, error message otherwise.
 */
/* static */ const char *D3D12Backend::Create()
{
	if (D3D12Backend::instance != nullptr) D3D12Backend::Destroy();

	D3D12Backend::instance = new D3D12Backend();
	return D3D12Backend::instance->Init();
}

/**
 * Free resources and destroy singleton back-end class.
 */
/* static */ void D3D12Backend::Destroy()
{
	delete D3D12Backend::instance;
	D3D12Backend::instance = nullptr;
}

/**
 * Construct OpenGL back-end class.
 */
D3D12Backend::D3D12Backend()
{

	QueryPerformanceCounter(&start);
}

/**
 * Free allocated resources.
 */
D3D12Backend::~D3D12Backend()
{
	
}

/**
 * Check for the needed OpenGL functionality and allocate all resources.
 * @return Error string or nullptr if successful.
 */
const char *D3D12Backend::Init()
{

	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
		//debugController->EnableDebugLayer();

		ComPtr<ID3D12Debug1> spDebugController1;
		debugController->QueryInterface(IID_PPV_ARGS(&spDebugController1));
		//spDebugController1->SetEnableGPUBasedValidation(true);
	}

	static bool useWarpDevice = false;

	HRESULT hr;

	if (useWarpDevice) {
		ComPtr<IDXGIFactory4> factory;
		hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory));

		ComPtr<IDXGIAdapter> warpAdapter;
		hr = factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));

		hr = D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
		device->SetName(L"OpenTTD D3D12 Device");
	}
	else {
		hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
		device->SetName(L"OpenTTD D3D12 Device");
	}

	if (FAILED(hr))
		return "Failed to create D3D12 Device at Feature Level 11.0";

	D3D12_COMMAND_QUEUE_DESC cqDesc = {};
	cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	hr = device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(commandQueue.ReleaseAndGetAddressOf()));
	device->SetName(L"OpenTTD D3D12 Graphics Command Queue");

	if (FAILED(hr))
		return "Failed to create D3D12 Graphics Command Queue";

	for (int i = 0; i < SWAP_CHAIN_BACK_BUFFER_COUNT; i++) {
		hr = device->CreateFence(nextFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fences[i].ReleaseAndGetAddressOf()));
		fences[i]->SetName(L"OpenTTD D3D12 Fence");
	}

	fenceEvent = CreateEventA(nullptr, FALSE, FALSE, "Fence Event");

	for (int i = 0; i < SWAP_CHAIN_BACK_BUFFER_COUNT; i++) 		{
		hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocators[i].ReleaseAndGetAddressOf()));

		wchar_t str[256];
		swprintf_s(str, L"OpenTTD D3D12 CommandAllocator Frame Index %u", i);
		commandAllocators[i]->SetName(str);

		frameEvents[i] = CreateEventA(nullptr, FALSE, FALSE, "Fence Event");
	}

	hr = device->CreateCommandList1(0, D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(commandList.ReleaseAndGetAddressOf()));
	commandList->SetName(L"OpenTTD D3D12 Command List");
	commandList->Reset(commandAllocators[0].Get(), nullptr);

	hr = device->CreateRootSignature(0, g_mainVS, sizeof(g_mainVS), IID_PPV_ARGS(rootSignature.ReleaseAndGetAddressOf()));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.pRootSignature = rootSignature.Get();
	psoDesc.PS = { g_mainPS, sizeof(g_mainPS) };
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = 1;
	psoDesc.VS = { g_mainVS, sizeof(g_mainVS) };

	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(finalCombinePSO.ReleaseAndGetAddressOf()));

	psoDesc.VS = { g_DrawVS, sizeof(g_DrawVS) };
	psoDesc.PS = { g_ROVPS, sizeof(g_ROVPS) };
	psoDesc.NumRenderTargets = 0;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	
	hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(blitPSO.ReleaseAndGetAddressOf()));

	D3D12_COMPUTE_PIPELINE_STATE_DESC csPSODesc = {};
	csPSODesc.pRootSignature = rootSignature.Get();
	csPSODesc.CS = { g_ScrollXCS, sizeof(g_ScrollXCS) };

	hr = device->CreateComputePipelineState(&csPSODesc, IID_PPV_ARGS(scrollXPSO.ReleaseAndGetAddressOf()));

	csPSODesc.CS = { g_ScrollYCS, sizeof(g_ScrollYCS) };
	hr = device->CreateComputePipelineState(&csPSODesc, IID_PPV_ARGS(scrollYPSO.ReleaseAndGetAddressOf()));

	csPSODesc.CS = { g_BlitCS, sizeof(g_BlitCS) };
	hr = device->CreateComputePipelineState(&csPSODesc, IID_PPV_ARGS(blitCSPSO.ReleaseAndGetAddressOf()));

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = SWAP_CHAIN_BACK_BUFFER_COUNT + 2;
	hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.ReleaseAndGetAddressOf()));

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.NumDescriptors = Descriptors::DESCRIPTOR_COUNT;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	hr = device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(srvHeap.ReleaseAndGetAddressOf()));

	paletteSurface = MallocT<uint>(256);

	return nullptr;
}

void D3D12Backend::WaitForGPU()
{
	ComPtr<ID3D12Fence> fence = fences[0];	// Doesn't matter which fence we use
	UINT64 now = fence->GetCompletedValue();
	UINT64 toBe = ++nextFenceValue;
	commandQueue->Signal(fence.Get(), toBe);

	// TODO. Use an event.
	while (fence->GetCompletedValue() < nextFenceValue)
	{
		Sleep(0);
	}
}

HRESULT D3D12Backend::CreateOrResizeSwapchain(int w, int h, bool force, HWND hwnd)
{
	// We can sometimes get a CreateOrResizeSwapchain request when already inside this function
	// so we don't need to recurse...
	if (isCreatingSwapChain)
		return S_OK;

	WaitForGPU();

	if (swapChain == nullptr) {
		isCreatingSwapChain = true;

		if (dxgiFactory == nullptr) {
			// Create a DXGI Factory first
			HRESULT hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(dxgiFactory.ReleaseAndGetAddressOf()));
			
			if (FAILED(hr)) {
				//TODO Error?
				return hr;
			}
		}

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.BufferCount = SWAP_CHAIN_BACK_BUFFER_COUNT;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;	// TODO: Check tearing is supported
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Height = h;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.Width = w;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsSwapChainDesc = {};
		fsSwapChainDesc.Windowed = TRUE;

		IDXGISwapChain1 *sc1;
		HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue.Get(), hwnd, &swapChainDesc, &fsSwapChainDesc, nullptr, &sc1);

		sc1->QueryInterface(IID_PPV_ARGS(swapChain.ReleaseAndGetAddressOf()));

		isCreatingSwapChain = false;

		if (FAILED(hr)) {
			// TODO Error?
			return hr;
		}
	} else {

		for (int i = 0; i < SWAP_CHAIN_BACK_BUFFER_COUNT; i++) {
			swapChainBuffers[i].Reset();
		}

		HRESULT hr = swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING);

		if (FAILED(hr)) {
			// TODO Error
			return hr;
		}
	}

	rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	for (int i = 0; i < SWAP_CHAIN_BACK_BUFFER_COUNT; i++) {
		swapChain->GetBuffer(i, IID_PPV_ARGS(swapChainBuffers[i].GetAddressOf()));

		wchar_t str[256];
		swprintf_s(str, L"OpenTTD D3D12 Swap Chain Buffer %u", i);
		swapChainBuffers[i]->SetName(str);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapStart = rtvHeap->GetCPUDescriptorHandleForHeapStart();
		rtvHeapStart.ptr += (i * rtvDescriptorSize);
				
		device->CreateRenderTargetView(swapChainBuffers[i].Get(), nullptr, rtvHeapStart);
	}

	Resize(w, h, force);

	swapChainBufferResizedThisFrame = true;

	return S_OK;
}

void D3D12Backend::Present()
{
	uint currentFrameIndex = swapChain->GetCurrentBackBufferIndex();

	char str[256];
	sprintf_s(str, "Present (%u)\n", currentFrameIndex);
	PIXBeginEvent(PIX_COLOR_DEFAULT, str);


	if (swapChainResourceState != D3D12_RESOURCE_STATE_PRESENT) {
		D3D12_RESOURCE_BARRIER transitionBarrier = {};
		transitionBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		transitionBarrier.Transition.pResource = swapChainBuffers[currentFrameIndex].Get();
		transitionBarrier.Transition.Subresource = 0;
		transitionBarrier.Transition.StateBefore = swapChainResourceState;
		transitionBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

		commandList->ResourceBarrier(1, &transitionBarrier);
		swapChainResourceState = D3D12_RESOURCE_STATE_PRESENT;
	}

	commandList->Close();
	commandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList *const *>(commandList.GetAddressOf()));

	frameEndValues[currentFrameIndex] = ++nextFenceValue;

	PIXBeginEvent(PIX_COLOR_DEFAULT, "swapChain->Present()");
	HRESULT hr = swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	PIXEndEvent();

	UINT64 valueToSignalTo = frameEndValues[currentFrameIndex];
	sprintf_s(str, "Signal + SetEventOnCompletion (%u) to value %u", currentFrameIndex, valueToSignalTo);

	PIXBeginEvent(PIX_COLOR_DEFAULT, str);
	commandQueue->Signal(fences[currentFrameIndex].Get(), valueToSignalTo);
	fences[currentFrameIndex]->SetEventOnCompletion(valueToSignalTo, frameEvents[currentFrameIndex]);
	PIXEndEvent();

	frameNumber++;

	if (frameNumber < 20) 		{
		wchar_t name[256];
		swprintf_s(name, L"E:/%u.wpix", frameNumber);
		//PIXGpuCaptureNextFrames(name, 1);
	}

	currentFrameIndex = swapChain->GetCurrentBackBufferIndex();

	remapBufferSpaceUsedThisFrame = 0;

	sprintf_s(str, "WaitForPreviousFrame (%u). Waiting on value %u. Currently %u.\n", currentFrameIndex, frameEndValues[currentFrameIndex], fences[currentFrameIndex]->GetCompletedValue());
	PIXBeginEvent(PIX_COLOR_DEFAULT, str);	
	//WaitForSingleObject(frameEvents[currentFrameIndex], INFINITE);

	while (fences[currentFrameIndex]->GetCompletedValue() < frameEndValues[currentFrameIndex]) {
		//__debugbreak();
		YieldProcessor();
	}

	PIXEndEvent();

	commandAllocators[currentFrameIndex]->Reset();
	commandList->Reset(commandAllocators[currentFrameIndex].Get(), nullptr);

	static int frames = 0;

	LARGE_INTEGER now, freq;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&now);

	double seconds = (now.QuadPart - start.QuadPart) / (double)freq.QuadPart;

	if (seconds > 1.0f) {

		//char str[256];
		//sprintf_s(str, "FPS: %u\n", frames);
		//OutputDebugStringA(str);

		frames = 0;
		start = now;
	}

	frames++;

	PIXEndEvent();
}

void D3D12Backend::PrepareContext()
{
	
}

std::string D3D12Backend::GetDriverName()
{
	return "D3D12Driver";
}

/**
 * Create all needed shader programs.
 * @return True if successful, false otherwise.
 */
bool D3D12Backend::InitShaders()
{
	return true;
}

/**
 * Change the size of the drawing window and allocate matching resources.
 * @param w New width of the window.
 * @param h New height of the window.
 * @param force Recreate resources even if size didn't change.
 * @param False if nothing had to be done, true otherwise.
 */
bool D3D12Backend::Resize(int w, int h, bool force)
{
	if (!force && _screen.width == w && _screen.height == h) return false;

	WaitForGPU();

	int bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();
	int bytesPerPixel = bpp / 8;

	if (vidSurface != nullptr)
		free(vidSurface);
	if (animSurface != nullptr)
		free(animSurface);

	// Align the 8bpp surface to 256 byte pitch, (i.e. 256 pixels) and then make that the pixel width of vidSurface
	uint alignedW = Align(w, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
	surfacePitchInPixels = alignedW;

	uint vidRowPitch = (alignedW * bytesPerPixel);
	uint vidBufferSize = vidRowPitch * h;
	vidSurface = MallocT<char>(vidBufferSize);

	uint animRowPitch = alignedW;
	uint animBufferSize = animRowPitch * h;
	animSurface = MallocT<char>(animBufferSize);

	ZeroMemory(vidSurface, vidBufferSize);
	ZeroMemory(animSurface, animBufferSize);

	
	D3D12_HEAP_PROPERTIES uploadProps = { D3D12_HEAP_TYPE_UPLOAD };
	D3D12_HEAP_PROPERTIES defaultProps = { D3D12_HEAP_TYPE_DEFAULT };

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += (SWAP_CHAIN_BACK_BUFFER_COUNT * rtvDescriptorSize);

	// Default Textures
	{
		vid_texture_default.Reset();
		anim_texture_default.Reset();
		vid_texture_default_GPUBlitter.Reset();
		anim_texture_default_GPUBlitter.Reset();

		D3D12_RESOURCE_DESC resDesc = {};
		resDesc.DepthOrArraySize = resDesc.MipLevels = resDesc.SampleDesc.Count = 1;
		resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		resDesc.Height = h;
		resDesc.Width = w;


		resDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		HRESULT hr = device->CreateCommittedResource(&defaultProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
													nullptr, IID_PPV_ARGS(vid_texture_default.ReleaseAndGetAddressOf()));
		hr = device->CreateCommittedResource(&defaultProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
													nullptr, IID_PPV_ARGS(vid_texture_default_GPUBlitter.ReleaseAndGetAddressOf()));

		device->CreateUnorderedAccessView(vid_texture_default_GPUBlitter.Get(), nullptr, nullptr, GetDescriptor(Descriptors::VID_TEXTURE_FLOAT));
		device->CreateRenderTargetView(vid_texture_default_GPUBlitter.Get(), nullptr, rtvHandle);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDescUINT = {};
		uavDescUINT.Format = DXGI_FORMAT_R32_UINT;
		uavDescUINT.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		device->CreateUnorderedAccessView(vid_texture_default_GPUBlitter.Get(), nullptr, &uavDescUINT, GetDescriptor(Descriptors::VID_TEXTURE_UINT));

		rtvHandle.ptr += rtvDescriptorSize;

		vid_texture_default->SetName(L"OpenTTD D3D12 Video Texture");

		resDesc.Format = DXGI_FORMAT_R8_UINT;
		hr = device->CreateCommittedResource(&defaultProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
												nullptr, IID_PPV_ARGS(anim_texture_default.ReleaseAndGetAddressOf()));
		hr = device->CreateCommittedResource(&defaultProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
												nullptr, IID_PPV_ARGS(anim_texture_default_GPUBlitter.ReleaseAndGetAddressOf()));

		device->CreateUnorderedAccessView(anim_texture_default_GPUBlitter.Get(), nullptr, nullptr, GetDescriptor(Descriptors::ANIM_TEXTURE));
		device->CreateRenderTargetView(anim_texture_default_GPUBlitter.Get(), nullptr, rtvHandle);

		anim_texture_default->SetName(L"OpenTTD D3D12 Anim Texture");
	}

	for (int i = 0; i < SWAP_CHAIN_BACK_BUFFER_COUNT; i++) {
		
		// Upload Buffers
		{
			vid_texture_upload[i].Reset();
			anim_texture_upload[i].Reset();

			D3D12_RESOURCE_DESC resDesc = {};
			resDesc.DepthOrArraySize = resDesc.MipLevels = resDesc.SampleDesc.Count = 1;
			resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resDesc.Format = DXGI_FORMAT_UNKNOWN;
			resDesc.Height = 1;
			resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resDesc.Width = vidBufferSize;

			HRESULT hr = device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE,
														nullptr, IID_PPV_ARGS(vid_texture_upload[i].ReleaseAndGetAddressOf()));

			wchar_t str[256];
			swprintf_s(str, L"OpenTTD D3D12 Video Upload Buffer Frame Index %u", i);
			vid_texture_upload[i]->SetName(str);

			resDesc.Width = animBufferSize;
			hr = device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE,
													nullptr, IID_PPV_ARGS(anim_texture_upload[i].ReleaseAndGetAddressOf()));

			swprintf_s(str, L"OpenTTD D3D12 Anim Upload Buffer Frame Index %u", i);
			anim_texture_upload[i]->SetName(str);

			resDesc.Width = 256 * 4;
			hr = device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
													nullptr, IID_PPV_ARGS(palette_texture_upload[i].ReleaseAndGetAddressOf()));

			D3D12_SHADER_RESOURCE_VIEW_DESC paletteSRVDesc = {};
			paletteSRVDesc.Format = DXGI_FORMAT_R32_UINT;
			paletteSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			paletteSRVDesc.Buffer.NumElements = 256;
			paletteSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			device->CreateShaderResourceView(palette_texture_upload[i].Get(), &paletteSRVDesc, GetDescriptor(Descriptors::PALETTE_TEXTURE_0, i));


			resDesc.Width = SIZE_OF_REMAP_BUFFER_UPLOAD_SPACE;
			hr = device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
													nullptr, IID_PPV_ARGS(remap_buffer_upload[i].ReleaseAndGetAddressOf()));
			remap_buffer_upload[i]->Map(0, nullptr, &remap_buffer_mapped[i]);
		}		
	}

	/* Set new viewport. */
	_screen.height = h;
	_screen.width = w;
	_screen.dst_ptr = nullptr;

	return true;
}

void D3D12Backend::FlushSpriteBuffer()
{
	if (blitRequests.size() == 0)
		return;

	LARGE_INTEGER freq, start, end;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);

	uint currentFrameIndex = swapChain->GetCurrentBackBufferIndex();

	char str[256];
	sprintf_s(str, "FlushSpriteBuffer (%u) %u sprites", currentFrameIndex, blitRequests.size());

	PIXBeginEvent(commandList.Get(), PIX_COLOR_DEFAULT, str);

	UpdatePaletteResource();


	static bool useCompute = false;


	static const D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);

	D3D12_GPU_VIRTUAL_ADDRESS baseRemapAddress = remap_buffer_upload[currentFrameIndex]->GetGPUVirtualAddress();
	uint screenResolution = (_screen.width | (_screen.height << 16));
	uint passConstants[2] = { screenResolution, currentFrameIndex };

	commandList->SetDescriptorHeaps(1, srvHeap.GetAddressOf());

	if (useCompute) {
		commandList->SetComputeRootSignature(rootSignature.Get());
		commandList->SetPipelineState(blitCSPSO.Get());

		D3D12_GPU_DESCRIPTOR_HANDLE firstSpriteHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
		firstSpriteHandle.ptr += (Descriptors::SPRITE_START * srvDescriptorSize);

		commandList->SetComputeRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
		commandList->SetComputeRootDescriptorTable(2, firstSpriteHandle);
		commandList->SetComputeRootShaderResourceView(3, baseRemapAddress);
		commandList->SetComputeRoot32BitConstants(4, ARRAYSIZE(passConstants), passConstants, 0);

		UINT numDwordsToSetForRequest = sizeof(BlitRequest) / sizeof(uint);

		for (int i = 0; i < blitRequests.size(); i++) {

			BlitRequest &req = blitRequests[i];
			
			commandList->SetComputeRoot32BitConstants(0, numDwordsToSetForRequest, &req, 0);
			
			uint width = req.right - req.left + 1;
			uint height = req.bottom - req.top + 1;
			uint numGroupsX = (width + 7) / 8;
			uint numGroupsY = (height + 7) / 8;

			commandList->Dispatch(numGroupsX, numGroupsY, 1);
			commandList->ResourceBarrier(1, &uavBarrier);
		}
	}
	else {
		commandList->SetGraphicsRootSignature(rootSignature.Get());		
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		D3D12_VIEWPORT viewport = { 0, 0, _screen.width, _screen.height, 0, 1 };
		commandList->RSSetViewports(1, &viewport);

		D3D12_RECT scissor = { 0, 0, _screen.width, _screen.height };
		commandList->RSSetScissorRects(1, &scissor);

		/*
		D3D12_RESOURCE_BARRIER targetsSRVToRTV[2];
		targetsSRVToRTV[0] = CD3DX12_RESOURCE_BARRIER::Transition(vid_texture_default_GPUBlitter.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
		targetsSRVToRTV[1] = CD3DX12_RESOURCE_BARRIER::Transition(anim_texture_default_GPUBlitter.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandList->ResourceBarrier(ARRAYSIZE(targetsSRVToRTV), targetsSRVToRTV);
		*/
		// Draw the blit requests.
		commandList->SetPipelineState(blitPSO.Get());

		commandList->OMSetRenderTargets(0, nullptr, TRUE, nullptr);

		D3D12_GPU_DESCRIPTOR_HANDLE firstSpriteHandle = srvHeap->GetGPUDescriptorHandleForHeapStart();
		firstSpriteHandle.ptr += (Descriptors::SPRITE_START * srvDescriptorSize);

		commandList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
		commandList->SetGraphicsRootDescriptorTable(2, firstSpriteHandle);
		commandList->SetGraphicsRootShaderResourceView(3, baseRemapAddress);
		commandList->SetGraphicsRoot32BitConstants(4, ARRAYSIZE(passConstants), passConstants, 0);
		
		UINT numDwordsToSetForRequest = sizeof(BlitRequest) / sizeof(uint);

		for (int i = 0; i < blitRequests.size(); i++) {
			BlitRequest &req = blitRequests[i];

			commandList->SetGraphicsRoot32BitConstants(0, numDwordsToSetForRequest, &req, 0);
			commandList->DrawInstanced(6, 1, 0, 0);
		}

		/*
		D3D12_RESOURCE_BARRIER targetsRTVToSRV[2];
		targetsRTVToSRV[0] = CD3DX12_RESOURCE_BARRIER::Transition(vid_texture_default_GPUBlitter.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		targetsRTVToSRV[1] = CD3DX12_RESOURCE_BARRIER::Transition(anim_texture_default_GPUBlitter.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		commandList->ResourceBarrier(ARRAYSIZE(targetsRTVToSRV), targetsRTVToSRV);*/
	}

	uint numRequests = blitRequests.size();

	blitRequests.clear();

	PIXEndEvent(commandList.Get());
	
	QueryPerformanceCounter(&end);

	double seconds = (end.QuadPart - start.QuadPart) / (double)freq.QuadPart;
	double milliseconds = seconds * 1000.0;

	

	static UINT64 totalRequests = 0;
	static double totalTime = 0;

	totalRequests += numRequests;
	totalTime += seconds;

	sprintf_s(str, "FlushSpriteBuffer, %f per millisecond\n", totalRequests / (totalTime * 1000.0));
	OutputDebugStringA(str);
	
}

/**
 * Render video buffer to the screen.
 */
void D3D12Backend::Paint()
{
	PIXBeginEvent(commandList.Get(), PIX_COLOR_DEFAULT, L"Paint");

	uint currentFrameIndex = swapChain->GetCurrentBackBufferIndex();

	FlushSpriteBuffer();
	UpdatePaletteResource();

	commandList->SetGraphicsRootSignature(rootSignature.Get());
	commandList->SetDescriptorHeaps(1, srvHeap.GetAddressOf());
	commandList->SetGraphicsRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D12_VIEWPORT viewport = { 0, 0, _screen.width, _screen.height, 0, 1 };
	commandList->RSSetViewports(1, &viewport);

	D3D12_RECT scissor = { 0, 0, _screen.width, _screen.height };
	commandList->RSSetScissorRects(1, &scissor);

	if (swapChainResourceState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
		// PRESENT to RENDER TARGET
		D3D12_RESOURCE_BARRIER presentToRTV = {};
		presentToRTV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		presentToRTV.Transition.pResource = swapChainBuffers[currentFrameIndex].Get();
		presentToRTV.Transition.StateBefore = swapChainResourceState;
		presentToRTV.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

		commandList->ResourceBarrier(1, &presentToRTV);
		swapChainResourceState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtvStart = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtvStart.ptr += (currentFrameIndex * rtvDescriptorSize);

	commandList->OMSetRenderTargets(1, &rtvStart, TRUE, nullptr);
	commandList->SetPipelineState(finalCombinePSO.Get());

	uint shaderMode = 0;	// Remap

	if (!BlitterFactory::GetCurrentBlitter()->NeedsAnimationBuffer()) {
		shaderMode = (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 8) ? 1 : 2;
	}

	commandList->SetGraphicsRoot32BitConstant(0, shaderMode, 0);
	commandList->SetGraphicsRoot32BitConstant(0, currentFrameIndex, 1);

	Point cursor_pos = _cursor.pos;
	commandList->SetGraphicsRoot32BitConstants(0, 2, &cursor_pos, 2);

	commandList->DrawInstanced(3, 1, 0, 0);

	PIXEndEvent(commandList.Get());
}

/**
 * Draw mouse cursor on screen.
 */
void D3D12Backend::DrawMouseCursor()
{
	// TODO
}

void D3D12Backend::PopulateCursorCache()
{

}

/**
 * Clear all cached cursor sprites.
 */
void D3D12Backend::InternalClearCursorCache()
{
}

/**
 * Queue a request for cursor cache clear.
 */
void D3D12Backend::ClearCursorCache()
{
}

/**
 * Get a pointer to the memory for the video driver to draw to.
 * @return Pointer to draw on.
 */
void *D3D12Backend::GetVideoBuffer()
{
	_screen.pitch = surfacePitchInPixels;
	return vidSurface;
}

/**
 * Get a pointer to the memory for the separate animation buffer.
 * @return Pointer to draw on.
 */
uint8_t *D3D12Backend::GetAnimBuffer()
{
	return (uint8_t *)animSurface;
}

/**
 * Update video buffer texture after the video buffer was filled.
 * @param update_rect Rectangle encompassing the dirty region of the video buffer.
 */
void D3D12Backend::ReleaseVideoBuffer(const Rect &update_rect)
{
	return;
}


/**
 * Update animation buffer texture after the animation buffer was filled.
 * @param update_rect Rectangle encompassing the dirty region of the animation buffer.
 */
void D3D12Backend::ReleaseAnimBuffer(const Rect &update_rect)
{
	return;
}

void D3D12Backend::UpdatePaletteResource()
{
	uint index = swapChain->GetCurrentBackBufferIndex();

	if (paletteIsDirty[index]) {
		
		uint *palette;
		palette_texture_upload[index]->Map(0, nullptr, (void **)&palette);
		memcpy(palette, paletteSurface, 256 * 4);

		paletteIsDirty[index] = false;
	}
}

void D3D12Backend::UpdatePalette(const Colour *pal, uint first, uint length)
{
	memcpy((uint32_t*)paletteSurface + first, pal + first, length * 4);

	for (int i = 0; i < SWAP_CHAIN_BACK_BUFFER_COUNT; i++)
		paletteIsDirty[i] = true;
}


void D3D12Backend::EnqueueFillRect(int left, int top, int right, int bottom, uint8_t colour)
{
	BlitRequest req = { left, top, right, bottom, 0, 0, colour, BlitType::SOLID_COLOUR, -1, ZOOM_LVL_BEGIN, 0, 0 };
	blitRequests.push_back(req);
}

void D3D12Backend::EnqueueSpriteBlit(SpriteBlitRequest *request)
{
	if (spriteResources[request->gpuSpriteID].spriteResources[request->zoom] == nullptr)
		return;

	BlitRequest req = { request->left, request->top, request->right, request->bottom, request->skip_left, request->skip_top, 0,
		BlitType::SPRITE_ALPHA, request->gpuSpriteID, request->zoom, request->blitterMode, 0 };

	static uint s_highWatermark = 0;

	// TODO - I really need BlitterMode here...
	if (request->blitterMode == 1 || request->blitterMode == 3 || request->blitterMode == 4 || request->blitterMode == 5) {

		// Allocate some space for the remap table
		const uint spaceRequired = 256;

		if (SIZE_OF_REMAP_BUFFER_UPLOAD_SPACE - remapBufferSpaceUsedThisFrame > spaceRequired) {

			uint currentFrameIndex = swapChain->GetCurrentBackBufferIndex();
			char *dest = (char*)remap_buffer_mapped[currentFrameIndex];
			memcpy(dest + remapBufferSpaceUsedThisFrame, request->remap, spaceRequired);

			req.remapByteOffset = remapBufferSpaceUsedThisFrame;

			remapBufferSpaceUsedThisFrame += spaceRequired;

			if (remapBufferSpaceUsedThisFrame > s_highWatermark) {
				s_highWatermark = remapBufferSpaceUsedThisFrame;

				//char str[256];
				//sprintf_s(str, "New high watermark: %u\n", s_highWatermark);
				//OutputDebugStringA(str);
			}
		}
		else {
			__debugbreak();
		}
	}

	blitRequests.push_back(req);
}

uint32_t D3D12Backend::CreateGPUSprite(const SpriteLoader::SpriteCollection &spriteColl)
{
	// TODO - Don't put the sprites in system memory!
	D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE, D3D12_MEMORY_POOL_L0);

	SpriteResourceSet spriteZoomSet = {};

	ZoomLevel zoom_min;
	ZoomLevel zoom_max;

	if (spriteColl[ZOOM_LVL_NORMAL].type == SpriteType::Font) {
		zoom_min = ZOOM_LVL_NORMAL;
		zoom_max = ZOOM_LVL_NORMAL;
	} else {
		zoom_min = ZOOM_LVL_NORMAL;
		zoom_max = (ZoomLevel)(ZOOM_LVL_END - 1);	// TODO get client settings.
		if (zoom_max == zoom_min) zoom_max = ZOOM_LVL_MAX;
	}

	for (int z = zoom_min; z <= zoom_max; z++) {

		const SpriteLoader::Sprite &sprite = spriteColl[z];

		DXGI_FORMAT format;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		
		if (sprite.colours == SCC_PAL) {
			format = DXGI_FORMAT_R8G8_UINT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;// D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 4, 4);	// Red, G, 0, 0
		}
		else if (sprite.colours == (SCC_ALPHA | SCC_PAL)) {
			format = DXGI_FORMAT_R8G8_UINT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;// D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 4, 4);	// Red, G, 0, 0
		}
		else {
			format = DXGI_FORMAT_R8G8_UINT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;// D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 4, 4);	// Red, G, 0, 0
		}

		srvDesc.Format = format;

		D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, sprite.width, sprite.height, 1, 1);

		ID3D12Resource **zoomTex = &spriteZoomSet.spriteResources[z];

		HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(zoomTex));
		assert(SUCCEEDED(hr));

		// TODO - Don't have the textures in a System Memory heap - this is just for testing! Awful perf!
		hr = (*zoomTex)->Map(0, nullptr, nullptr);
		assert(SUCCEEDED(hr));

		uint numTexels = sprite.width * sprite.height;

		if (false){// sprite.colours == SCC_PAL) {
			// Copy just the 'm' channel
			uint8_t *mData = MallocT<uint8_t>(numTexels);

			for (int i = 0; i < numTexels; i++)
				mData[i] = sprite.data[i].m;

			(*zoomTex)->WriteToSubresource(0, nullptr, mData, sprite.width, 0);
			free(mData);
		} else if (sprite.colours == (SCC_ALPHA | SCC_PAL) || sprite.colours == (SCC_PAL)) {
			uint16_t *maData = MallocT<uint16_t>(numTexels);

			for (int i = 0; i < numTexels; i++) {
				uint16_t m = sprite.data[i].m;
				uint16_t a = sprite.data[i].a;
				maData[i] = m | (a << 8);
			}

			(*zoomTex)->WriteToSubresource(0, nullptr, maData, sprite.width * 2, 0);
			free(maData);
		}


		D3D12_CPU_DESCRIPTOR_HANDLE textureHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
		textureHandle.ptr += ((Descriptors::SPRITE_START + (nextGPUSpriteID * ZOOM_LVL_END) + z) * srvDescriptorSize);

		device->CreateShaderResourceView(*zoomTex, &srvDesc, textureHandle);
	}
	
	spriteResources.push_back(spriteZoomSet);

	return nextGPUSpriteID++;
}

void D3D12Backend::ScrollBuffer(int &left, int &top, int &width, int &height, int scroll_x, int scroll_y)
{
	FlushSpriteBuffer();

	PIXBeginEvent(commandList.Get(), PIX_COLOR_DEFAULT, L"ScrollBuffer");

	if (scroll_x != 0 || scroll_y != 0) 		{
		commandList->SetComputeRootSignature(rootSignature.Get());

		int scrollParams[8] = { left, top, width, height, scroll_x, scroll_y, _screen.width, _screen.height };
		commandList->SetComputeRoot32BitConstants(0, ARRAYSIZE(scrollParams), scrollParams, 0);

		commandList->SetDescriptorHeaps(1, srvHeap.GetAddressOf());
		commandList->SetComputeRootDescriptorTable(1, srvHeap->GetGPUDescriptorHandleForHeapStart());
	}

	// Not sure what came before that we could overlap with...
	D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
	commandList->ResourceBarrier(1, &uavBarrier);

	if (scroll_x != 0) {	
		commandList->SetPipelineState(scrollXPSO.Get());
		commandList->Dispatch(1, height, 1);
		commandList->ResourceBarrier(1, &uavBarrier);
	}


	if (scroll_y != 0) {
		commandList->SetPipelineState(scrollYPSO.Get());
		commandList->Dispatch(width, 1, 1);
		commandList->ResourceBarrier(1, &uavBarrier);
	}

	PIXEndEvent(commandList.Get());
}
