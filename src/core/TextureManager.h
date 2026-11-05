#pragma once

// sampler name / file path -> texture, and sampler name -> the slot holding a texture that may be swapped
typedef std::map<std::string, IDirect3DBaseTexture9*>	TextureList;
typedef std::map<std::string, IDirect3DBaseTexture9**>	TexturePointersList;
typedef std::vector<TextureRecord*>						WaterMapList;

// Owns NVR's screen-sized buffers, the named texture slots shaders bind by sampler name, and the cache of texture files.
// Created once at startup (Initialize) and kept for the whole session.
class TextureManager {
public:
	static void				Initialize();

	void					InitTexture(const char* Name, IDirect3DTexture9** Texture, IDirect3DSurface9** Surface, int Width, int Height, D3DFORMAT Format, bool mipmaps = false);
	void					RegisterTexture(const char* Name, IDirect3DBaseTexture9** Slot);
	void					SetWaterHeightMap(IDirect3DBaseTexture9* HeightMap);
	void					SetWaterReflectionMap(IDirect3DBaseTexture9* ReflectionMap);
	IDirect3DBaseTexture9*	GetFileTexture(std::string TexturePath, TextureRecord::TextureRecordType Kind);
	IDirect3DBaseTexture9*	GetCachedTexture(std::string& path);
	IDirect3DBaseTexture9*	GetTextureByName(std::string& Name);
	IDirect3DBaseTexture9**	GetTextureSlotByName(std::string& Name);
	void					DumpToFile(IDirect3DTexture9* Source, const char* FileName);

	// screen buffers (texture + its top surface where it is a render target)
	IDirect3DTexture9		*SourceTexture;
	IDirect3DSurface9		*SourceSurface;
	IDirect3DTexture9		*RenderedTexture;
	IDirect3DSurface9		*RenderedSurface;
	IDirect3DTexture9		*BloomTexture;
	IDirect3DSurface9		*BloomSurface;
	IDirect3DTexture9		*DepthTexture, *DepthTextureViewModel;	// readable (INTZ) depth: world, first-person model
	IDirect3DTexture9*		TerrainBlendDepthTexture = nullptr;	// UNOFFICIAL terrain blending: the world's depth as the land starts (Terrain.h)
	IDirect3DTexture9*		ParticleDepthTexture = nullptr;	// UNOFFICIAL 2026-11-04 soft particles: the world's depth as the first lit particle draws (Particles.h)
	IDirect3DSurface9		*CombinedDepthSurface;
	IDirect3DTexture9		*CombinedDepthTexture, *DepthTextureINTZ;
	IDirect3DSurface9		*DepthSurface;

	TextureList				TextureCache;		// loaded files by path
	TexturePointersList		TextureNames;		// sampler name -> slot
	WaterMapList			WaterHeightMapTextures, WaterReflectionMapTextures;	// records that follow the game's water maps
	IDirect3DBaseTexture9	*WaterHeightMapB, *WaterReflectionMapB;			// the game's current water maps
};
