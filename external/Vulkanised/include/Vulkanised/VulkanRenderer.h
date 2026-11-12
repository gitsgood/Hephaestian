#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <iostream>
#include <stdexcept>
#include <vector>
#include <memory>
#include <atomic>
#include <set>
#include <algorithm>
#include <optional>
#include <chrono>
#include <string_view>
#include <filesystem>

#include <stb_image.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Utilities.h"
#include "Logger.h"
#include "Vertex.h"
#include "Mesh.h"
#include "Rendering/GpuMemory.h"
#include "Rendering/FrameStatistics.h"
#include "Rendering/FrameResources.h"
#include "Rendering/ResourceUploader.h"
#include "Rendering/ColourSpace.h"
#include "Rendering/TextureOptions.h"
#include "utils/VulkanUtils.h"

#include <array>

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

	// Meshes and textures are uploaded in one batch at the start of the next SubmitFrame, without stalling. Call this to send
	// them right away AND wait until they are on the GPU (e.g. at the end of a loading screen).
	void flushPendingUploads() { m_ResourceUploader.submitPendingUploadsAndWait(); }

	// - Multi-mesh models
	// Creates a model out of meshes and textures that were already loaded. Returns the model identifier.
	uint32_t createModel(const std::vector<ModelSubmesh>& SubmeshesMakingUpTheModel);
	// Uploads every piece of geometry, then creates a model out of them. Returns the model identifier.
	uint32_t loadModel(const std::vector<ModelSubmeshGeometry>& SubmeshGeometriesMakingUpTheModel);
	// Convenience for the (very common) one mesh, one texture model.
	uint32_t createModelFromSingleMesh(uint32_t MeshIdentifier, uint32_t TextureIdentifier);
	[[nodiscard]] bool isModelIdentifierValid(uint32_t ModelIdentifier) const noexcept;
	// loadMesh returns 0 when it fails, which is also a valid identifier: compare this before/after to know if it worked
	[[nodiscard]] size_t getLoadedMeshCount() const noexcept { return m_Meshes.size(); }
	[[nodiscard]] const ModelAsset& getModelAsset(uint32_t ModelIdentifier) const;

	// - Camera
	// Sets what the next submitted frame will be seen from. The projection is rebuilt from these every frame using
	// the current swapchain size, so window resizes are handled here and nobody else has to care about aspect ratio.
	void setCameraViewAndLens(const glm::mat4& CameraViewMatrix, const glm::vec3& CameraWorldPosition, float VerticalFieldOfViewInRadians, float NearClippingPlaneDistance, float FarClippingPlaneDistance) noexcept;
	[[nodiscard]] float getSwapchainAspectRatio() const noexcept;

	// - Screenshots: the next submitted frame is also saved as a 32-bit .bmp (folders are created as needed)
	void requestScreenshot(const std::filesystem::path& OutputBitmapFilePath);
	[[nodiscard]] bool isScreenshotPending() const noexcept { return m_PendingScreenshotFilePath.has_value(); }

	// - Lighting (one point light; place it far away to make it behave like the sun)
	void setLightPosition(const glm::vec3& NewLightWorldPosition) noexcept { m_UboViewProjection.lightPos = glm::vec4(NewLightWorldPosition, 1.0f); }
	// Picked in sRGB (like every colour a person chooses), lit with in linear
	void setLightColour(const glm::vec3& NewLightColour) noexcept { m_UboViewProjection.lightColor = glm::vec4(Rendering::convertSrgbColourToLinear(NewLightColour), 1.0f); }

	// - Background
	void setClearColour(const glm::vec3& NewClearColour) noexcept { m_ClearColour = NewClearColour; }
	[[nodiscard]] const glm::vec3& getClearColour() const noexcept { return m_ClearColour; }

	// - Dear ImGui (optional, drawn on top of everything)
	// Call AFTER the application installed its own GLFW input callbacks: ImGui chains onto them, so both keep receiving input.
	void initialiseImGui(GLFWwindow* WindowToReceiveImGuiInput);
	// Call once per frame, before any ImGui:: window code. SubmitFrame then renders whatever was built.
	void beginImGuiFrame();
	[[nodiscard]] bool isImGuiInitialised() const noexcept { return m_IsImGuiInitialised; }

	// - Infinite grid (XZ plane at y = 0, follows the camera, fades out ~100 units away). Off by default.
	void setInfiniteGridVisible(bool ShouldInfiniteGridBeVisible) noexcept { m_IsInfiniteGridVisible = ShouldInfiniteGridBeVisible; }
	[[nodiscard]] bool isInfiniteGridVisible() const noexcept { return m_IsInfiniteGridVisible; }

	// - Diagnostics
	[[nodiscard]] const Rendering::FrameStatistics& getFrameStatistics() const noexcept { return m_FrameStatistics; }
	[[nodiscard]] Rendering::GpuMemoryStatistics getGpuMemoryStatistics() const { return m_GpuMemory.getStatistics(); }
	[[nodiscard]] std::string_view getGpuName() const noexcept { return m_PhysicalDeviceProperties.deviceName; }

	void cleanup() noexcept;

	~VulkanRenderer();

private:
	// ECS OVERHAUL
	// Limits
	static constexpr uint32_t MAX_INSTANCES = 100000;
	static constexpr VkDeviceSize UNIFIED_BUFFER_SIZE = 64 * 1024 * 1024; // 64 MB for geometry to start with, the buffers grow (doubling) when a mesh doesn't fit

	// Asset Storage
	std::vector<MeshAsset> m_Meshes;
	std::vector<ModelAsset> m_Models;

	// Camera lens settings (the view matrix itself lives in m_UboViewProjection)
	float m_CameraVerticalFieldOfViewInRadians{ glm::radians(45.0f) };
	float m_CameraNearClippingPlaneDistance{ 0.1f };
	float m_CameraFarClippingPlaneDistance{ 100.0f };

	// Rebuilds m_UboViewProjection.projection from the lens settings and the current swapchain extent
	void updateProjectionMatrixFromCameraLens() noexcept;

	glm::vec3 m_ClearColour{ 0.6f, 0.65f, 0.4f };

	// GPU memory: every buffer and image comes from here (VMA)
	Rendering::GpuMemoryAllocator m_GpuMemory;
	Rendering::ResourceUploader m_ResourceUploader;

	// Diagnostics
	Rendering::FrameStatistics m_FrameStatistics;
	VkPhysicalDeviceProperties m_PhysicalDeviceProperties{};
	bool m_CanWriteTimestamps{ false };
	std::chrono::steady_clock::time_point m_PreviousSubmitFrameStartTime{};
	bool m_WasPreviousFrameAScreenshotFrame{ false };
	void readFinishedTimestampQueries(Rendering::FrameResources& FinishedFrame);
	void recordFrameStatistic(float& SmoothedValue, float NewValue) const noexcept;

	// Frames in flight: everything the CPU rewrites every frame, one copy per frame the GPU may still be working on
	std::array<Rendering::FrameResources, Utilities::Vulkan::c_MaxFrameDraws> m_Frames{};
	void createFrameResources();
	void destroyFrameResources() noexcept;
	// One per swapchain image: presenting waits on it, and the image index (not the frame) says when it's free again
	std::vector<VkSemaphore> m_RenderFinishedSemaphores;
	void createRenderFinishedSemaphores();
	void destroyRenderFinishedSemaphores() noexcept;

	// Screenshots
	std::optional<std::filesystem::path> m_PendingScreenshotFilePath;
	bool m_ShouldCaptureScreenshotThisFrame{ false };
	Rendering::AllocatedBuffer m_ScreenshotBuffer;
	void prepareScreenshotBufferIfRequested();
	void recordScreenshotCopy(VkCommandBuffer CommandBuffer, VkImage SwapchainImage);
	void writeCapturedScreenshotToFile();

	// Dear ImGui
	bool m_IsImGuiInitialised{ false };
	bool m_HasImGuiFrameBegun{ false };		// beginImGuiFrame was called and SubmitFrame hasn't consumed it yet
	bool m_ShouldRenderImGuiThisFrame{ false };
	std::string m_ImGuiIniFilePath;			// Must outlive ImGui, which only keeps the pointer
	void shutdownImGui() noexcept;

	// Infinite grid: its own pipeline (no vertex buffer, alpha blended, no depth writes), sharing m_PipelineLayout
	VkPipeline m_InfiniteGridPipeline{ VK_NULL_HANDLE };
	bool m_IsInfiniteGridVisible{ false };
	void createInfiniteGridPipeline();

	// Unified Geometry Buffers (Replaces individual Mesh buffers). Their capacity is their m_SizeInBytes.
	Rendering::AllocatedBuffer m_GlobalVertexBuffer;
	uint32_t m_CurrentVertexOffset{ 0 };

	Rendering::AllocatedBuffer m_GlobalIndexBuffer;
	uint32_t m_CurrentIndexOffset{ 0 };

	// Replaces the buffer with a bigger one (at least RequiredCapacityInBytes, usually double) and copies the used part over on the GPU.
	// Waits for the device to go idle, so it's meant for load time, not the middle of gameplay.
	void growUnifiedBuffer(Rendering::AllocatedBuffer& BufferToGrow, VkDeviceSize UsedBytesToKeep, VkDeviceSize RequiredCapacityInBytes,
		VkBufferUsageFlags BufferUsage, std::string_view BufferNameForLogging);

	// New internal setup functions
	void createUnifiedBuffers();

	// GLFW Window
	VAtomic<GLFWwindow> m_Window;

	int m_CurrentFrame{ 0 };

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

	Rendering::AllocatedImage m_DepthBuffer;
	VkFormat m_DepthBufferFormat{ VK_FORMAT_UNDEFINED };

	VkSampler m_SmoothTextureSampler{ VK_NULL_HANDLE };		// Trilinear + anisotropic (TextureFiltering::Smooth)
	VkSampler m_PixelatedTextureSampler{ VK_NULL_HANDLE };	// Nearest (TextureFiltering::Pixelated)

	// - Descriptors
	VkDescriptorSetLayout m_DescriptorSetLayout;
	VkDescriptorSetLayout m_BindlessTextureSetLayout;	// Set 1: every texture, one array
	//VkPushConstantRange m_PushConstantRange;

	VkDescriptorPool m_DescriptorPool;			// Set 0 of every frame in flight
	VkDescriptorPool m_BindlessTexturePool;
	VkDescriptorSet m_BindlessTextureSet{ VK_NULL_HANDLE };
	uint32_t m_BindlessTextureCapacity{ 0 };	// Array size (GPU limits permitting)
	uint32_t m_TextureCount{ 0 };				// Slots written so far: a texture identifier is its slot

	// - Assets
	std::vector<Rendering::AllocatedImage> m_TextureImages;

	// - Pipeline
	VkPipeline m_GraphicsPipeline;
	VkPipelineLayout m_PipelineLayout;


	// - Utility
	VkFormat m_SwapchainImageFormat;		// sRGB when available: the scene is drawn in this
	VkFormat m_SwapchainOverlayFormat;		// Its UNORM twin when the swapchain allows a second view format, otherwise the same
	bool m_IsSwapchainMutableFormatSupported{ false };
	VkExtent2D m_SwapchainExtent;

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
	void chooseDepthBufferFormat();
	[[nodiscard]] VkImageAspectFlags getDepthBufferAspectMask() const noexcept;
	void createDescriptorSetLayout();
	void createGraphicsPipeline();
	void createDepthBufferImage();
	void createTextureSamplers();

	void createDescriptorPool();

	// - Resizable window related
	void cleanupSwapchain();	// Swapchain, its image views, depth buffer: everything sized like the window
	void recreateSwapchain();	// Waits for the GPU, then rebuilds what cleanupSwapchain destroyed (blocks while minimised)
	// The window's framebuffer no longer matches the swapchain (resized), checked every frame without any GLFW callback
	[[nodiscard]] bool doesSwapchainNeedRecreation() const;

	// - Record functions
	void recordCommands(Rendering::FrameResources& FrameToRecord, uint32_t SwapchainImageIndex, const std::vector<RenderInstance>& instances);

	// - Get functions
	void getPhysicalDevice();

	// - Support functions
	// -- Checker functions
	bool checkInstanceExtensionSupport(const std::vector<const char*>* checkExtensions);
	bool checkDeviceExtensionSupport(VkPhysicalDevice device);
	bool checkValidationLayerSupport();
	bool checkDeviceSuitable(VkPhysicalDevice device);
	// Higher is better: discrete GPUs first, then the rest. Only called for devices that passed checkDeviceSuitable.
	[[nodiscard]] static uint64_t scorePhysicalDevice(VkPhysicalDevice device);


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
	[[nodiscard]] static VkFormat getUnormFormatOfSrgbFormat(VkFormat SrgbFormat) noexcept;
	[[nodiscard]] bool canGenerateMipmapsForFormat(VkFormat TextureFormat) const;
	VkPresentModeKHR chooseBestPresentationMode(const std::vector<VkPresentModeKHR>& presentationModes);
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities);
	VkFormat chooseSupportedFormat(const std::vector<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags featureFlags) const;

	// -- Create functions
	VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const;
	VkShaderModule createShaderModule(const std::vector<uint32_t>& code) const;

	public:
	int createTextureImage(std::string_view fileName, const Rendering::TextureOptions& Options);
	// Loads an image file (relative names come from assets/Textures). Returns the texture identifier.
	int createTexture(std::string_view fileName, const Rendering::TextureOptions& Options = {});
	int createTextureDescriptor(VkImageView textureImage, VkSampler TextureSampler);

	// - Textures that don't come from a file in assets/Textures (model importers need these)
	// Raw 8-bit RGBA pixels, WidthInPixels * HeightInPixels * 4 bytes. Returns the texture identifier.
	int createTextureFromRgbaPixels(const unsigned char* RgbaPixels, uint32_t WidthInPixels, uint32_t HeightInPixels, const Rendering::TextureOptions& Options = {});
	// A whole PNG/JPG/... file that's already in memory (e.g. embedded in a .glb). Falls back to white if it can't be decoded.
	int createTextureFromEncodedImageInMemory(const unsigned char* EncodedImageBytes, size_t EncodedImageByteCount, const Rendering::TextureOptions& Options = {});
	// 1x1 white texture, for materials with no texture (the shader multiplies it by the vertex colour). Created on first use.
	[[nodiscard]] uint32_t getDefaultWhiteTextureIdentifier();
	private:
	int createTextureImageFromRgbaPixels(const unsigned char* RgbaPixels, uint32_t WidthInPixels, uint32_t HeightInPixels, const Rendering::TextureOptions& Options);
	int createTextureViewAndDescriptorForImage(int TextureImageIndex, const Rendering::TextureOptions& Options);
	std::optional<uint32_t> m_DefaultWhiteTextureIdentifier;
	// -- Loader Functions
	stbi_uc* loadTextureFile(std::string_view fileName, int* width, int* height, VkDeviceSize* imageSize);
};
}

#endif // !VULKANRENDERER_H
