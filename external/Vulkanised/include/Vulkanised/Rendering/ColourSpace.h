#ifndef VULKANISED_RENDERING_COLOURSPACE_H
#define VULKANISED_RENDERING_COLOURSPACE_H

#include <cmath>

#include <glm/glm.hpp>

/*
* Colours people pick (colour pickers, JSON files, image editors) are sRGB: their numbers are perceptual, not physical.
* Lighting maths only works on LINEAR values, so picked colours are converted before the GPU uses them, and the sRGB
* render target converts the result back on write. A picked colour therefore shows up on screen exactly as picked.
*/
namespace Vulkanised::Rendering
{

[[nodiscard]] inline float convertSrgbChannelToLinear(float SrgbChannel) noexcept
{
	// The exact sRGB curve (a short linear toe, then a 2.4 power), not the 2.2 approximation
	return SrgbChannel <= 0.04045f ? SrgbChannel / 12.92f : std::pow((SrgbChannel + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline glm::vec3 convertSrgbColourToLinear(const glm::vec3& SrgbColour) noexcept
{
	return { convertSrgbChannelToLinear(SrgbColour.r), convertSrgbChannelToLinear(SrgbColour.g), convertSrgbChannelToLinear(SrgbColour.b) };
}

}

#endif // !VULKANISED_RENDERING_COLOURSPACE_H
