#include "Vulkanised/Mesh.h"

Vulkanised::Mesh::Mesh()
{
}

Vulkanised::Mesh::Mesh(
		VkPhysicalDevice newPhysicalDevice, 
		VkDevice newDevice, 
		VkQueue transferQueue, 
		VkCommandPool transferCommandPool, 
		std::vector<Vertex>* vertices,
		std::vector<uint32_t>* indices,
		int newTexId
	)
	: m_Model(glm::mat4(1.0f)), m_TexId(newTexId), m_VertexCount(vertices->size()), m_IndexCount(indices->size()), m_PhysicalDevice(newPhysicalDevice), m_LogicalDevice(newDevice)
{
	LOG("A new mesh joins the ranks...");
	createVertexBuffer(transferQueue, transferCommandPool, vertices);
	createIndexBuffer(transferQueue, transferCommandPool, indices);
}

Vulkanised::Mesh::~Mesh()
{
	LOG("A mesh has begun its destruction!");

	if (m_VertexBuffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(m_LogicalDevice, m_VertexBuffer, nullptr);
		m_VertexBuffer = VK_NULL_HANDLE;
	}
	if (m_VertexBufferMemory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_LogicalDevice, m_VertexBufferMemory, nullptr);
		m_VertexBufferMemory = VK_NULL_HANDLE;
	}

	if (m_IndexBuffer != VK_NULL_HANDLE)
	{
		vkDestroyBuffer(m_LogicalDevice, m_IndexBuffer, nullptr);
		m_IndexBuffer = VK_NULL_HANDLE;
	}
	if (m_IndexBufferMemory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_LogicalDevice, m_IndexBufferMemory, nullptr);
		m_IndexBufferMemory = VK_NULL_HANDLE;
	}
}

void Vulkanised::Mesh::createVertexBuffer(VkQueue transferQueue, VkCommandPool transferCommandPool, std::vector<Vertex>* vertices)
{
	// Get size of buffer needed for vertices
	VkDeviceSize bufferSize{sizeof(Vertex) * vertices->size()};

	// Temporary buffer to "stage" vertex data before transferring to GPU
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;

	// Create staging buffer and allocate memory to it
	Utilities::Vulkan::createBuffer(
		m_PhysicalDevice, 
		m_LogicalDevice, 
		bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
		&stagingBuffer, 
		&stagingBufferMemory
	);
	

	// MAP MEMORY TO VERTEX BUFFER
	void* data;																				// 1. Create pointer to a point in normal memory
	vkMapMemory(m_LogicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);				// 2. "Map" the vertex buffer memory to that point
	memcpy(data, vertices->data(), (size_t)bufferSize);										// 3. Copy memory from vertices vector to the point
	vkUnmapMemory(m_LogicalDevice, stagingBufferMemory);									// 4. Unmap the vertex buffer memory

	// Create buffer with TRANSFER_DST_BIT to mark as recipient of transfer data (also, actual Vertex Buffer)
	// Buffer memory is to be DEVICE_LOCAL_BIT, meaning memory is on the GPU and only accessible by it and not CPU (host)
	Utilities::Vulkan::createBuffer(
		m_PhysicalDevice, 
		m_LogicalDevice, 
		bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
		&m_VertexBuffer, 
		&m_VertexBufferMemory
	);

	// Copy staging buffer to vertex buffer on GPU
	Utilities::Vulkan::copyBuffer(m_LogicalDevice, transferQueue, transferCommandPool, stagingBuffer, m_VertexBuffer, bufferSize);

	// Clean up staging buffer parts
	vkDestroyBuffer(m_LogicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(m_LogicalDevice, stagingBufferMemory, nullptr); 
}

void Vulkanised::Mesh::createIndexBuffer(VkQueue transferQueue, VkCommandPool transferCommandPool, std::vector<uint32_t>* indices)
{
	// Get size of buffer needed for indices
	VkDeviceSize bufferSize{ sizeof(uint32_t) * indices->size() };

	// Temporary buffer to "stage" vertex data before transferring to GPU
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	Utilities::Vulkan::createBuffer(
		m_PhysicalDevice, 
		m_LogicalDevice, 
		bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&stagingBuffer,
		&stagingBufferMemory
	);

	// MAP MEMORY TO INDEX BUFFER
	void* data;
	vkMapMemory(m_LogicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, indices->data(), (size_t)bufferSize);
	vkUnmapMemory(m_LogicalDevice, stagingBufferMemory);

	// Create buffer for INDEX data on GPU access memory area
	Utilities::Vulkan::createBuffer(
		m_PhysicalDevice,
		m_LogicalDevice,
		bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		&m_IndexBuffer,
		&m_IndexBufferMemory
	);

	// Copy from staging buffer to GPU access buffer
	Utilities::Vulkan::copyBuffer(
		m_LogicalDevice,
		transferQueue,
		transferCommandPool,
		stagingBuffer,
		m_IndexBuffer,
		bufferSize
	);

	// Destroy & release staging buffer resources
	vkDestroyBuffer(m_LogicalDevice, stagingBuffer, nullptr);
	vkFreeMemory(m_LogicalDevice, stagingBufferMemory, nullptr);
}
