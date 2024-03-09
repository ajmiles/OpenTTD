#pragma once

//#define SEPARATE_DRAWS 1

static const uint SHADER_MODE_REMAP = 0;
static const uint SHADER_MODE_PALETTE = 1;
static const uint SHADER_MODE_PROGRAM = 2;

enum BlitType {
	
	// These five are supporting by drawing a rectangle
	RECTANGLE,
	COLOUR_MAPPING_RECTANGLE,
	COPY_TO_BACKUP,
	COPY_FROM_BACKUP,
	SPRITE,
	
	// Ones below this point are not
	LINE,
	
	BLIT_TYPE_COUNT
};


struct BlitRequest
{
	int left : 16;
	int top: 16;
	int right: 16;
	int bottom: 16;
	int skip_left: 16;
	int skip_top: 16;
	uint colour : 8;
	BlitType blitType : 3;
	uint gpuSpriteID : 17;
	uint zoom : 4;
	uint blitterMode : 3;
	uint remapByteOffset : 13;	// More than it needs to be
	
#if !defined(__cplusplus)
	uint2 GetBlitDimensions()
	{
		return uint2(right - left, bottom - top) + 1;
	}
	
	uint2 GetXYOffset()
	{
		return uint2(left, top);
	}
	
	uint2 GetSkipOffset()
	{
		return uint2(skip_left, skip_top);
	}
#endif
};
