#ifndef VERTEX_H
#define VERTEX_H

#include <array>

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

namespace Vulkanised 
{

// Vertex data representation
struct Vertex
{
	glm::vec3 pos;		// Vertex position (x, y, z)
	glm::vec3 col;		// Vertex colour (r, g, b)
	glm::vec2 tex;		// Texture Coords (u, v)

	// How the data for a single vertex (including info such as position, colour, texture coordinates, normals, etc...) is as a whole
	[[nodiscard]] static inline VkVertexInputBindingDescription getBindingDescription() noexcept
	{
		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;									// Can bind multiple streams of data, this defines which one
		bindingDescription.stride = sizeof(Vertex);						// Size of a single vertex object
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;		// How to move between data after each vertex
																		// VK_VERTEX_INPUT_RATE_INDEX		: Move on to the next vertex
																		// VK_VERTEX_INPUT_RATE_INSTANCE	: Move to a vertex for the next instance

		return bindingDescription;
	}

	// How the data for an attribute is defined within a vertex
	[[nodiscard]] static inline std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() noexcept
	{
		std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

		// Position attribute
		attributeDescriptions[0].binding = 0;							// Which binding the data is at (should be same as above)
		attributeDescriptions[0].location = 0;							// Location in shader where data will be read from
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;	// Format the data will take (also helps define size of data, happens to match vec3)
		attributeDescriptions[0].offset = offsetof(Vertex, pos);		// Where this attribute is defined in the data for a single vertex

		// Colour attribute
		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, col);

		// Texture attribute
		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(Vertex, tex);

		return attributeDescriptions;
	}
};

}

#endif // !VERTEX_H
