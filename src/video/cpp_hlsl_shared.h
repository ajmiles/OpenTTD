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
	int left;
	int top;
	int right;
	int bottom;
	int skip_left;
	int skip_top;
	uint colour;
	BlitType blitType;
	uint gpuSpriteID;
	uint zoom;
	uint blitterMode;
	uint remapByteOffset;
	
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