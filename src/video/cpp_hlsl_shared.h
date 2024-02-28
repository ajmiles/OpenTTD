#pragma once

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

