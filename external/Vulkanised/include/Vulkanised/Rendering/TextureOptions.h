#ifndef VULKANISED_RENDERING_TEXTUREOPTIONS_H
#define VULKANISED_RENDERING_TEXTUREOPTIONS_H

namespace Vulkanised::Rendering
{

// What the texture's numbers mean
enum class TextureColourSpace
{
	Colour,	// Something you look at (albedo/base colour, sprites, UI art): stored as sRGB, decoded to linear when sampled
	Data	// Numbers that aren't colours (normal maps, roughness, masks): used as they are
};

// How it looks up close
enum class TextureFiltering
{
	Smooth,		// Linear filtering with mipmaps and 16x anisotropy: photos, painted textures, anything realistic
	Pixelated	// Nearest neighbour: crisp pixel art (mipmaps are still used far away, picked without blending)
};

struct TextureOptions
{
	TextureColourSpace m_ColourSpace{ TextureColourSpace::Colour };
	TextureFiltering m_Filtering{ TextureFiltering::Smooth };
	bool m_ShouldGenerateMipmaps{ true };	// Without them, distant or angled textures shimmer and sampling them is slower
};

}

#endif // !VULKANISED_RENDERING_TEXTUREOPTIONS_H
