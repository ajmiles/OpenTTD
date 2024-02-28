/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file 40bpp_anim_gpu.cpp Implementation of the GPU-based 40 bpp blitter. */

#include "../stdafx.h"
#include "../zoom_func.h"
#include "../settings_type.h"
#include "../video/video_driver.hpp"
#include "../palette_func.h"
#include "40bpp_anim_gpu.hpp"
#include "common.hpp"

#include "../table/sprites.h"

#include "../safeguards.h"

#if defined(_WIN32)
#	include <windows.h>
#endif


/** Instantiation of the 40bpp with animation blitter factory. */
static FBlitter_40bppAnimGPU iFBlitter_40bppAnimGPU;

/** Cached black value. */
static const Colour _black_colour(0, 0, 0);

std::pair<uint, uint> GetXYFromDst(const void *dst)
{
	uint difference = ((char *)dst - (char *)_screen.dst_ptr) / 4;
	uint x = difference % _screen.pitch;
	uint y = difference / _screen.pitch;

	return std::make_pair(x, y);
}

void *Blitter_40bppAnimGPU::MoveTo(void *video, int x, int y)
{
	return (uint32_t *)video + x + y * _screen.pitch;
}

void Blitter_40bppAnimGPU::SetPixel(void *video, int x, int y, uint8_t colour)
{
	assert(VideoDriver::GetInstance()->SupportsGPUBlitting());

	std::pair<uint, uint> dstCoord = GetXYFromDst(video);
	auto dstX = dstCoord.first + x;
	auto dstY = dstCoord.second + y;

	VideoDriver::GetInstance()->EnqueueFillRect(dstX, dstY, dstX, dstY, colour);
}

void Blitter_40bppAnimGPU::DrawRect(void *video, int width, int height, uint8_t colour)
{
	assert(VideoDriver::GetInstance()->SupportsGPUBlitting());
	
	std::pair<uint, uint> dstCoord = GetXYFromDst(video);
	auto x = dstCoord.first;
	auto y = dstCoord.second;

	VideoDriver::GetInstance()->EnqueueFillRect(x, y, x + width - 1, y + height - 1, colour);
}

void Blitter_40bppAnimGPU::DrawLine(void *video, int x, int y, int x2, int y2, [[maybe_unused]] int screen_width, [[maybe_unused]] int screen_height, uint8_t colour, int width, int dash)
{
	assert(VideoDriver::GetInstance()->SupportsGPUBlitting());

	std::pair<uint, uint> dstCoord = GetXYFromDst(video);
	auto dstX = dstCoord.first;
	auto dstY = dstCoord.second;

	VideoDriver::GetInstance()->EnqueueDrawLine(dstX + x, dstY + y, dstX + x2, dstY + y2, colour, width, dash);
}

/**
 * Draws a sprite to a (screen) buffer. Calls adequate templated function.
 *
 * @param bp further blitting parameters
 * @param mode blitter mode
 * @param zoom zoom level at which we are drawing
 */
void Blitter_40bppAnimGPU::Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom)
{
	assert(VideoDriver::GetInstance()->SupportsGPUBlitting());

	std::pair<uint, uint> dstCoord = GetXYFromDst(bp->dst);
	auto x = dstCoord.first;
	auto y = dstCoord.second;



	const SpriteData *spriteData = (const SpriteData *)bp->sprite;

	SpriteBlitRequest request;
	request.gpuSpriteID = spriteData->gpuSpriteID;
	request.left = bp->left + x;
	request.top = bp->top + y;
	request.right = request.left + bp->width - 1;
	request.bottom = request.top + bp->height - 1;
	request.skip_left = bp->skip_left;
	request.skip_top = bp->skip_top;
	request.zoom = zoom;
	request.blitterMode = mode;
	request.remap = bp->remap;


	VideoDriver::GetInstance()->EnqueueSpriteBlit(&request);
}

void Blitter_40bppAnimGPU::DrawColourMappingRect(void *dst, int width, int height, PaletteID pal)
{
	assert(VideoDriver::GetInstance()->SupportsGPUBlitting());

	std::pair<uint, uint> dstCoord = GetXYFromDst(dst);
	auto x = dstCoord.first;
	auto y = dstCoord.second;

	VideoDriver::GetInstance()->EnqueueDrawColourMappingRect(x, y, width, height, pal);
}

Sprite *Blitter_40bppAnimGPU::Encode(const SpriteLoader::SpriteCollection &sprite, AllocatorProc *allocator)
{
	return this->EncodeInternal(sprite, allocator);
}


void Blitter_40bppAnimGPU::CopyFromBuffer([[maybe_unused]] void *video, [[maybe_unused]] const void *src, int width, int height)
{
	assert(VideoDriver::GetInstance()->SupportsGPUBlitting());

	std::pair<uint, uint> dstCoord = GetXYFromDst(video);
	auto x = dstCoord.first;
	auto y = dstCoord.second;

	VideoDriver::GetInstance()->EnqueueCopyFromBackup(x, y, width, height);
}

void Blitter_40bppAnimGPU::CopyToBuffer([[maybe_unused]] const void *video, [[maybe_unused]] void *dst, int width, int height)
{
	assert(VideoDriver::GetInstance()->SupportsGPUBlitting());

	std::pair<uint, uint> dstCoord = GetXYFromDst(video);
	auto x = dstCoord.first;
	auto y = dstCoord.second;

	VideoDriver::GetInstance()->EnqueueCopyToBackup(x, y, width, height);
}

void Blitter_40bppAnimGPU::CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch)
{
	assert(VideoDriver::GetInstance()->SupportsGPUBlitting());

	std::pair<uint, uint> dstCoord = GetXYFromDst(video);
	auto x = dstCoord.first;
	auto y = dstCoord.second;

	VideoDriver::GetInstance()->CopyImageToBuffer(dst, x, y, width, height, dst_pitch);
}

void Blitter_40bppAnimGPU::ScrollBuffer([[maybe_unused]] void *video, int &left, int &top, int &width, int &height, int scroll_x, int scroll_y)
{
	VideoDriver::GetInstance()->ScrollBuffer(left, top, width, height, scroll_x, scroll_y);

	// Not sure why this code is here...
	if (scroll_y > 0) {
		/* Decrease height and increase top */
		top += scroll_y;
		height -= scroll_y;
		assert(height > 0);

		/* Adjust left & width */
		if (scroll_x >= 0) {
			left += scroll_x;
			width -= scroll_x;
		} else {
			width += scroll_x;
		}
	} else {
		/* Decrease height. (scroll_y is <=0). */
		height += scroll_y;
		assert(height > 0);

		/* Adjust left & width */
		if (scroll_x >= 0) {
			left += scroll_x;
			width -= scroll_x;
		} else {
			width += scroll_x;
		}
	}
}

size_t Blitter_40bppAnimGPU::BufferSize([[maybe_unused]] uint width, [[maybe_unused]] uint height)
{
	// We've already allocated backup buffers for dst and anim, so tell the caller we need no memory
	return 0;
}

Blitter::PaletteAnimation Blitter_40bppAnimGPU::UsePaletteAnimation()
{
	return Blitter::PALETTE_ANIMATION_VIDEO_BACKEND;
}

void Blitter_40bppAnimGPU::PaletteAnimate([[maybe_unused]] const Palette &palette)
{
	__debugbreak();
}

bool Blitter_40bppAnimGPU::NeedsAnimationBuffer()
{
	return true;
}

Sprite *Blitter_40bppAnimGPU::EncodeInternal(const SpriteLoader::SpriteCollection &sprite, AllocatorProc *allocator)
{
	Sprite *dest_sprite = (Sprite *)allocator(sizeof(*dest_sprite) + sizeof(SpriteData));
	dest_sprite->height = sprite[ZOOM_LVL_NORMAL].height;
	dest_sprite->width	= sprite[ZOOM_LVL_NORMAL].width;
	dest_sprite->x_offs = sprite[ZOOM_LVL_NORMAL].x_offs;
	dest_sprite->y_offs = sprite[ZOOM_LVL_NORMAL].y_offs;

	SpriteData *dst = (SpriteData *)dest_sprite->data;
	dst->gpuSpriteID = VideoDriver::GetInstance()->CreateGPUSprite(sprite);

	return dest_sprite;
}
