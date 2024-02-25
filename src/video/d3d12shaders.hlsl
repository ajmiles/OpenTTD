#define RootSig "RootConstants(b0, num32bitconstants=12), DescriptorTable(UAV(u0, numdescriptors=3, flags = DESCRIPTORS_VOLATILE), SRV(t0, numdescriptors=3)), DescriptorTable(SRV(t0, space=1, numdescriptors=unbounded, flags = DESCRIPTORS_VOLATILE)), SRV(t3), RootConstants(b1, num32bitconstants=2)"

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

RWTexture2D<uint> remap_tex : register(u1);
RWTexture2D<uint> dst_tex : register(u2);
RasterizerOrderedTexture2D<uint> remap_rov : register(u1);
RasterizerOrderedTexture2D<uint> dst_rov : register(u2);

Buffer<uint> palette[3] : register(t0);
ByteAddressBuffer remap_buffer : register(t3);

Texture2D<uint2> spriteTextures[] : register(t0,space1);

static const uint SHADER_MODE_REMAP = 0;
static const uint SHADER_MODE_PALETTE = 1;
static const uint SHADER_MODE_PROGRAM = 2;

cbuffer Params : register(b0)
{
	uint shaderMode;
	uint frameIndex;
	uint cursorLocationX;
	uint cursorLocationY;
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
		float4 remap_col = MakeColour(palette[frameIndex][vpos.x ]).ToFloat4();
		return float4(remap_col.rgb, 1);
	}
	
	float2 cursorPos = float2(cursorLocationX, cursorLocationY);
	
	if(length(cursorPos - vpos.xy) < 5)
		return float4(1, 0, 0, 1);
	
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
cbuffer BlitRequest : register(b0)
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
};

cbuffer BlitParamsConstantForPass : register(b1)
{
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

Colour RealizeBlendedColour(uint2 screenCoord, bool readROV)
{	
	Colour c = MakeColour(readROV ? dst_rov[screenCoord] : dst_tex[screenCoord]);
	uint anim = readROV ? remap_rov[screenCoord] : remap_tex[screenCoord];
	
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

uint MakeDark(uint r, uint g, uint b)
{
	return ((r * 13063) + (g * 25647) + (b * 4981)) / 65536;
}

Colour MakeDark(Colour colour)
{
	uint d = MakeDark(colour.Red(), colour.Green(), colour.Blue());
	return MakeColour(d, d, d);
}

uint LoadRemapValue(uint m)
{
	uint dword = remap_buffer.Load<uint>((m & ~3) + remapByteOffset);	// Byte address!
	uint byte = dword >> ((m % 4) * 8);
	return byte & 0xFF;
}

struct BlitOutput
{
	uint dst;
	uint anim;
	bool writeDst;
	bool writeAnim;
	bool zeroAlpha;
	
	void SetDst(uint val)
	{
		dst = val;
		writeDst = true;
	}
	
	void SetAnim(uint val)
	{
		anim = val;
		writeAnim = true;
	}
};

BlitOutput CalculateOutputs(uint2 DTid, uint2 screenCoord, bool readROV)
{
	BlitOutput blitOutput = { 0, 0, false, false, false };
	
	if(blitType == SOLID_COLOUR)
	{
		blitOutput.SetDst(MakeColour(0, 0, 0, 255).data);
		blitOutput.SetAnim(colour);
	}
	else if(blitType >= SPRITE_OPAQUE)
	{
		uint2 skipAmount = uint2(skip_left, skip_top);	
		uint2 spriteLocalCoord = DTid + skipAmount;
		uint textureID = (gpuSpriteID * 6) + zoom;
		uint2 spriteTexelData = spriteTextures[textureID][spriteLocalCoord];
		
		uint m = spriteTexelData.r;
		uint a = spriteTexelData.g;
		// TODO RGB
		blitOutput.zeroAlpha = (a == 0);
		
		if(blitOutput.zeroAlpha)
			return blitOutput;
		
		Colour src_px = MakeColour(0, 0, 0, a);
												
		switch(blitterMode)
		{
			case BM_BLACK_REMAP:	// Done
			{
				blitOutput.SetDst(MakeColour(0, 0, 0, 255).data);
				blitOutput.SetAnim(0);
			}break;
			case BM_NORMAL:			// Done?
			{		
				if(src_px.Alpha() == 255)
				{
					blitOutput.SetDst(src_px.data);	
					blitOutput.SetAnim(m);
				}
				else
				{
					Colour b = RealizeBlendedColour(screenCoord, readROV);
					
					if(m == 0)
					{						
						blitOutput.SetDst(ComposeColourRGBANoCheck(src_px, src_px.Alpha(), b).data);	
						blitOutput.SetAnim(0);
					}
					else
					{
						Colour remap_col = MakeColour(palette[blitFrameIndex][m]);
												
						blitOutput.SetDst(ComposeColourPANoCheck(remap_col, src_px.Alpha(), b).data);	
						blitOutput.SetAnim(m);
					}
				}
			}break;
			case BM_COLOUR_REMAP:
			case BM_CRASH_REMAP:
			{
				// TODO Need the remaps... to actually remap
				if(src_px.Alpha() == 255)
				{
					if(m == 0)
					{						
						blitOutput.SetDst((blitterMode == BM_CRASH_REMAP) ? MakeDark(src_px).data : src_px.data);	
						blitOutput.SetAnim(0);
					}
					else
					{
						uint r = LoadRemapValue(m);
						if(r != 0)
						{							
							blitOutput.SetDst(src_px.data);	
							blitOutput.SetAnim(r);
						}
					}
				}
				else
				{
					Colour b = RealizeBlendedColour(screenCoord, readROV);
					
					if(m == 0)
					{
						Colour c = MakeColour((blitterMode == BM_CRASH_REMAP) ? MakeDark(src_px).data : src_px.data);
												
						blitOutput.SetDst(ComposeColourRGBANoCheck(c, src_px.Alpha(), b).data);	
						blitOutput.SetAnim(0);
					}
					else
					{
						uint r = LoadRemapValue(m);
						
						if(r != 0)
						{
							Colour remap_col = MakeColour(palette[blitFrameIndex][r]);
							
							blitOutput.SetDst(ComposeColourPANoCheck(remap_col, src_px.Alpha(), b).data);	
							blitOutput.SetAnim(0);							
						}
					}
				}
			}break;
			case BM_TRANSPARENT:
			{
				if(src_px.Alpha() == 255)
				{
					Colour dst;
					uint anim;
					
					if(readROV)	// Compile time resolvable branch
					{
						dst = MakeColour(dst_rov[screenCoord]);
						anim = remap_rov[screenCoord];
					}
					else
					{
						dst = MakeColour(dst_tex[screenCoord]);
						anim = remap_tex[screenCoord];
					}
					
					Colour b = dst;
					
					if(anim != 0)
						b = MakeColour(GetColourBrightness(dst), 0, 0);
					
					blitOutput.SetDst(MakeTransparent(b, 3, 4).data);	
				}
				else
				{
					Colour b = RealizeBlendedColour(screenCoord, readROV);
										
					blitOutput.SetDst(MakeTransparent(b, (256 * 4 - src_px.Alpha()), 256 * 4).data);	
					blitOutput.SetAnim(0);
				}
			}break;
			case BM_TRANSPARENT_REMAP:
			{
				uint anim = readROV ? remap_rov[screenCoord] : remap_tex[screenCoord];
				
				if(anim != 0)
					blitOutput.SetAnim(LoadRemapValue(anim));
				else
				{
					// A Search?!?!
					blitOutput.SetDst(MakeColour(255, 0, 255, 255).data);
					blitOutput.SetAnim(0);
				}
			}break;			
		}
	}
	
	return blitOutput;
}

[RootSignature(RootSig)]
[numthreads(8,8,1)]
void BlitCS(uint2 DTid : SV_DispatchThreadID)
{
	uint2 blitDims = GetBlitDimensions();
	
	if(any(DTid.xy >= blitDims))
		return;
			
	uint2 screenCoord = DTid.xy + uint2(left, top);
	
	BlitOutput output = CalculateOutputs(DTid, screenCoord, false);
	
	if(output.writeDst)
		dst_tex[screenCoord] = output.dst;
	if(output.writeAnim)
		remap_tex[screenCoord] = output.anim;
}

[RootSignature(RootSig)]
void ROVPS(float4 vpos : SV_POSITION)
{
	uint2 screenCoord = vpos.xy;
	uint2 DTid = screenCoord - uint2(left, top);
	
	BlitOutput output = CalculateOutputs(DTid, screenCoord, true);
	
	if(output.writeDst)
		dst_rov[screenCoord] = output.dst;
	if(output.writeAnim)
		remap_rov[screenCoord] = output.anim;
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