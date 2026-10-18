#ifndef MESH_H
#define MESH_H

#include <vector>
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "Vertex.h"
#include "Logger.h"
#include "Utilities.h"

namespace Vulkanised
{

struct Model {
	glm::mat4 model;
};

class Mesh
{
public:
	Mesh();
	Mesh(
		VkPhysicalDevice newPhysicalDevice, 
		VkDevice newDevice, 
		VkQueue transferQueue, 
		VkCommandPool transferCommandPool, 
		std::vector<Vertex>* vertices,
		std::vector<uint32_t>* indices,
		int newTexId
	);

	inline void setModel(glm::mat4 newModel) noexcept { m_Model.model = newModel; }
	[[nodiscard]] inline const Model& getModel() const noexcept { return m_Model; }

	[[nodiscard]] inline int getTexId() const noexcept { return m_TexId; }

	[[nodiscard]] inline constexpr size_t getVertexCount() const noexcept { return m_VertexCount; }
	[[nodiscard]] inline constexpr VkBuffer getVertexBuffer() const noexcept { return m_VertexBuffer; }

	[[nodiscard]] inline constexpr size_t getIndexCount() const noexcept {return m_IndexCount; }
	[[nodiscard]] inline constexpr VkBuffer getIndexBuffer() const noexcept { return m_IndexBuffer; }

	~Mesh();

	// C++ over here making shallow fucking copies of my meshes ffs, at least now someone trying to just assign a mesh with "=" will not even compile
	Mesh(const Mesh&) = delete;
	Mesh& operator=(const Mesh&) = delete;
private:
	Model m_Model;
	int m_TexId;

	size_t m_VertexCount{ 0 };
	VkBuffer m_VertexBuffer;
	VkDeviceMemory m_VertexBufferMemory;

	size_t m_IndexCount{ 0 };
	VkBuffer m_IndexBuffer;
	VkDeviceMemory m_IndexBufferMemory;

	VkPhysicalDevice m_PhysicalDevice;
	VkDevice m_LogicalDevice;
	
	void createVertexBuffer(VkQueue transferQueue, VkCommandPool transferCommandPool, std::vector<Vertex>* vertices);
	void createIndexBuffer(VkQueue transferQueue, VkCommandPool transferCommandPool, std::vector<uint32_t>* indices);

	// This one has been moved to the Vulkan Utilities namespace (as a more general one)
	//uint32_t findMemoryTypeIndex(uint32_t allowedTypes, VkMemoryPropertyFlags properties) const noexcept;
};

}

#endif // !MESH_H
