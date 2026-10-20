#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <iostream>
#include <stdexcept>
#include <vector>
#include <memory>
#include <atomic>
#include <set>
#include <algorithm>

#include <stb_image.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Utilities.h"
#include "Logger.h"
#include "Vertex.h"
#include "Mesh.h"

namespace Vulkanised
{
class VulkanRenderer
{
public:
	VulkanRenderer();

	int init(GLFWwindow* newWindow);

	// The new ECS-friendly rendering API
	uint32_t loadMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, int texId);
	void SubmitFrame(const std::vector<RenderInstance>& instances);

	void cleanup() noexcept;

	~VulkanRenderer();

private:
	// ECS OVERHAUL
	// Limits
	static constexpr uint32_t MAX_INSTANCES = 100000;
	static constexpr VkDeviceSize UNIFIED_BUFFER_SIZE = 64 * 1024 * 1024; // 64 MB for geometry

	// Asset Storage
	std::vector<MeshAsset> m_Meshes;

	// Unified Geometry Buffers (Replaces individual Mesh buffers)
	VkBuffer m_GlobalVertexBuffer;
	VkDeviceMemory m_GlobalVertexBufferMemory;
	uint32_t m_CurrentVertexOffset{ 0 };

	VkBuffer m_GlobalIndexBuffer;
	VkDeviceMemory m_GlobalIndexBufferMemory;
	uint32_t m_CurrentIndexOffset{ 0 };

	// SSBO for Instance Transforms (Replaces Push Constants)
	std::vector<VkBuffer> m_SSBO;
	std::vector<VkDeviceMemory> m_SSBOMemory;
	std::vector<void*> m_SSBOMapped;

	// New internal setup functions
	void createUnifiedBuffers();
	void createSSBOs();


	// GLFW Window
	VAtomic<GLFWwindow> m_Window;

	int m_CurrentFrame{ 0 };

	// Scene objects


	// Scene settings
	struct UboViewProjection {
		glm::mat4 projection;
		glm::mat4 view;
		glm::vec4 lightPos;
		glm::vec4 viewPos;
		glm::vec4 lightColor;
	} m_UboViewProjection;	// Everything is either a mat4 or vec4, for alignment purposes with the standard

	// Vulkan Components
	// - Main
	VkInstance m_Instance;
	struct {
		VkPhysicalDevice physicalDevice;
		VkDevice logicalDevice;
	} m_MainDevice;
	VkQueue m_GraphicsQueue;
	VkQueue m_PresentationQueue;
	VkDebugUtilsMessengerEXT m_DebugMessenger;
	VkSurfaceKHR m_Surface;
	VkSwapchainKHR m_Swapchain;
	std::vector<Utilities::Vulkan::SwapchainImage> m_SwapchainImages;
	std::vector<VkFramebuffer> m_SwapchainFramebuffers;
	std::vector<VkCommandBuffer> m_CommandBuffers;

	VkImage m_DepthBufferImage;
	VkDeviceMemory m_DepthBufferImageMemory;
	VkImageView m_DepthBufferImageView;

	VkSampler m_TextureSampler;

	// - Descriptors
	VkDescriptorSetLayout m_DescriptorSetLayout;
	VkDescriptorSetLayout m_SamplerSetLayout;
	//VkPushConstantRange m_PushConstantRange;

	VkDescriptorPool m_DescriptorPool;
	VkDescriptorPool m_SamplerDescriptorPool;
	std::vector<VkDescriptorSet> m_DescriptorSets;
	std::vector<VkDescriptorSet> m_SamplerDescriptorSets;

	//std::vector<VkBuffer> m_UniformBuffer;
	//std::vector<VkDeviceMemory> m_UniformBufferMemory;

	std::vector<VkBuffer> m_VpUniformBuffer;
	std::vector<VkDeviceMemory> m_VpUniformBufferMemory;

	std::vector<VkBuffer> m_ModelDUniformBuffer;
	std::vector<VkDeviceMemory> m_ModelDUniformBufferMemory;

	//VkDeviceSize m_MinUniformBufferOffset;
	//size_t m_ModelUniformAlignment;
	//UboModel* m_ModelTransferSpace;

	// - Assets
	std::vector<VkImage> m_TextureImages;
	std::vector<VkDeviceMemory> m_TextureImageMemory;
	std::vector<VkImageView> m_TextureImageViews;

	// - Pipeline
	VkPipeline m_GraphicsPipeline;
	VkPipelineLayout m_PipelineLayout;
	VkRenderPass m_RenderPass;

	// - Pools
	VkCommandPool m_GraphicsCommandPool;

	// - Utility
	VkFormat m_SwapchainImageFormat;
	VkExtent2D m_SwapchainExtent;

	// - Synchronisation
	std::vector<VkSemaphore> m_ImageAvailable;
	std::vector<VkSemaphore> m_RenderFinished;
	std::vector<VkFence> m_DrawFences;

	// Validation layer trackers
	std::uint16_t m_PerformanceMsgCount{ 0 };	// Validation layer detects that we can probably do better for performance
	std::uint16_t m_ValidationMsgCount{ 0 };	// Means we broke a Vulkan rule, we ought to look into it
	std::uint16_t m_ErrorMsgCount{ 0 };			// We probably fucked up, ideally this stays at 0...
	std::uint16_t m_WarningMsgCount{ 0 };		// Amount of things we should look into;

	// - Validation tracker printers
	void logTrackers() const noexcept;

	// Vulkan Functions
	// - Create functions
	void createInstance();
	void createLogicalDevice();
	void setupDebugMessenger();
	void createSurface();
	void createSwapchain();
	void createRenderPass();
	void createDescriptorSetLayout();
	void createGraphicsPipeline();
	void createDepthBufferImage();
	void createFramebuffers();
	void createCommandPool();
	void createCommandBuffers();
	void createTextureSampler();

	void createUniformBuffers();
	void createDescriptorPool();
	void createDescriptorSets();

	void createSynchronisation();

	void updateUniformBuffers(uint32_t imageIndex);			// Called in draw()

	// - Resizable window related
	void cleanupSwapchain();	// Called in draw or cleanup
	void recreateSwapchain();	// Called in draw, if window size changed

	// - Record functions
	void recordCommands(uint32_t currentImage, const std::vector<RenderInstance>& instances);				// Now also called in draw()

	// - Get functions
	void getPhysicalDevice();

	// - Allocate Functions
	void allocateDynamicBufferTransferSpace();

	// - Support functions
	// -- Checker functions
	bool checkInstanceExtensionSupport(const std::vector<const char*>* checkExtensions);
	bool checkDeviceExtensionSupport(VkPhysicalDevice device);
	bool checkValidationLayerSupport();
	bool checkDeviceSuitable(VkPhysicalDevice device);


	// -- Getter functions
	Utilities::Vulkan::QueueFamilyIndices getQueueFamilies(VkPhysicalDevice device);
	void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData);
	Utilities::Vulkan::SwapchainDetails getSwapchainDetails(VkPhysicalDevice device);

	// -- Choose functions
	VkSurfaceFormatKHR chooseBestSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);
	VkPresentModeKHR chooseBestPresentationMode(const std::vector<VkPresentModeKHR>& presentationModes);
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities);
	VkFormat chooseSupportedFormat(const std::vector<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags featureFlags) const;

	// -- Create functions
	VkImage createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags useFlags, VkMemoryPropertyFlags propFlags, VkDeviceMemory* imageMemory) const;
	VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const;
	VkShaderModule createShaderModule(const std::vector<uint32_t>& code) const;

	public:
	int createTextureImage(std::string_view fileName);
	int createTexture(std::string_view fileName);
	int createTextureDescriptor(VkImageView textureImage);
	private:
	// -- Loader Functions
	stbi_uc* loadTextureFile(std::string_view fileName, int* width, int* height, VkDeviceSize* imageSize);
};
}

#endif // !VULKANRENDERER_H
