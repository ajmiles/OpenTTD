#define RootSig "RootConstants(b0, num32bitconstants=13), DescriptorTable(UAV(u0, numdescriptors=3, flags = DESCRIPTORS_VOLATILE), SRV(t0, numdescriptors=2)), DescriptorTable(SRV(t0, space=1, numdescriptors=unbounded))"

enum BlitType
{
	SOLID_COLOUR,
	SPRITE_OPAQUE,
	SPRITE_ALPHA
};

/** The modes of blitting we can do. */
enum BlitterMode {
	BM_NORMAL,       ///< Perform the simple blitting.
	BM_COLOUR_REMAP, ///< Perform a colour remapping.
	BM_TRANSPARENT,  ///< Perform transparency darkening remapping.
	BM_TRANSPARENT_REMAP, ///< Perform transparency colour remapping.
	BM_CRASH_REMAP,  ///< Perform a crash remapping.
	BM_BLACK_REMAP,  ///< Perform remapping to a completely blackened sprite
};

struct Colour
{
	uint data;
			
	uint Red()
	{
		return (data >> 16) & 0xFF;
	}
	
	uint Green()
	{
		return (data >> 8) & 0xFF;
	}
	
	uint Blue()
	{
		return (data >> 0) & 0xFF;
	}
	
	uint Alpha()
	{
		return data >> 24;
	}
	
	float4 ToFloat4()
	{
		return float4(Red(), Green(), Blue(), Alpha()) / 255.0f;
	}
};

Colour MakeColour(uint val)
{
	Colour c = { val };
	return c;
}

Colour MakeColour(uint r, uint g, uint b, uint a = 0xFF)
{
	uint packed = (a << 24) | (r << 16) | (g << 8) | b;
	Colour c = { packed };
	return c;
} 

Colour MakeColour(float4 colour)
{
	colour *= 255.0f;
	return MakeColour(colour.r, colour.g, colour.b, colour.a);
}

RWTexture2D<float4> colour_tex : register(u0);
RWTexture2D<uint> remap_tex : register(u1);
RWTexture2D<uint> dst_tex : register(u2);
Buffer<uint> palette[2] : register(t0);

Texture2D<uint2> spriteTextures[] : register(t0,space1);

static const uint SHADER_MODE_REMAP = 0;
static const uint SHADER_MODE_PALETTE = 1;
static const uint SHADER_MODE_PROGRAM = 2;

cbuffer Params
{
	uint shaderMode;
	uint frameIndex;
};

float max3(float3 v) 
{
	return max(max(v.x, v.y), v.z);
}

float3 adj_brightness(float3 colour, float brightness)
{
	float3 adj = colour * (brightness > 0.0 ? brightness / 0.5 : 1.0);
	float3 ob_vec = clamp(adj - 1.0, 0.0, 1.0);
	float ob = (ob_vec.r + ob_vec.g + ob_vec.b) / 2.0;

	return clamp(adj + ob * (1.0 - adj), 0.0, 1.0);
}

[RootSignature(RootSig)]
float4 mainVS(uint vid : SV_VertexID) : SV_POSITION
{
	return float4(vid == 1 ? 3 : -1, vid == 2 ? -3 : 1, 0, 1);
}

[RootSignature(RootSig)]
float4 mainPS(float4 vpos : SV_POSITION) : SV_TARGET
{
	uint2 coord = vpos.xy;
	
	// Debug - to show palette!
	if(vpos.y < 50 && vpos.x < 256)
	{
		float4 remap_col = MakeColour(palette[frameIndex][vpos.x]).ToFloat4();
		return float4(remap_col.rgb, 1);
	}
	
	uint idx = remap_tex[coord];	
	Colour remap_col = MakeColour(palette[frameIndex][idx]);
	Colour rgb_col = MakeColour(dst_tex[coord]);
	
	if(shaderMode == SHADER_MODE_PALETTE)
		return remap_col.ToFloat4();
	else if(shaderMode == SHADER_MODE_PROGRAM)
		return rgb_col.ToFloat4();
	else
	{	
		float4 remap_col_f = remap_col.ToFloat4();
		float4 rgb_col_f = rgb_col.ToFloat4();
		float3 result = idx > 0 ? adj_brightness(remap_col_f.rgb, max3(rgb_col_f.rgb)) : rgb_col_f.rgb;
				
		return float4(result, 1);
	}
}

// TODO Separate this out
cbuffer BlitParams
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
	uint screenResolution;
	uint blitFrameIndex;
};

uint2 GetBlitDimensions()
{
	return uint2(right - left, bottom - top) + 1;
}

float2 GetScreenResolution()
{
	return float2(screenResolution & 0xFFFF, screenResolution >> 16);
}

[RootSignature(RootSig)]
float4 DrawVS(uint vid : SV_VertexID, uint instanceID : SV_InstanceID) : SV_POSITION
{
	uint x = (vid >= 1 && vid <= 3) ? 1 : 0;
	uint y = (vid >= 2 && vid <= 4) ? 1 : 0;
	
	uint2 blitDims = GetBlitDimensions();
	
	uint2 unitSquare = uint2(x, y);
	float2 scaledTranslationRectF = (unitSquare * blitDims + uint2(left, top));
	
	scaledTranslationRectF = scaledTranslationRectF / (GetScreenResolution() * 0.5f);
	scaledTranslationRectF.y = -scaledTranslationRectF.y;
	scaledTranslationRectF += float2(-1, 1);
		
	return float4(scaledTranslationRectF, 0, 1);
}

struct PS_OUTPUT
{
	uint dst : SV_TARGET0;
	uint anim : SV_TARGET1;
};

Colour LookupColourInPalette(uint index)
{
	Colour c;
	c.data = palette[blitFrameIndex][index];
	return c;
}

static const uint DEFAULT_BRIGHTNESS = 128;

template<typename T>
uint GB(T x, uint s, uint n)
{
	return (uint)((x >> s) & (((T)1U << n) - 1));
}

Colour ReallyAdjustBrightness(Colour colour, float brightness)
{
	uint64_t combined = (((uint64_t) colour.Red()) << 32) | (((uint64_t) colour.Green()) << 16) | ((uint64_t) colour.Blue());
	combined *= brightness; 
	
	uint r = GB(combined, 39, 9);
	uint g = GB(combined, 23, 9);
	uint b = GB(combined, 7, 9);
	
	if ((combined & 0x800080008000L) == 0L) {
		return MakeColour(r, g, b, colour.Alpha());
	}

	uint ob = 0;
	/* Sum overbright */
	if (r > 255) ob += r - 255;
	if (g > 255) ob += g - 255;
	if (b > 255) ob += b - 255;

	/* Reduce overbright strength */
	ob /= 2;
	return MakeColour(
		r >= 255 ? 255 : min(r + ob * (255 - r) / 256, 255),
		g >= 255 ? 255 : min(g + ob * (255 - g) / 256, 255),
		b >= 255 ? 255 : min(b + ob * (255 - b) / 256, 255),
		colour.Alpha());
}

Colour AdjustBrightness(Colour colour, uint brightness)
{
	if(brightness == DEFAULT_BRIGHTNESS)
		return colour;
	else
		return ReallyAdjustBrightness(colour, brightness);
}

uint GetColourBrightness(Colour colour)
{
	uint rgb_max = max(max(colour.Red(), colour.Green()), colour.Blue());
	
	return (rgb_max == 0) ? DEFAULT_BRIGHTNESS : rgb_max;
}

Colour RealizeBlendedColour(uint2 screenCoord)
{	
	Colour c = MakeColour(dst_tex[screenCoord]);
	uint anim = remap_tex[screenCoord];
	
	if(anim != 0)
		return AdjustBrightness(LookupColourInPalette(anim), GetColourBrightness(c));
	else
		return c;
}

Colour ComposeColourRGBANoCheck(Colour srcRGB, uint a, Colour current)
{
	uint r = srcRGB.Red();
	uint g = srcRGB.Green();
	uint b = srcRGB.Blue();
	
	uint cr = current.Red();
	uint cg = current.Green();
	uint cb = current.Blue();
	
	return MakeColour(
					((int)(r - cr) * a) / 256 + cr,
					((int)(g - cg) * a) / 256 + cg,
					((int)(b - cb) * a) / 256 + cb);
}

Colour ComposeColourPANoCheck(Colour colour, uint a, Colour current)
{
	return ComposeColourRGBANoCheck(colour, a, current);
}

Colour MakeTransparent(Colour colour, uint nom, uint denom = 256)
{
	uint r = colour.Red();
	uint g = colour.Green();
	uint b = colour.Blue();

	return MakeColour(r * nom / denom, g * nom / denom, b * nom / denom);
}

[RootSignature(RootSig)]
[numthreads(8,8,1)]
void BlitCS(uint2 DTid : SV_DispatchThreadID)
{
	uint2 blitDims = GetBlitDimensions();
	
	if(any(DTid.xy >= blitDims))
		return;
		
	uint2 screenCoord = DTid.xy + uint2(left, top);
	
	if(blitType == SOLID_COLOUR)
	{
		dst_tex[screenCoord] = MakeColour(0, 0, 0, 255).data;
		remap_tex[screenCoord] = colour;
	}
	else if(blitType >= SPRITE_OPAQUE)
	{
		uint2 skipAmount = uint2(skip_left, skip_top);	
		uint2 spriteLocalCoord = DTid + skipAmount;
		uint textureID = (gpuSpriteID * 6) + zoom;
		uint2 spriteTexelData = spriteTextures[textureID][spriteLocalCoord];
		
		uint m = spriteTexelData.r;
		uint a = spriteTexelData.g;
		
		if(a == 0)
			return;
		
		Colour src_px = MakeColour(0, 0, 0, a);
		
		uint bm = blitterMode;
								
		switch(bm)
		{
			case BM_BLACK_REMAP:	// Done
			{
				dst_tex[screenCoord] = MakeColour(0, 0, 0, 255).data;
				remap_tex[screenCoord] = 0;
			}break;
			case BM_NORMAL:			// Done?
			{		
				if(src_px.Alpha() == 255)
				{
					dst_tex[screenCoord] = src_px.data;	// TODO RGB??
					remap_tex[screenCoord] = m;
				}
				else
				{
					Colour b = RealizeBlendedColour(screenCoord);
					
					if(m == 0)
					{
						dst_tex[screenCoord] = ComposeColourRGBANoCheck(src_px, src_px.Alpha(), b).data;
						remap_tex[screenCoord] = 0;
					}
					else
					{
						Colour remap_col = MakeColour(palette[blitFrameIndex][m]);
						
						dst_tex[screenCoord] = ComposeColourPANoCheck(remap_col, src_px.Alpha(), b).data;
						remap_tex[screenCoord] = m;
					}
				}
			}break;
			case BM_COLOUR_REMAP:
			case BM_CRASH_REMAP:
			{
				// TODO Need the remaps... to actualy remap
				if(src_px.Alpha() == 255)
				{
					dst_tex[screenCoord] = src_px.data;	// TODO RGB??
					remap_tex[screenCoord] = m;
				}
				else
				{
					Colour b = RealizeBlendedColour(screenCoord);
					
					if(m == 0)
					{
						dst_tex[screenCoord] = ComposeColourRGBANoCheck(src_px, src_px.Alpha(), b).data;
						remap_tex[screenCoord] = 0;
					}
					else
					{
						Colour remap_col = MakeColour(palette[blitFrameIndex][m]);
						
						dst_tex[screenCoord] = ComposeColourPANoCheck(remap_col, src_px.Alpha(), b).data;
						remap_tex[screenCoord] = m;
					}
				}
			}break;
			case BM_TRANSPARENT:
			{
				if(src_px.Alpha() == 255)
				{
					Colour dst = MakeColour(dst_tex[screenCoord]);
					uint anim = remap_tex[screenCoord];
					
					Colour b = dst;
					
					if(anim != 0)
						b = MakeColour(GetColourBrightness(dst), 0, 0);
					
					dst_tex[screenCoord] = MakeTransparent(b, 3, 4).data;
				}
				else
				{
					Colour b = RealizeBlendedColour(screenCoord);
					
					dst_tex[screenCoord] = MakeTransparent(b, (256 * 4 - src_px.Alpha()), 256 * 4).data;
					remap_tex[screenCoord] = 0;
				}
			}break;
			case BM_TRANSPARENT_REMAP:
			{
				dst_tex[screenCoord] = MakeColour(0, 0, 255, 255).data;
				remap_tex[screenCoord] = 0;
			}break;			
		}
	}	
}

[RootSignature(RootSig)]
PS_OUTPUT DrawPS(float4 vpos : SV_POSITION)
{
	PS_OUTPUT output;
	
	// TODO - restore this if I revert back to using raster for blitting.
	
	output.dst = 0;
	output.anim = 0;
	
	return output;
}

cbuffer ScrollParams : register(b0)
{
	int scrollLeft;
	int scrollTop;
	int scrollWidth;
	int scrollHeight;
	int scrollX;
	int scrollY;
	int screenWidth;
	int screenHeight;
};

// TODO - investigate what the best thread group size is for this
#define THREAD_GROUP_SIZE 128

[RootSignature(RootSig)]
[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void ScrollXCS(uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
	// Positive scrollX means the screen contents need to be moved right.
	
	int2 topLeft = int2(scrollLeft, scrollTop);												// Inclusive left hand column
	int2 bottomRight = int2(scrollLeft + scrollWidth - 1, scrollTop + scrollHeight - 1);	// Inclusive right hand column
	int2 topRight = int2(bottomRight.x, topLeft.y);
		
	bool scrollingLeft = (scrollX < 0);
	
	int2 writeCoord = scrollingLeft ? (topLeft + int2(groupIndex, 0)) : (topRight - int2(groupIndex, 0));
	writeCoord.y += groupID.y;
	
	// Handles going left and right
	while(writeCoord.x >= topLeft.x && writeCoord.x <= topRight.x)
	{		
		int2 readCoord = writeCoord - int2(scrollX, 0);
		
		// This can read out of bounds, but return 0.
		uint dst = dst_tex[readCoord];		
		uint anim = remap_tex[readCoord];
		
		// Before anyone writes, we must have all read!
		GroupMemoryBarrierWithGroupSync();		
		
		dst_tex[writeCoord] = dst;
		remap_tex[writeCoord] = anim;
		
		if(scrollingLeft)
			writeCoord.x += THREAD_GROUP_SIZE;
		else
			writeCoord.x -= THREAD_GROUP_SIZE;
	}
}

[RootSignature(RootSig)]
[numthreads(1, THREAD_GROUP_SIZE, 1)]
void ScrollYCS(uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
	// Positive scrollY means the screen contents need to be moved down.
	int2 topLeft = int2(scrollLeft, scrollTop);												// Inclusive left hand column
	int2 bottomRight = int2(scrollLeft + scrollWidth - 1, scrollTop + scrollHeight - 1);	// Inclusive right hand column
	int2 bottomLeft = int2(topLeft.x, bottomRight.y);
		
	bool scrollingUp = (scrollY < 0);
	
	int2 writeCoord = scrollingUp ? (topLeft + int2(0, groupIndex)) : (bottomLeft - int2(0, groupIndex));
	writeCoord.x += groupID.x;
	
	// Handles going up and down
	while(writeCoord.y >= topLeft.y && writeCoord.y <= bottomLeft.y)
	{		
		int2 readCoord = writeCoord - int2(0, scrollY);
		
		// This can read out of bounds, but return 0.
		uint dst = dst_tex[readCoord];		
		uint anim = remap_tex[readCoord];
		
		// Before anyone writes, we must have all read!
		GroupMemoryBarrierWithGroupSync();		
		
		dst_tex[writeCoord] = dst;
		remap_tex[writeCoord] = anim;
		
		if(scrollingUp)
			writeCoord.y += THREAD_GROUP_SIZE;
		else
			writeCoord.y -= THREAD_GROUP_SIZE;
	}
}