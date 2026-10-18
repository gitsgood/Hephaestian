#include "Vulkanised/VulkanRenderer.h"

using namespace Vulkanised;

Vulkanised::VulkanRenderer::VulkanRenderer()
{
}

int Vulkanised::VulkanRenderer::init(GLFWwindow* newWindow)
{
	if (newWindow == nullptr) 
	{
		LOGE("GLFW window is null\n");
		return EXIT_FAILURE;
	}

	// Wrap it in a shared_ptr with a custom deleter
	// We pass glfwDestroyWindow as the function to call when the ref count hits 0
	m_Window = std::shared_ptr<GLFWwindow>(newWindow, glfwDestroyWindow);

	try
	{
		createInstance();
		setupDebugMessenger();
		createSurface();
		getPhysicalDevice();
		createLogicalDevice();
		createSwapchain();
		createRenderPass();
		createDescriptorSetLayout();
		createPushConstantRange();
		createGraphicsPipeline();
		createDepthBufferImage();
		createFramebuffers();
		createCommandPool();
		createCommandBuffers();
		createTextureSampler();
		//allocateDynamicBufferTransferSpace();
		createUniformBuffers();
		createDescriptorPool();
		createDescriptorSets();
		//recordCommands();
		createSynchronisation();

		m_UboViewProjection.projection = glm::perspective(glm::radians(45.0f), (float)m_SwapchainExtent.width / (float)m_SwapchainExtent.height, 0.1f, 100.0f);
		m_UboViewProjection.view = glm::lookAt(glm::vec3(2.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		m_UboViewProjection.projection[1][1] *= -1;

		// Create a mesh
		// Vertex data
		std::vector<Vertex> meshVertices{
			{ { -0.4, 0.4, 0.0 },{ 1.0f, 0.0f, 0.0f },{ 1.0f, 1.0f } },		// 0
			{ { -0.4, -0.4, 0.0 },{ 1.0f, 0.0f, 0.0f },{ 1.0f, 0.0f } },	// 1
			{ { 0.4, -0.4, 0.0 },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 0.0f } },		// 2
			{ { 0.4, 0.4, 0.0 },{ 1.0f, 0.0f, 0.0f },{ 0.0f, 1.0f } },		// 3
		};

		std::vector<Vertex> meshVertices2{
			{ { -0.25, 0.6, 0.0 },{ 0.0f, 0.0f, 1.0f },{ 1.0f, 1.0f } },	// 0
			{ { -0.25, -0.6, 0.0 },{ 0.0f, 0.0f, 1.0f },{ 1.0f, 0.0f } },	// 1
			{ { 0.25, -0.6, 0.0 },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 0.0f } },    // 2
			{ { 0.25, 0.6, 0.0 },{ 0.0f, 0.0f, 1.0f },{ 0.0f, 1.0f } },		// 3
		};

		// Index data
		std::vector<uint32_t> meshIndices{ {0, 1, 2, 2, 3, 0} };
		m_MeshList.push_back(make_unique<Mesh>(m_MainDevice.physicalDevice, m_MainDevice.logicalDevice, m_GraphicsQueue, m_GraphicsCommandPool, &meshVertices, &meshIndices, createTexture("bibi.jpg")));
		m_MeshList.push_back(make_unique<Mesh>(m_MainDevice.physicalDevice, m_MainDevice.logicalDevice, m_GraphicsQueue, m_GraphicsCommandPool, &meshVertices2, &meshIndices, createTexture("trump.jpg")));
	}
	catch (const std::runtime_error &e)
	{
		LOGE(e.what());
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

void Vulkanised::VulkanRenderer::draw()
{
	// -- GET NEXT IMAGE --
	// Wait for given fence to signal (open) from last draw before continuing
	vkWaitForFences(m_MainDevice.logicalDevice, 1, &m_DrawFences[m_CurrentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max());
	// Manually reset (close) fences
	vkResetFences(m_MainDevice.logicalDevice, 1, &m_DrawFences[m_CurrentFrame]);

	// Get index of next image to be drawn to, and signal semaphore when ready to be drawn to
	uint32_t imageIndex{ 0 };
	VkResult result = vkAcquireNextImageKHR(m_MainDevice.logicalDevice, m_Swapchain, std::numeric_limits<uint64_t>::max(), m_ImageAvailable[m_CurrentFrame], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		recreateSwapchain();
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		constexpr const char* message{ "failed to acquire next image" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	recordCommands(imageIndex);
	updateUniformBuffers(imageIndex);

	// -- SUBMIT COMMAND BUFFER TO RENDER --
	// Queue submission information
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;										// Number of semaphores to wait on
	submitInfo.pWaitSemaphores = &m_ImageAvailable[m_CurrentFrame];			// List of semaphores to wait on
	VkPipelineStageFlags waitStages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
	};
	submitInfo.pWaitDstStageMask = waitStages;							// Stages to check semaphores at
	submitInfo.commandBufferCount = 1;									// Number of command buffers to submit
	submitInfo.pCommandBuffers = &m_CommandBuffers[imageIndex];			// Command buffer to submit
	submitInfo.signalSemaphoreCount = 1;								// Number of semaphores to signal
	submitInfo.pSignalSemaphores = &m_RenderFinished[imageIndex];	// Semaphores to signal when command buffer finishes

	// Submit command buffer to queue
	if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_DrawFences[m_CurrentFrame]) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to submit command buffer to queue" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// -- PRESENT RENDERED IMAGE TO SCREEN --
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;									// Number of semaphores to wait on
	presentInfo.pWaitSemaphores = &m_RenderFinished[imageIndex];	// Semaphores to wait on
	presentInfo.swapchainCount = 1;										// Number of swapchains to present to
	presentInfo.pSwapchains = &m_Swapchain;								// Swapchains to present images to
	presentInfo.pImageIndices = &imageIndex;							// Index of images in swapchains to present

	// Present image
	VkResult presentResult = vkQueuePresentKHR(m_PresentationQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		// The swapchain became out of date since we acquired the image
		recreateSwapchain();
		return; // Stop this frame and start over with the new swapchain
	}
	else if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
	{
		constexpr const char* message{ "failed to present image to presentation queue" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Get next frame (use % Utilities::Vulkan::c_MaxFrameDraws to keep value below Utilities::Vulkan::c_MaxFrameDraws)
	m_CurrentFrame = (m_CurrentFrame + 1) % Utilities::Vulkan::c_MaxFrameDraws;
}

void Vulkanised::VulkanRenderer::cleanup() noexcept
{
	// Wait until no actions being run on device before destroying
	vkDeviceWaitIdle(m_MainDevice.logicalDevice);

	//_aligned_free(m_ModelTransferSpace);

	if (m_SamplerDescriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(m_MainDevice.logicalDevice, m_SamplerDescriptorPool, nullptr);
		m_SamplerDescriptorPool = VK_NULL_HANDLE;
	}
	if (m_SamplerSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(m_MainDevice.logicalDevice, m_SamplerSetLayout, nullptr);
		m_SamplerSetLayout = VK_NULL_HANDLE;
	}
	if (m_TextureSampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(m_MainDevice.logicalDevice, m_TextureSampler, nullptr);
		m_TextureSampler = VK_NULL_HANDLE;
	}

	for (size_t i = 0; i < m_TextureImages.size(); i++)
	{
		if (m_TextureImageViews[i] != VK_NULL_HANDLE)
		{
			vkDestroyImageView(m_MainDevice.logicalDevice, m_TextureImageViews[i], nullptr);
			m_TextureImageViews[i] = VK_NULL_HANDLE;
		}
		if (m_TextureImages[i] != VK_NULL_HANDLE)
		{
			vkDestroyImage(m_MainDevice.logicalDevice, m_TextureImages[i], nullptr);
			m_TextureImages[i] = VK_NULL_HANDLE;
		}
		if (m_TextureImageMemory[i] != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_MainDevice.logicalDevice, m_TextureImageMemory[i], nullptr);
			m_TextureImageMemory[i] = VK_NULL_HANDLE;
		}
	}

	// Destroy the meshes
	if (!m_MeshList.empty()) 
	{
		for (auto& mesh : m_MeshList)
		{
			mesh.reset();
		}
	}

	if (m_DescriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(m_MainDevice.logicalDevice, m_DescriptorPool, nullptr);
		m_DescriptorPool = VK_NULL_HANDLE;
	}
	if (m_DescriptorSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(m_MainDevice.logicalDevice, m_DescriptorSetLayout, nullptr);
		m_DescriptorSetLayout = VK_NULL_HANDLE;
	}

	cleanupSwapchain();

	// Destroy semaphores and fences
	for (size_t i{ 0 }; i < Utilities::Vulkan::c_MaxFrameDraws; i++)
	{
		if (m_ImageAvailable[i] != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(m_MainDevice.logicalDevice, m_ImageAvailable[i], nullptr);
			m_ImageAvailable[i] = VK_NULL_HANDLE;
		}
		if (m_DrawFences[i] != VK_NULL_HANDLE)
		{
			vkDestroyFence(m_MainDevice.logicalDevice, m_DrawFences[i], nullptr);
			m_DrawFences[i] = VK_NULL_HANDLE;
		}
	}
	for (auto renderFinishedSemaphore : m_RenderFinished)
	{
		if (renderFinishedSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(m_MainDevice.logicalDevice, renderFinishedSemaphore, nullptr);
			renderFinishedSemaphore = VK_NULL_HANDLE;
		}
	}

	if (m_GraphicsCommandPool != VK_NULL_HANDLE)
	{
		vkDestroyCommandPool(m_MainDevice.logicalDevice, m_GraphicsCommandPool, nullptr);
		m_GraphicsCommandPool = VK_NULL_HANDLE;
	}


	if (m_GraphicsPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(m_MainDevice.logicalDevice, m_GraphicsPipeline, nullptr);
		m_GraphicsPipeline = VK_NULL_HANDLE;
	}
	if (m_PipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(m_MainDevice.logicalDevice, m_PipelineLayout, nullptr);
		m_PipelineLayout = VK_NULL_HANDLE;
	}
	if (m_RenderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(m_MainDevice.logicalDevice, m_RenderPass, nullptr);
		m_RenderPass = VK_NULL_HANDLE;
	}
	if (m_MainDevice.logicalDevice != VK_NULL_HANDLE)
	{
		vkDestroyDevice(m_MainDevice.logicalDevice, nullptr);
		m_MainDevice.logicalDevice = VK_NULL_HANDLE;
	}
	// Debug messenger, surface and instance
	if (Utilities::Vulkan::enableValidationLayers && m_DebugMessenger)
	{
		Utilities::Vulkan::DestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
	}
	if (m_Surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
		m_Surface = VK_NULL_HANDLE;
	}
	if (m_Instance != VK_NULL_HANDLE)
	{
		vkDestroyInstance(m_Instance, nullptr);
		m_Instance = VK_NULL_HANDLE;
	}

	// Log current validation "score"
	logTrackers();

	// Destroy GLFW window and terminate GLFW
	//glfwDestroyWindow(m_Window.load().get());	// Normally this should be done automatically by the shared pointer...

	glfwTerminate();
}

Vulkanised::VulkanRenderer::~VulkanRenderer()
{
}

void Vulkanised::VulkanRenderer::logTrackers() const noexcept
{
	if (!Utilities::Vulkan::enableValidationLayers) { return; }

	LOGH("\nValidation score (the closer to 0, the better):\n\t-Performance: {}\n\t-Validation: {}\n\t-Errors: {}\n\t-Warnings: {}", m_PerformanceMsgCount, m_ValidationMsgCount, m_ErrorMsgCount, m_WarningMsgCount);
}

void Vulkanised::VulkanRenderer::createInstance()
{
#ifdef _WIN32
	if (Utilities::Vulkan::enableValidationLayers && !checkValidationLayerSupport())
	{
		constexpr const char* message{ "validation layers requested, but not available!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

#elif defined(__APPLE__)    //Validation layers on Mac sort of kinda working, but also not?
	if (Utilities::Vulkan::enableValidationLayers && !checkValidationLayerSupport())
	{
		Utilities::Vulkan::enableValidationLayers = false;
		LOGE("This Mac has no support for Validation layers.");
	}
	//LOG("Mac has no support for Validation layers atm.");
	//LOG("Or DOES it??");
#endif

	// Application information
	// Most data here doesn't affect the program, it's for dev convenience.
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulkanised";						// Custom application name
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);			// Custom application version
	appInfo.pEngineName = "No Engine... yet";						// Custom engine name
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);				// Custom engine version
	appInfo.apiVersion = VK_API_VERSION_1_4;						// Vulkan API version

	// Creation information for a Vulkan instance
	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	// Create list to hold instance extensions
	std::vector<const char*> instanceExtensions = std::vector<const char*>();

	// Set up extensions Instance will use
	uint32_t glfwExtensionCount{ 0 };								// GLFW may require multiple extensions
	const char** glfwExtensions;									// Extensions passed as an array of cstrings, so we need pointer (the array) to pointer (the cstring)

	// Get GLFW extensions
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	// Add GLFW extensions to our list
	instanceExtensions.reserve(glfwExtensionCount);
	for (size_t i = 0; i < glfwExtensionCount; i++)
	{
		instanceExtensions.push_back(glfwExtensions[i]);
	}

	// Add debug extension if validation layers are enabled
	if (Utilities::Vulkan::enableValidationLayers)
	{
		instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	// Check if instance extensions are supported...
	if(!checkInstanceExtensionSupport(&instanceExtensions))
	{
		constexpr const char* message{ "VkInstance does not support required extensions!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
	createInfo.ppEnabledExtensionNames = instanceExtensions.data();

	// Set up validation layers if enabled
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	if (Utilities::Vulkan::enableValidationLayers)
	{
		createInfo.enabledLayerCount = static_cast<uint32_t>(Utilities::Vulkan::validationLayers.size());
		createInfo.ppEnabledLayerNames = Utilities::Vulkan::validationLayers.data();

		populateDebugMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
	}
	else {
		createInfo.enabledLayerCount = 0;
		createInfo.ppEnabledLayerNames = nullptr;
		createInfo.pNext = nullptr;
	}

	// Create instance
	if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS)
	{
		constexpr const char* message{ "Failed to create a Vulkan Instance!" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::createLogicalDevice()
{
	// Get the queue family indices for the chosen Physical device
	Utilities::Vulkan::QueueFamilyIndices indices = getQueueFamilies(m_MainDevice.physicalDevice);

	// Vector for queue creation information, and set for family indices
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<int> queueFamilyIndices{ indices.graphicsFamily, indices.presentationFamily };

	// Queues the logical device needs to create and info to do so
	for (int queueFamilyIndex : queueFamilyIndices)
	{
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamilyIndex;					// Index of the family to create a queue from
		queueCreateInfo.queueCount = 1;											// Number of queues to create
		float priority{ 1.f };
		queueCreateInfo.pQueuePriorities = &priority;							// Vulkan needs to know how to handle multiple queues, so decide priority (1 = highest priority)

		queueCreateInfos.push_back(queueCreateInfo);
	}

	// Information to create logical device (sometimes called "device")
	VkDeviceCreateInfo deviceCreateInfo{};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());							// Number of Queue Create infos
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();													// List of queue create infos so device can create required queues
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(Utilities::Vulkan::deviceExtensions.size());				// Number of enabled logical device extensions
	deviceCreateInfo.ppEnabledExtensionNames = Utilities::Vulkan::deviceExtensions.data();									// List of enabled logical device extensions

	// Physical Device Features the logical device will be using
	VkPhysicalDeviceFeatures deviceFeatures{};
	deviceFeatures.samplerAnisotropy = VK_TRUE;		// Enable Anisotropy

	deviceCreateInfo.pEnabledFeatures = &deviceFeatures;				// Physical Device features Logical Device will use

	// Create the logical device for the given physical device
	if (vkCreateDevice(m_MainDevice.physicalDevice, &deviceCreateInfo, nullptr, &m_MainDevice.logicalDevice) != VK_SUCCESS)
	{
		constexpr const char* message{ "Failed to create a Logical Device!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Queues are created at the same time as the device...
	// So we want handle to queues
	// For given Logical Device, of given Queue Family, of given Queue Index (0 since only one queue), place reference in given VkQueue
	vkGetDeviceQueue(m_MainDevice.logicalDevice, indices.graphicsFamily, 0, &m_GraphicsQueue);
	vkGetDeviceQueue(m_MainDevice.logicalDevice, indices.presentationFamily, 0, &m_PresentationQueue);
}

void Vulkanised::VulkanRenderer::setupDebugMessenger()
{
	if (!Utilities::Vulkan::enableValidationLayers)
		return;

	VkDebugUtilsMessengerCreateInfoEXT createInfo;
	populateDebugMessengerCreateInfo(createInfo);

	if (Utilities::Vulkan::CreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to set up debug messenger!" };
		LOGE(message);
		throw std::runtime_error(message);
	}
	else
	{
		LOG("setupDebugMessenger successful");
	}
}

void Vulkanised::VulkanRenderer::createSurface()
{
	// Create Surface (creates a surface create info struct, runs the create surface function, returns VkResult)
	if (glfwCreateWindowSurface(m_Instance, m_Window.load().get(), nullptr, &m_Surface) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create a surface" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::createSwapchain()
{
	// Get swapchain details so we can pick best settings
	Utilities::Vulkan::SwapchainDetails swapchainDetails = getSwapchainDetails(m_MainDevice.physicalDevice);

	// 1. CHOOSE BEST SURFACE FORMAT
	VkSurfaceFormatKHR surfaceFormat = chooseBestSurfaceFormat(swapchainDetails.formats);
	// 2. CHOOSE BEST PRESENTATION MODE
	VkPresentModeKHR presentMode = chooseBestPresentationMode(swapchainDetails.presentationModes);
	// 3. CHOOSE SWAP CHAIN IMAGE RESOLUTION
	VkExtent2D extent = chooseSwapExtent(swapchainDetails.surfaceCapabilities);

	// How many images are in the swapchain? Get 1 more than the minimum to allow triple buffering
	uint32_t imageCount = swapchainDetails.surfaceCapabilities.minImageCount + 1;
	
	// If imageCount higher than max, then clamp down to max
	// If 0, then limitless
	if (swapchainDetails.surfaceCapabilities.maxImageCount > 0 && swapchainDetails.surfaceCapabilities.maxImageCount < imageCount)
	{
		imageCount = swapchainDetails.surfaceCapabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapchainCreateInfo{};
	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.surface = m_Surface;													// Swapchain surface
	swapchainCreateInfo.imageFormat = surfaceFormat.format;										// Swapchain format
	swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;								// Swapchain colour space
	swapchainCreateInfo.presentMode = presentMode;												// Swapchain presentation mode
	swapchainCreateInfo.imageExtent = extent;													// Swapchain image extents
	swapchainCreateInfo.minImageCount = imageCount;												// Minimum images in the swapchain
	swapchainCreateInfo.imageArrayLayers = 1;													// Number of layers for each image in chain
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;						// What attachement images will be used as
	swapchainCreateInfo.preTransform = swapchainDetails.surfaceCapabilities.currentTransform;	// Transform to perform on swapchain images
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;						// How to handle blending images with external graphics (e.g. other windows)
	swapchainCreateInfo.clipped = VK_TRUE;														// Whether to clip parts of image not in view (e.g. behind another window, off screen, etc.)
	
	// Get queue family indices
	Utilities::Vulkan::QueueFamilyIndices indices = getQueueFamilies(m_MainDevice.physicalDevice);

	// If graphics and presentation families are different, then swapchain must let images be shared between families
	if (indices.graphicsFamily != indices.presentationFamily)
	{
		// Queues to share between
		uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphicsFamily, (uint32_t)indices.presentationFamily };

		swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;		// Image share handling
		swapchainCreateInfo.queueFamilyIndexCount = 2;							// Number of queues to share images between
		swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;			// Array of queues to share between
	}
	else
	{
		swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		swapchainCreateInfo.queueFamilyIndexCount = 0;
		swapchainCreateInfo.pQueueFamilyIndices = nullptr;
	}

	// If old swapchain being destroyed and this one replaces it, then link old one to quickly hand over responsibilities
	swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

	// Create actual swapchain
	if (vkCreateSwapchainKHR(m_MainDevice.logicalDevice, &swapchainCreateInfo, nullptr, &m_Swapchain) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create a swapchain" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Store for later reference
	m_SwapchainImageFormat = surfaceFormat.format;
	m_SwapchainExtent = extent;

	// Get swapchain images (first count, then values)
	uint32_t swapchainImageCount;
	vkGetSwapchainImagesKHR(m_MainDevice.logicalDevice, m_Swapchain, &swapchainImageCount, nullptr);
	std::vector<VkImage> images(swapchainImageCount);
	vkGetSwapchainImagesKHR(m_MainDevice.logicalDevice, m_Swapchain, &swapchainImageCount, images.data());

	for (VkImage image : images)
	{
		// Store image handle
		Utilities::Vulkan::SwapchainImage swapchainImage{};
		swapchainImage.image = image;
		swapchainImage.imageView = createImageView(image, m_SwapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);

		// Add to swapchain image list
		m_SwapchainImages.push_back(swapchainImage);
	}
}

void Vulkanised::VulkanRenderer::createRenderPass()
{
	// ATTACHMENTS
	// Colour attachement of render pass
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = m_SwapchainImageFormat;					// Format to use for attachment
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;					// Number of samples to write for multisampling
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;				// Describes what to do with attachment before rendering
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;				// Describes what to do with attachment after rendering
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	// Describes what to do with stencil before rendering
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;	// Describes what to do with stencil after rendering

	// Framebuffer data will be stored as an image, but images can be given different data layouts
	// to give optimal use for certain operations

	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;			// Image data layout before render pass starts
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;		// Image data layout after render pass (to change to)

	// Depth attachment of render pass
	VkAttachmentDescription depthAttachment = {};
	depthAttachment.format = chooseSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// REFERENCES
	// Attachment reference uses an attachment index that refers to index in the attachment list passed to renderPassCreateInfo
	VkAttachmentReference colourAttachmentReference{};
	colourAttachmentReference.attachment = 0;
	colourAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// Depth Attachment Reference
	VkAttachmentReference depthAttachmentReference = {};
	depthAttachmentReference.attachment = 1;
	depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Information about a particular subpass the render pass is using
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;		// Pipeline type subpass is to be bound to
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colourAttachmentReference;
	subpass.pDepthStencilAttachment = &depthAttachmentReference;

	// Need to determine when layout transitions occur using subpass dependencies
	std::array<VkSubpassDependency, 2> subpassDependencies{};

	// Conversion from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	// Transition must happen after...
	subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;						// Subpass index (VK_SUBPASS_EXTERNAL = Special value meaning outside of renderpass)
	subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;		// Pipeline stage
	subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;				// Stage access mask (memory access)
	// But must happen before...
	subpassDependencies[0].dstSubpass = 0;
	subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependencies[0].dependencyFlags = 0;

	// Conversion from VK_IMAGE_LAYOUT_OPTIMAL to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	// Transition must happen after...
	subpassDependencies[1].srcSubpass = 0;
	subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	// But must happen before...
	subpassDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	subpassDependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	subpassDependencies[1].dependencyFlags = 0;

	std::array<VkAttachmentDescription, 2> renderPassAttachments = { colorAttachment, depthAttachment };

	// Create info for render pass
	VkRenderPassCreateInfo renderPassCreateInfo{};
	renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(renderPassAttachments.size());
	renderPassCreateInfo.pAttachments = renderPassAttachments.data();
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = &subpass;
	renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
	renderPassCreateInfo.pDependencies = subpassDependencies.data();

	if (vkCreateRenderPass(m_MainDevice.logicalDevice, &renderPassCreateInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create render pass!" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::createDescriptorSetLayout()
{
	// UNIFORM VALUES DESCRIPTOR SET LAYOUT
	// UboViewProjection Binding Info
	VkDescriptorSetLayoutBinding vpLayoutBinding = {};
	vpLayoutBinding.binding = 0;											// Binding point in shader (designated by binding number in shader)
	vpLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;		// Type of descriptor (uniform, dynamic uniform, image sampler, etc)
	vpLayoutBinding.descriptorCount = 1;									// Number of descriptors for binding
	vpLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;				// Shader stage to bind to
	vpLayoutBinding.pImmutableSamplers = nullptr;							// For Texture: Can make sampler data unchangeable (immutable) by specifying in layout

	// Model Binding Info
	//VkDescriptorSetLayoutBinding modelLayoutBinding = {};
	//modelLayoutBinding.binding = 1;
	//modelLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	//modelLayoutBinding.descriptorCount = 1;
	//modelLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	//modelLayoutBinding.pImmutableSamplers = nullptr;

	std::vector<VkDescriptorSetLayoutBinding> layoutBindings = { vpLayoutBinding };

	// Create Descriptor Set Layout with given bindings
	VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());	// Number of binding infos
	layoutCreateInfo.pBindings = layoutBindings.data();								// Array of binding infos

	// Create Descriptor Set Layout 
	if (vkCreateDescriptorSetLayout(m_MainDevice.logicalDevice, &layoutCreateInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create descriptor set layout!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// CREATE TEXTURE SAMPLER DESCRIPTOR SET LAYOUT
	// Texture binding info
	VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
	samplerLayoutBinding.binding = 0;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	samplerLayoutBinding.pImmutableSamplers = nullptr;

	// Create a Descriptor Set Layout with given bindings for texture
	VkDescriptorSetLayoutCreateInfo textureLayoutCreateInfo = {};
	textureLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	textureLayoutCreateInfo.bindingCount = 1;
	textureLayoutCreateInfo.pBindings = &samplerLayoutBinding;

	// Create Descriptor Set Layout
	if (vkCreateDescriptorSetLayout(m_MainDevice.logicalDevice, &textureLayoutCreateInfo, nullptr, &m_SamplerSetLayout) != VK_SUCCESS)
	{
		constexpr const char* message{ "Failed to create a Descriptor Set Layout!" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::createPushConstantRange()
{
	// Define push constant values (no 'create' needed!)
	m_PushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;	// Shader stage push constant will go to
	m_PushConstantRange.offset = 0;									// Offset into given data to pass to push constant
	m_PushConstantRange.size = sizeof(Model);						// Size of data being passed
}

void Vulkanised::VulkanRenderer::createGraphicsPipeline()
{
	// Read in SPIR-V code of shaders
	auto vertexShaderCode = Utilities::File::readFile("src/shaders/vert.spv");
	auto fragmentShaderCode = Utilities::File::readFile("src/shaders/frag.spv");

	// Create Shader Modules to link to Graphics Pipeline
	VkShaderModule vertexShaderModule = createShaderModule(vertexShaderCode);
	VkShaderModule fragmentShaderModule = createShaderModule(fragmentShaderCode);

	// -- SHADER STAGE CREATION INFORMATION --
	// Vertex stage creation information
	VkPipelineShaderStageCreateInfo vertexShaderCreateInfo{};
	vertexShaderCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertexShaderCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;								// Shader stage name
	vertexShaderCreateInfo.module = vertexShaderModule;										// Shader module to be used by stage
	vertexShaderCreateInfo.pName = "main";													// What name does the main function have in the vertex shader

	// Fragment stage creation information
	VkPipelineShaderStageCreateInfo fragmentShaderCreateInfo{};
	fragmentShaderCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragmentShaderCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;							// Shader stage name
	fragmentShaderCreateInfo.module = fragmentShaderModule;									// Shader module to be used by stage
	fragmentShaderCreateInfo.pName = "main";													// What name does the main function have in the vertex shader

	// Put shader stage creation infos into array
	// Graphics pipeline creation info requires array of shader stage creates
	VkPipelineShaderStageCreateInfo shaderStages[] = { vertexShaderCreateInfo, fragmentShaderCreateInfo };

	// How the data for a single vertex (including info such as position, colour, texture coordinates, normals, etc...) is as a whole
	VkVertexInputBindingDescription bindingDescription{ Vertex::getBindingDescription() };

	// How the data for an attribute is defined within a vertex
	std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{ Vertex::getAttributeDescriptions() };

	// -- VERTEX INPUT --
	VkPipelineVertexInputStateCreateInfo vertexInputCreateInfo{};
	vertexInputCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputCreateInfo.vertexBindingDescriptionCount = 1;
	vertexInputCreateInfo.pVertexBindingDescriptions = &bindingDescription;											// List of vertex binding descriptions (data spacing/stride information)
	vertexInputCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputCreateInfo.pVertexAttributeDescriptions = attributeDescriptions.data();								// List of attribute descriptions (data format and where to bind to/from)

	// -- INPUT ASSEMBLY --
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo{};
	inputAssemblyCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;		// Primitive type to assemble vertices as
	inputAssemblyCreateInfo.primitiveRestartEnable = VK_FALSE;					// Allow overriding of strip topology to start new primitives

	// -- VIEWPORT & SCISSOR --
	// Create a viewport info struct
	VkViewport viewport{};
	viewport.x = 0.0f;										// x start coordinate
	viewport.y = 0.0f;										// y start coordinate
	viewport.width = (float)m_SwapchainExtent.width;		// width of viewport
	viewport.height = (float)m_SwapchainExtent.height;		// height of viewport
	viewport.minDepth = 0.0f;								// min framebuffer depth
	viewport.maxDepth = 1.0f;								// max framebuffer depth

	//// Create a scissor info struct
	//VkRect2D scissor{};
	//scissor.offset = { 0,0 };				// Offset to use region from
	//scissor.extent = m_SwapchainExtent;		// Extent to describe region to use, starting at offset

	//VkPipelineViewportStateCreateInfo viewportStateCreateInfo{};
	//viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	//viewportStateCreateInfo.viewportCount = 1;
	//viewportStateCreateInfo.pViewports = &viewport;
	//viewportStateCreateInfo.scissorCount = 1;
	//viewportStateCreateInfo.pScissors = &scissor;

	// -- DYNAMIC STATES --
	// Dynamic states to enable
	std::vector<VkDynamicState> dynamicStateEnables = {
		VK_DYNAMIC_STATE_VIEWPORT,		// Dynamic viewport: can resize in command buffer with vkCmdSetViewport(commandbuffer, 0, 1, &viewport);
		VK_DYNAMIC_STATE_SCISSOR		// Dynamic scissor : can resize in command buffer with vkCmdSetScissor(commandbuffer, 0, 1, &scissor);
	};

	// Dynamic state creation info
	VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{};
	dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());
	dynamicStateCreateInfo.pDynamicStates = dynamicStateEnables.data();

	// Because we enabled dynamic states, we don't need to pass the actual 
	// viewport and scissor structs here. We only need to tell Vulkan how MANY there are.
	VkPipelineViewportStateCreateInfo viewportStateCreateInfo{};
	viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCreateInfo.viewportCount = 1;
	viewportStateCreateInfo.pViewports = nullptr; // <--- Now null, we set this in recordCommands
	viewportStateCreateInfo.scissorCount = 1;
	viewportStateCreateInfo.pScissors = nullptr;  // <--- Now null, we set this in recordCommands

	// -- RASTERIZER --
	VkPipelineRasterizationStateCreateInfo rasterizerCreateInfo{};
	rasterizerCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizerCreateInfo.depthClampEnable = VK_FALSE;			// Change if fragments beyond near/far planes are clipped (default) or clamped to plane
	rasterizerCreateInfo.rasterizerDiscardEnable = VK_FALSE;	// Whether to discard data and skip rasterizer. Never creates fragments, only suitable for pipeline without framebuffer output
	rasterizerCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;	// How to handle filling points between vertices
	rasterizerCreateInfo.lineWidth = 1.0f;						// How thick lines should be when drawn
	rasterizerCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;		// Which face of a tri to cull
	rasterizerCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;	// Winding to determine which side is front
	rasterizerCreateInfo.depthBiasEnable = VK_FALSE;			// Whether to add depth bias to fragments (good for stopping "shadow acne" in shadow mapping)

	// -- MULTISAMPLING --
	VkPipelineMultisampleStateCreateInfo multisamplingCreateInfo{};
	multisamplingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisamplingCreateInfo.sampleShadingEnable = VK_FALSE;					// Enable multisampling shading or not
	multisamplingCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;	// Number of samples to use per fragment

	// -- BLENDING --
	// Blending decides how to blend a new colour being written to a fragment, with the old value

	// Blend attachment state (how blending is handled)
	VkPipelineColorBlendAttachmentState colourState{};
	// Colours to apply bending to
	colourState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colourState.blendEnable = VK_TRUE;		// Enable blending

	// Blending uses equation : (srcColorBlendFactor * new colour) colorBlendOp (dstColorBlendFactor * old colour)
	colourState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colourState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colourState.colorBlendOp = VK_BLEND_OP_ADD;

	// Summarised : (VK_BLEND_FACTOR_SRC_ALPHA * new colour) + (VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA * old colour)
	//				(new colour alpha * new colour) + ((1 - new colour alpha) * old colour)

	colourState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colourState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colourState.alphaBlendOp = VK_BLEND_OP_ADD;

	// Summarised : (1 * new alpha) + (0 * old alpha) = new alpha


	VkPipelineColorBlendStateCreateInfo colourBlendingCreateInfo{};
	colourBlendingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colourBlendingCreateInfo.logicOpEnable = VK_FALSE;				// Alternative to calculations is to use logical operations
	//colourBlendingCreateInfo.logicOp = VK_LOGIC_OP_COPY;
	colourBlendingCreateInfo.attachmentCount = 1;
	colourBlendingCreateInfo.pAttachments = &colourState;

	// -- PIPELINE LAYOUT --
	std::array<VkDescriptorSetLayout, 2> descriptorSetLayouts = { m_DescriptorSetLayout, m_SamplerSetLayout };

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &m_PushConstantRange;

	// Create pipeline layout
	if (vkCreatePipelineLayout(m_MainDevice.logicalDevice, &pipelineLayoutCreateInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create pipeline layout!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// -- DEPTH STENCIL TESTING --
	VkPipelineDepthStencilStateCreateInfo depthStencilCreateInfo = {};
	depthStencilCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilCreateInfo.depthTestEnable = VK_TRUE;				// Enable checking depth to determine fragment write
	depthStencilCreateInfo.depthWriteEnable = VK_TRUE;				// Enable writing to depth buffer (to replace old values)
	depthStencilCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS;		// Comparison operation that allows an overwrite (is in front)
	depthStencilCreateInfo.depthBoundsTestEnable = VK_FALSE;		// Depth Bounds Test: Does the depth value exist between two bounds
	depthStencilCreateInfo.stencilTestEnable = VK_FALSE;			// Enable Stencil Test

	// -- GRAPHICS PIPELINE CREATION --
	VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo{};
	graphicsPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	graphicsPipelineCreateInfo.stageCount = 2;									// Number of shader stages
	graphicsPipelineCreateInfo.pStages = shaderStages;							// List of shader stages
	graphicsPipelineCreateInfo.pVertexInputState = &vertexInputCreateInfo;		// All the fixed function pipeline states
	graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyCreateInfo;
	graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
	graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
	graphicsPipelineCreateInfo.pRasterizationState = &rasterizerCreateInfo;
	graphicsPipelineCreateInfo.pMultisampleState = &multisamplingCreateInfo;
	graphicsPipelineCreateInfo.pColorBlendState = &colourBlendingCreateInfo;
	graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilCreateInfo;
	graphicsPipelineCreateInfo.layout = m_PipelineLayout;						// Pipeline layout pipeline should use
	graphicsPipelineCreateInfo.renderPass = m_RenderPass;						// Render pass description the pipeline is compatible with
	graphicsPipelineCreateInfo.subpass = 0;										// Subpass of render pass to use with pipeline

	// Pipeline derivatives : Can create multiple pipelines that derive from one another for optimisation
	graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;		// existing pipeline to derive from...
	graphicsPipelineCreateInfo.basePipelineIndex = -1;					// or index of pipeline being created to derive from (in case creating multiple at once)

	// Create graphics pipeline
	if (vkCreateGraphicsPipelines(m_MainDevice.logicalDevice, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create pipeline(s)!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Destroy shader modules, no longer needed after pipeline created
	vkDestroyShaderModule(m_MainDevice.logicalDevice, fragmentShaderModule, nullptr);
	vkDestroyShaderModule(m_MainDevice.logicalDevice, vertexShaderModule, nullptr);
}

void Vulkanised::VulkanRenderer::createDepthBufferImage()
{
	// Get supported format for depth buffer
	VkFormat depthFormat = chooseSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

	// Create Depth Buffer Image
	m_DepthBufferImage = createImage(m_SwapchainExtent.width, m_SwapchainExtent.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m_DepthBufferImageMemory);

	// Create Depth Buffer Image View
	m_DepthBufferImageView = createImageView(m_DepthBufferImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Vulkanised::VulkanRenderer::createFramebuffers()
{
	// Resize framebuffer count to equal swap chain image count
	m_SwapchainFramebuffers.resize(m_SwapchainImages.size());

	// Create a framebuffer for each swapchain image
	for (size_t i = 0; i < m_SwapchainFramebuffers.size(); i++)
	{
		std::array<VkImageView, 2> attachment = {
			m_SwapchainImages[i].imageView,
			m_DepthBufferImageView
		};

		VkFramebufferCreateInfo framebufferCreateInfo{};
		framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferCreateInfo.renderPass = m_RenderPass;									// Render pass layout the framebuffer will be used with
		framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachment.size());
		framebufferCreateInfo.pAttachments = attachment.data();								// List of attachments (1:1 with render pass)
		framebufferCreateInfo.width = m_SwapchainExtent.width;								// Framebuffer width
		framebufferCreateInfo.height = m_SwapchainExtent.height;							// Framebuffer height
		framebufferCreateInfo.layers = 1;													// Framebuffer layers

		if (vkCreateFramebuffer(m_MainDevice.logicalDevice, &framebufferCreateInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS)
		{
			constexpr const char* message{ "failed to create a swapchain framebuffer" };
			LOGE(message);
			throw std::runtime_error(message);
		}
	}
}

void Vulkanised::VulkanRenderer::createCommandPool()
{
	// Get indices of queue families from device
	Utilities::Vulkan::QueueFamilyIndices queueFamilyIndices = getQueueFamilies(m_MainDevice.physicalDevice);

	VkCommandPoolCreateInfo poolCreateInfo{};
	poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolCreateInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;	// Queue family type that buffers from this command pool will use

	// Create a graphics queue family command pool
	if (vkCreateCommandPool(m_MainDevice.logicalDevice, &poolCreateInfo, nullptr, &m_GraphicsCommandPool) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create command pool" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::createCommandBuffers()
{
	// Resize command buffer count to have 1 for each framebuffer
	m_CommandBuffers.resize(m_SwapchainFramebuffers.size());

	VkCommandBufferAllocateInfo commandBufferAllocInfo{};
	commandBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocInfo.commandPool = m_GraphicsCommandPool;
	commandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;		// VK_COMMAND_BUFFER_LEVEL_PRIMARY : Buffer you submit directly to queue. Can't be called directly by other buffers.
																		// VK_COMMAND_BUFFER_LEVEL_SECONDARY : Buffer can't be called directly. Can be called from other buffers via "vkCmdExecuteCommands" when recording commands in primary buffer
	commandBufferAllocInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

	// Allocate command buffers and place handles in array of buffers
	if (vkAllocateCommandBuffers(m_MainDevice.logicalDevice, &commandBufferAllocInfo, m_CommandBuffers.data()) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to allocate command buffers" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::createTextureSampler()
{
	// Sampler Creation Info
	VkSamplerCreateInfo samplerCreateInfo = {};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;						// How to render when image is magnified on screen
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;						// How to render when image is minified on screen
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;	// How to handle texture wrap in U (x) direction
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;	// How to handle texture wrap in V (y) direction
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;	// How to handle texture wrap in W (z) direction
	samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;	// Border beyond texture (only workds for border clamp)
	samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;				// Whether coords should be normalized (between 0 and 1)
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;		// Mipmap interpolation mode
	samplerCreateInfo.mipLodBias = 0.0f;								// Level of Details bias for mip level
	samplerCreateInfo.minLod = 0.0f;									// Minimum Level of Detail to pick mip level
	samplerCreateInfo.maxLod = 0.0f;									// Maximum Level of Detail to pick mip level
	samplerCreateInfo.anisotropyEnable = VK_TRUE;						// Enable Anisotropy
	samplerCreateInfo.maxAnisotropy = 16;								// Anisotropy sample level

	if (vkCreateSampler(m_MainDevice.logicalDevice, &samplerCreateInfo, nullptr, &m_TextureSampler) != VK_SUCCESS)
	{
		constexpr const char* message{ "Filed to create a Texture Sampler!" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::createSynchronisation()
{
	m_ImageAvailable.resize(Utilities::Vulkan::c_MaxFrameDraws);
	//m_RenderFinished.resize(Utilities::Vulkan::c_MaxFrameDraws);
	m_DrawFences.resize(Utilities::Vulkan::c_MaxFrameDraws);

	// Get the actual number of images in the swapchain
	uint32_t imageCount;
	vkGetSwapchainImagesKHR(m_MainDevice.logicalDevice, m_Swapchain, &imageCount, nullptr);

	m_RenderFinished.resize(imageCount);

	// Semaphore creation information
	VkSemaphoreCreateInfo semaphoreCreateInfo{};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	// Fence creation information
	VkFenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < Utilities::Vulkan::c_MaxFrameDraws; i++)
	{
		if (vkCreateSemaphore(m_MainDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &m_ImageAvailable[i]) != VK_SUCCESS ||
			//vkCreateSemaphore(m_MainDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &m_RenderFinished[i]) != VK_SUCCESS ||
			vkCreateFence(m_MainDevice.logicalDevice, &fenceCreateInfo, nullptr, &m_DrawFences[i]) != VK_SUCCESS)
		{
			constexpr const char* message{ "failed to create image available semaphore(s) and/or draw fence(s)" };
			LOGE(message);
			throw std::runtime_error(message);
		}
	}

	// The render finished semaphores need to amount up to the image count in the swapchain, so it has its own loop
	for (size_t i = 0; i < imageCount; i++)
	{
		if (vkCreateSemaphore(m_MainDevice.logicalDevice, &semaphoreCreateInfo, nullptr, &m_RenderFinished[i]) != VK_SUCCESS)
		{
			constexpr const char* message{ "failed to create render finished semaphore(s)" };
			LOGE(message);
			throw std::runtime_error(message);
		}
	}
}

void Vulkanised::VulkanRenderer::createUniformBuffers()
{
	// ViewProjection buffer size
	VkDeviceSize vpBufferSize = sizeof(UboViewProjection);

	// Model buffer size
	//VkDeviceSize modelBufferSize = m_ModelUniformAlignment * Utilities::Vulkan::c_MaxObjects;

	// One uniform buffer for each image (and by extension, command buffer)
	m_VpUniformBuffer.resize(m_SwapchainImages.size());
	m_VpUniformBufferMemory.resize(m_SwapchainImages.size());
	//m_ModelDUniformBuffer.resize(m_SwapchainImages.size());
	//m_ModelDUniformBufferMemory.resize(m_SwapchainImages.size());

	// Create the uniform buffers
	for (size_t i{ 0 }; i < m_SwapchainImages.size(); i++)
	{
		Utilities::Vulkan::createBuffer(
			m_MainDevice.physicalDevice,
			m_MainDevice.logicalDevice,
			vpBufferSize,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&m_VpUniformBuffer[i],
			&m_VpUniformBufferMemory[i]
		);

		//Utilities::Vulkan::createBuffer(
		//	m_MainDevice.physicalDevice,
		//	m_MainDevice.logicalDevice,
		//	modelBufferSize,
		//	VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		//	VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		//	&m_ModelDUniformBuffer[i],
		//	&m_ModelDUniformBufferMemory[i]
		//);
	}
}

void Vulkanised::VulkanRenderer::createDescriptorPool()
{
	// CREATE UNIFORM DESCRIPTOR POOL
	// Type of descriptors + how many DESCRIPTORS, not descriptor sets (combined makes the pool size)
	// ViewProjection Pool
	VkDescriptorPoolSize vpPoolSize = {};
	vpPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	vpPoolSize.descriptorCount = static_cast<uint32_t>(m_VpUniformBuffer.size());

	// Model Pool (DYNAMIC)
	//VkDescriptorPoolSize modelPoolSize = {};
	//modelPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	//modelPoolSize.descriptorCount = static_cast<uint32_t>(m_ModelDUniformBuffer.size());

	// List of pool sizes
	std::vector<VkDescriptorPoolSize> descriptorPoolSizes = { vpPoolSize };

	// Data to create descriptor pool
	VkDescriptorPoolCreateInfo poolCreateInfo{};
	poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolCreateInfo.maxSets = static_cast<uint32_t>(m_SwapchainImages.size());					// Maximum number of Descriptor Sets that can be created from pool
	poolCreateInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());			// Amount of Pool Sizes being passed
	poolCreateInfo.pPoolSizes = descriptorPoolSizes.data();										// Pool Sizes to create pool with

	// Create descriptor pool
	if (vkCreateDescriptorPool(m_MainDevice.logicalDevice, &poolCreateInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create the uniform descriptor pool" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// CREATE SAMPLER DESCRIPTOR POOL
	// Texture sampler pool
	VkDescriptorPoolSize samplerPoolSize = {};
	samplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerPoolSize.descriptorCount = Utilities::Vulkan::c_MaxObjects;

	VkDescriptorPoolCreateInfo samplerPoolCreateInfo = {};
	samplerPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	samplerPoolCreateInfo.maxSets = Utilities::Vulkan::c_MaxObjects;
	samplerPoolCreateInfo.poolSizeCount = 1;
	samplerPoolCreateInfo.pPoolSizes = &samplerPoolSize;

	if (vkCreateDescriptorPool(m_MainDevice.logicalDevice, &samplerPoolCreateInfo, nullptr, &m_SamplerDescriptorPool) != VK_SUCCESS)
	{
		constexpr const char* message{ "Failed to create a the sampler descriptor Pool!" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::createDescriptorSets()
{
	// Resize descriptor set list such that one for every buffer
	m_DescriptorSets.resize(m_SwapchainImages.size());

	std::vector<VkDescriptorSetLayout> setLayouts(m_SwapchainImages.size(), m_DescriptorSetLayout);

	// Descriptor set allocation info
	VkDescriptorSetAllocateInfo setAllocInfo{};
	setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	setAllocInfo.descriptorPool = m_DescriptorPool;											// Pool to allocate descriptor set from
	setAllocInfo.descriptorSetCount = static_cast<uint32_t>(m_SwapchainImages.size());		// Number of sets to allocate
	setAllocInfo.pSetLayouts = setLayouts.data();											// Layouts to use to allocate sets (1:1 relationship)

	// Allocate descriptor sets (multiple)
	if (vkAllocateDescriptorSets(m_MainDevice.logicalDevice, &setAllocInfo, m_DescriptorSets.data()) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to allocate descriptor sets" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Update all of descriptor set buffer bindings
	for (size_t i{ 0 }; i < m_SwapchainImages.size(); ++i)
	{
		// Buffer info and data offset info
		VkDescriptorBufferInfo vpBufferInfo{};
		vpBufferInfo.buffer = m_VpUniformBuffer[i];			// Buffer to get data from
		vpBufferInfo.offset = 0;							// Position of start of data
		vpBufferInfo.range = sizeof(UboViewProjection);		// Size of data

		// Data about connection between binding and buffer
		VkWriteDescriptorSet vpSetWrite = {};
		vpSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		vpSetWrite.dstSet = m_DescriptorSets[i];								// Descriptor Set to update
		vpSetWrite.dstBinding = 0;											// Binding to update (matches with binding on layout/shader)
		vpSetWrite.dstArrayElement = 0;									// Index in array to update
		vpSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;		// Type of descriptor
		vpSetWrite.descriptorCount = 1;									// Amount to update
		vpSetWrite.pBufferInfo = &vpBufferInfo;							// Information about buffer data to bind

		// MODEL DESCRIPTOR
		// Model Buffer Binding Info
		//VkDescriptorBufferInfo modelBufferInfo = {};
		//modelBufferInfo.buffer = m_ModelDUniformBuffer[i];
		//modelBufferInfo.offset = 0;
		//modelBufferInfo.range = m_ModelUniformAlignment;

		//VkWriteDescriptorSet modelSetWrite = {};
		//modelSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		//modelSetWrite.dstSet = m_DescriptorSets[i];
		//modelSetWrite.dstBinding = 1;
		//modelSetWrite.dstArrayElement = 0;
		//modelSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		//modelSetWrite.descriptorCount = 1;
		//modelSetWrite.pBufferInfo = &modelBufferInfo;

		// List of Descriptor Set Writes
		std::vector<VkWriteDescriptorSet> setWrites = { vpSetWrite };

		// Update the descriptor sets with new buffer/binding info
		vkUpdateDescriptorSets(m_MainDevice.logicalDevice, static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
	}
}

void Vulkanised::VulkanRenderer::updateUniformBuffers(uint32_t imageIndex)
{
	void* data;
	vkMapMemory(m_MainDevice.logicalDevice, m_VpUniformBufferMemory[imageIndex], 0, sizeof(UboViewProjection), 0, &data);
	memcpy(data, &m_UboViewProjection, sizeof(UboViewProjection));
	vkUnmapMemory(m_MainDevice.logicalDevice, m_VpUniformBufferMemory[imageIndex]);

	// Copy Model data
	//for (size_t i = 0; i < m_MeshList.size(); i++)
	//{
	//	UboModel* thisModel = (UboModel*)((uint64_t)m_ModelTransferSpace + (i * m_ModelUniformAlignment));
	//	*thisModel = m_MeshList[i]->getModel();
	//}

	//// Map the list of model data
	//vkMapMemory(m_MainDevice.logicalDevice, m_ModelDUniformBufferMemory[imageIndex], 0, m_ModelUniformAlignment * m_MeshList.size(), 0, &data);
	//memcpy(data, m_ModelTransferSpace, m_ModelUniformAlignment * m_MeshList.size());
	//vkUnmapMemory(m_MainDevice.logicalDevice, m_ModelDUniformBufferMemory[imageIndex]);
}

void Vulkanised::VulkanRenderer::cleanupSwapchain()
{
	// 1. Destroy Framebuffers
	for (auto framebuffer : m_SwapchainFramebuffers)
	{
		if (framebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(m_MainDevice.logicalDevice, framebuffer, nullptr);
			framebuffer = VK_NULL_HANDLE;
		}
	}
	m_SwapchainFramebuffers.clear();

	// 2. Destroy Uniform Buffers (since they are per-swapchain-image)
	// Uniform buffer and uniform buffer memory are always the same size, since they're created at the same time, in the same loop. This might not look safe in isolation, but it should be.
	for (size_t i{ 0 }; i < m_SwapchainImages.size(); i++)
	{
		if (m_VpUniformBuffer[i] != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_MainDevice.logicalDevice, m_VpUniformBuffer[i], nullptr);
			m_VpUniformBuffer[i] = VK_NULL_HANDLE;
		}
		if (m_VpUniformBufferMemory[i] != VK_NULL_HANDLE)
		{
			vkFreeMemory(m_MainDevice.logicalDevice, m_VpUniformBufferMemory[i], nullptr);
			m_VpUniformBufferMemory[i] = VK_NULL_HANDLE;
		}

		//if (m_ModelDUniformBuffer[i] != VK_NULL_HANDLE)
		//{
		//	vkDestroyBuffer(m_MainDevice.logicalDevice, m_ModelDUniformBuffer[i], nullptr);
		//	m_ModelDUniformBuffer[i] = VK_NULL_HANDLE;
		//}
		//if (m_ModelDUniformBufferMemory[i] != VK_NULL_HANDLE)
		//{
		//	vkFreeMemory(m_MainDevice.logicalDevice, m_ModelDUniformBufferMemory[i], nullptr);
		//	m_ModelDUniformBufferMemory[i] = VK_NULL_HANDLE;
		//}
	}
	m_VpUniformBuffer.clear();
	m_VpUniformBufferMemory.clear();

	// 3. Destroy Swapchain Image Views
	for (auto& image : m_SwapchainImages)
	{
		if (image.imageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(m_MainDevice.logicalDevice, image.imageView, nullptr);
			image.imageView = VK_NULL_HANDLE;
		}
	}
	m_SwapchainImages.clear();

	// 4. Destroy the Swapchain
	if (m_Swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(m_MainDevice.logicalDevice, m_Swapchain, nullptr);
		m_Swapchain = VK_NULL_HANDLE;
	}

	// 5. Destroy Depth Buffer (Must match swapchain size)
	if (m_DepthBufferImageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(m_MainDevice.logicalDevice, m_DepthBufferImageView, nullptr);
		m_DepthBufferImageView = VK_NULL_HANDLE;
	}
	if (m_DepthBufferImage != VK_NULL_HANDLE)
	{
		vkDestroyImage(m_MainDevice.logicalDevice, m_DepthBufferImage, nullptr);
		m_DepthBufferImage = VK_NULL_HANDLE;
	}
	if (m_DepthBufferImageMemory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_MainDevice.logicalDevice, m_DepthBufferImageMemory, nullptr);
		m_DepthBufferImageMemory = VK_NULL_HANDLE;
	}

	if (m_GraphicsCommandPool != VK_NULL_HANDLE) {
		vkDestroyCommandPool(m_MainDevice.logicalDevice, m_GraphicsCommandPool, nullptr);
		m_GraphicsCommandPool = VK_NULL_HANDLE;
	}
}

void Vulkanised::VulkanRenderer::recreateSwapchain()
{
	// 1. Handle minimization
	int width = 0, height = 0;
	glfwGetFramebufferSize(m_Window.load().get(), &width, &height);
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(m_Window.load().get(), &width, &height);
		glfwWaitEvents();
	}

	// 2. Wait for GPU to finish current work
	vkDeviceWaitIdle(m_MainDevice.logicalDevice);

	// 3. Wipe the resolution-dependent objects
	cleanupSwapchain();

	// 4. REBUILD EVERYTHING in the correct order
	createSwapchain();           // Updates m_SwapchainExtent and m_SwapchainImages
	createDepthBufferImage();    // Re-creates depth buffer for new size
	createFramebuffers();        // Re-fills m_SwapchainFramebuffers
	createUniformBuffers();      // Re-fills m_VpUniformBuffer and m_VpUniformBufferMemory
	createCommandPool();         // Re-creates the pool (or just reset it)
	createCommandBuffers();      // Re-fills m_CommandBuffers to match new image count

	vkResetDescriptorPool(m_MainDevice.logicalDevice, m_DescriptorPool, 0);
	createDescriptorSets();
}

void Vulkanised::VulkanRenderer::recordCommands(uint32_t currentImage)
{
	// Information about how to begin each command buffer
	VkCommandBufferBeginInfo commandBufferBeginInfo{};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	//commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;	// Buffer can be resubmitted when it has already been submitted and is awaiting execution

	// Information about how to begin a render pass (only needed for graphical applications)
	VkRenderPassBeginInfo renderPassBeginInfo{};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = m_RenderPass;							// Render pass to begin
	renderPassBeginInfo.renderArea.offset = { 0, 0 };						// Start point of render pass in pixels
	renderPassBeginInfo.renderArea.extent = m_SwapchainExtent;				// Size of region to run render pass on, starting at offset

	std::array<VkClearValue, 2> clearValues = {};
	clearValues[0].color = { 0.6f, 0.65f, 0.4f, 1.0f };
	clearValues[1].depthStencil.depth = 1.0f;

	renderPassBeginInfo.pClearValues = clearValues.data();					// List of clear values
	renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());

	renderPassBeginInfo.framebuffer = m_SwapchainFramebuffers[currentImage];

	// Start recording commands to command buffer!
	if (vkBeginCommandBuffer(m_CommandBuffers[currentImage], &commandBufferBeginInfo) != VK_SUCCESS)
	{
		constexpr const char* message{ "Failed to start recording a Command Buffer!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Begin Render Pass
	vkCmdBeginRenderPass(m_CommandBuffers[currentImage], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	// 1. DEFINE THE CURRENT VIEWPORT
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)m_SwapchainExtent.width;
	viewport.height = (float)m_SwapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	// 2. DEFINE THE CURRENT SCISSOR
	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = m_SwapchainExtent;

	// 3. SET THE DYNAMIC STATES
	vkCmdSetViewport(m_CommandBuffers[currentImage], 0, 1, &viewport);
	vkCmdSetScissor(m_CommandBuffers[currentImage], 0, 1, &scissor);

	// Bind Pipeline to be used in render pass
	vkCmdBindPipeline(m_CommandBuffers[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

	for (size_t j{ 0 }; j < m_MeshList.size(); j++)
	{
		VkBuffer vertexBuffers[] = { m_MeshList[j]->getVertexBuffer() };					// Buffers to bind
		VkDeviceSize offsets[] = { 0 };												// Offsets into buffers being bound
		vkCmdBindVertexBuffers(m_CommandBuffers[currentImage], 0, 1, vertexBuffers, offsets);	// Command to bind vertex buffer before drawing with them

		// Bind mesh index buffer, with 0 offset and using the uint32 type
		vkCmdBindIndexBuffer(m_CommandBuffers[currentImage], m_MeshList[j]->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

		// Dynamic Offset Amount
		// uint32_t dynamicOffset = static_cast<uint32_t>(modelUniformAlignment) * j;

		// "Push" constants to given shader stage directly (no buffer)
		vkCmdPushConstants(
			m_CommandBuffers[currentImage],
			m_PipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT,			// Stage to push constants to
			0,									// Offset of push constants to update
			sizeof(Model),						// Size of data being pushed
			&m_MeshList[j]->getModel()			// Actual data being pushed (can be array)
		);

		std::array<VkDescriptorSet, 2> descriptorSetGroup = { m_DescriptorSets[currentImage], m_SamplerDescriptorSets[m_MeshList[j]->getTexId()] };

		// Bind Descriptor Sets
		vkCmdBindDescriptorSets(m_CommandBuffers[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, static_cast<uint32_t>(descriptorSetGroup.size()), descriptorSetGroup.data(), 0, nullptr);

		// Execute pipeline
		vkCmdDrawIndexed(m_CommandBuffers[currentImage], static_cast<uint32_t>(m_MeshList[j]->getIndexCount()), 1, 0, 0, 0);
	}

	// End Render Pass
	vkCmdEndRenderPass(m_CommandBuffers[currentImage]);

	// Stop recording to command buffer
	if (vkEndCommandBuffer(m_CommandBuffers[currentImage]) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to stop recording a command buffer" };
		LOGE(message);
		throw std::runtime_error(message);
	}
}

void Vulkanised::VulkanRenderer::getPhysicalDevice()
{
	// Enumerate physical devices the VkInstance can access
	uint32_t deviceCount{ 0 };
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

	// If no devices available, then none support Vulkan!
	if (deviceCount == 0)
	{
		constexpr const char* message{ "Can't find GPU's that support Vulkan instance!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Get list of physical devices
	std::vector<VkPhysicalDevice> deviceList(deviceCount);
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, deviceList.data());

	for (const auto& device : deviceList)
	{
		if (checkDeviceSuitable(device))
		{
			m_MainDevice.physicalDevice = device;
			break;
		}
	}

	// Get properties of our new device
	VkPhysicalDeviceProperties deviceProperties;
	vkGetPhysicalDeviceProperties(m_MainDevice.physicalDevice, &deviceProperties);

	//m_MinUniformBufferOffset = deviceProperties.limits.minUniformBufferOffsetAlignment;
}

void Vulkanised::VulkanRenderer::allocateDynamicBufferTransferSpace()
{
	//// Calculate alignment of model data
	//m_ModelUniformAlignment = (sizeof(UboModel) + m_MinUniformBufferOffset - 1) & ~(m_MinUniformBufferOffset - 1);

	//// Create space in memory to hold dynamic buffer that is aligned to our required alignment and holds MAX_OBJECTS
	//m_ModelTransferSpace = (UboModel*)_aligned_malloc(m_ModelUniformAlignment * Utilities::Vulkan::c_MaxObjects, m_ModelUniformAlignment);
}

bool Vulkanised::VulkanRenderer::checkInstanceExtensionSupport(const std::vector<const char*> *checkExtensions)
{
	// Need to get number of extensions to create array of correct size to hold extensions
	uint32_t extensionCount{ 0 };
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

	// Create a list of vkExtensionProperties using count
	std::vector<VkExtensionProperties> extensions(extensionCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

	// Check if given extensions are in list of available extensions
	for (const auto &checkExtension : *checkExtensions)
	{
		bool hasExtension{ false };
		for(const auto &extension : extensions)
		{
			if (strcmp(checkExtension, extension.extensionName))
			{
				hasExtension = true;
				break;
			}
		}

		if(!hasExtension)
		{
			return false;
		}
	}

	return true;
}

bool Vulkanised::VulkanRenderer::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
	uint32_t extensionCount{ 0 };
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

	// If no extensions found, return failure
	if (extensionCount == 0)
	{
		return false;
	}

	// Populate list of extensions
	std::vector<VkExtensionProperties> extensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

	// Check for extension
	for (const auto& deviceExtension : Utilities::Vulkan::deviceExtensions)
	{
		bool hasExtension{ false };
		for (const auto& extension : extensions)
		{
			if (strcmp(deviceExtension, extension.extensionName) == 0)
			{
				hasExtension = true;
				break;
			}
		}

		if (!hasExtension)
		{
			return false;
		}
	}
	return true;
}

bool Vulkanised::VulkanRenderer::checkDeviceSuitable(VkPhysicalDevice device)
{
	//// Information about the device itself (ID, name, type, vendor, etc)
	//VkPhysicalDeviceProperties deviceProperties;
	//vkGetPhysicalDeviceProperties(device, &deviceProperties);

	// Information about what the device can do (geo shader, tess shader, wide lines, etc)
	VkPhysicalDeviceFeatures deviceFeatures;
	vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

	Utilities::Vulkan::QueueFamilyIndices indices = getQueueFamilies(device);

	bool areExtensionsSupported = checkDeviceExtensionSupport(device);

	bool isSwapchainValid{ false };
	if (areExtensionsSupported)
	{
		Utilities::Vulkan::SwapchainDetails swapchainDetails = getSwapchainDetails(device);
		isSwapchainValid = !swapchainDetails.presentationModes.empty() && !swapchainDetails.formats.empty();
	}

	return indices.isValid() && areExtensionsSupported && isSwapchainValid && deviceFeatures.samplerAnisotropy;
}

bool Vulkanised::VulkanRenderer::checkValidationLayerSupport()
{
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char* layerName : Utilities::Vulkan::validationLayers)
	{
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers)
		{
			if (strcmp(layerName, layerProperties.layerName) == 0)
			{
				layerFound = true;
				break;
			}
		}

		if (!layerFound)
			return false;
	}

	return true;
}

Utilities::Vulkan::QueueFamilyIndices Vulkanised::VulkanRenderer::getQueueFamilies(VkPhysicalDevice device)
{
	Utilities::Vulkan::QueueFamilyIndices indices;

	// Get all queue family property info for the given device
	uint32_t queueFamilyCount{ 0 };
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilyList(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilyList.data());

	// Go through each queue family and check if it has at least 1 of the required types of queue
	int i{ 0 };
	for (const auto &queueFamily : queueFamilyList)
	{
		// First check if queue family has at least 1 queue in that family (could have no queues)
		// Queue can be multiples types defined through bitfield. Need to bitwise AND with VK_QUEUE_*_BIT to check if has required type
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			indices.graphicsFamily = i;		// If queue family is valid, then get index

		}

		// Check if queue family supports presentation
		VkBool32 presentationSupport{ false };
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentationSupport);
		// Check if queue is presentation type (can be both graphics AND presentation)
		if (queueFamily.queueCount > 0 && presentationSupport)
		{
			indices.presentationFamily = i;
		}

		// Check if queue family indices are in a valid state, stop searching if so
		if (indices.isValid())
		{
			break;
		}

		i++;
	}

	return indices;
}

void Vulkanised::VulkanRenderer::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
	createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = debugCallback;
	createInfo.pUserData = this;	// pUserData could be used for having a pointer to the renderer, we could perhaps track some things in members...
}

VKAPI_ATTR VkBool32 VKAPI_CALL Vulkanised::VulkanRenderer::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
	VulkanRenderer* renderer = static_cast<VulkanRenderer*>(pUserData);
	const char* severity{ " [UNDETERMINED] " };
	const char* type{ " [UNDETERMINED] " };

	switch (messageSeverity)
	{
	case VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		if (renderer) { renderer->m_ErrorMsgCount++; }
		//Error message
		severity = " [ERROR] ";
		break;
	case VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		if (renderer) { renderer->m_WarningMsgCount++; }
		//Warning message
		severity = " [WARNING] ";
		break;
	case VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
		//it verbose, ok
		severity = " [VERBOSE] ";
		break;
	case VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		//just info
		severity = " [INFO] ";
		break;
	case VkDebugUtilsMessageSeverityFlagBitsEXT::VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
		//whatever the fuck
		severity = " [WHATEVER THE FUCK] ";
		break;
	}

	if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
	{
		if (renderer) { renderer->m_PerformanceMsgCount++; }
		//mention its performance
		type = " [PERFORMANCE] ";
	}
	else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
	{
		if (renderer) { renderer->m_ValidationMsgCount++; }
		//mention what the validation is for
		type = " [VALIDATION] ";
	}
	else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
	{
		type = " [GENERAL] ";
	}

	LOGGER(Logger::LogType::VALIDATION, "\nValidation layer:\n\t-Severity: {}\n\t-Message type: {}\n\t-Message: \n{}", severity, type, pCallbackData->pMessage);
	//std::println(stderr, "\nValidation layer:\n\t-Severity: {}\n\t-Message type: {}\n\t-Message: \n{}", severity, type, pCallbackData->pMessage);

	return VK_FALSE;
}

Utilities::Vulkan::SwapchainDetails Vulkanised::VulkanRenderer::getSwapchainDetails(VkPhysicalDevice device)
{
	Utilities::Vulkan::SwapchainDetails swapchainDetails;

	// -- CAPABILITIES --
	// Get the surface capabilities for the given surface on the given physical device
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_Surface, &swapchainDetails.surfaceCapabilities);

	// -- FORMATS --
	uint32_t formatCount{ 0 };
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, nullptr);

	// If formats returned, get list of formats
	if (formatCount != 0)
	{
		swapchainDetails.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface, &formatCount, swapchainDetails.formats.data());
	}

	// -- PRESENTATION MODES --
	uint32_t presentationCount{ 0 };
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentationCount, nullptr);

	// If presentation modes returned, get list of presentation modes
	if (presentationCount != 0)
	{
		swapchainDetails.presentationModes.resize(presentationCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface, &presentationCount, swapchainDetails.presentationModes.data());
	}

	return swapchainDetails;
}

// Best format is subjective, but ours will be:
// Format		:	VK_FORMAT_R8G8B8A8_UNORM (VK_FORMAT_B8G8R8_UNORM as backup)
// colorSpace	:	VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
VkSurfaceFormatKHR Vulkanised::VulkanRenderer::chooseBestSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
	// If only 1 format available AND is undefined, then this means ALL formats are available (no restrictions)
	if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
	{
		return { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	}

	// If restricted, search for optimal format
	for (const auto& format : formats)
	{
		if ((format.format == VK_FORMAT_R8G8B8A8_UNORM || format.format == VK_FORMAT_B8G8R8_UNORM) && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			return format;
		}
	}

	// If can't find optimal format, then just return first format
	return formats[0];
}

VkPresentModeKHR Vulkanised::VulkanRenderer::chooseBestPresentationMode(const std::vector<VkPresentModeKHR>& presentationModes)
{
	// Look for Mailbox presentation mode
	for (const auto& presentationMode : presentationModes)
	{
		if (presentationMode == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			return presentationMode;
		}
	}

	// If can't find, use FIFO as Vulkan spec says it must be present
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Vulkanised::VulkanRenderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& surfaceCapabilities)
{
	// If current extent is at numeric limits, then extent can vary. Otherwise, it is the size of the window.
	if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
	{
		return surfaceCapabilities.currentExtent;
	}
	else
	{
		// If value can vary, need to set manually

		// Get window size
		int width{ 0 }, height{ 0 };
		glfwGetFramebufferSize(m_Window.load().get(), &width, &height);

		// Create new extent using window size
		VkExtent2D newExtent{};
		newExtent.height = static_cast<uint32_t>(height);
		newExtent.width = static_cast<uint32_t>(width);

		// Surface also defines max and min, so make sure within boundaries by clamping value
		newExtent.width = std::max(surfaceCapabilities.minImageExtent.width, std::min(surfaceCapabilities.maxImageExtent.width, newExtent.width));
		newExtent.height = std::max(surfaceCapabilities.minImageExtent.height, std::min(surfaceCapabilities.maxImageExtent.height, newExtent.height));

		return newExtent;
	}
}

VkFormat Vulkanised::VulkanRenderer::chooseSupportedFormat(const std::vector<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags featureFlags) const
{
	// Loop through options and find compatible one
	for (VkFormat format : formats)
	{
		// Get properties for give format on this device
		VkFormatProperties properties;
		vkGetPhysicalDeviceFormatProperties(m_MainDevice.physicalDevice, format, &properties);

		// Depending on tiling choice, need to check for different bit flag
		if (tiling == VK_IMAGE_TILING_LINEAR && (properties.linearTilingFeatures & featureFlags) == featureFlags)
		{
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & featureFlags) == featureFlags)
		{
			return format;
		}
	}

	constexpr const char* message{ "Failed to find a matching format!" };
	LOGE(message);
	throw std::runtime_error(message);
}

VkImage Vulkanised::VulkanRenderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags useFlags, VkMemoryPropertyFlags propFlags, VkDeviceMemory* imageMemory) const
{
	// CREATE IMAGE
	// Image Creation Info
	VkImageCreateInfo imageCreateInfo = {};
	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;						// Type of image (1D, 2D, or 3D)
	imageCreateInfo.extent.width = width;								// Width of image extent
	imageCreateInfo.extent.height = height;								// Height of image extent
	imageCreateInfo.extent.depth = 1;									// Depth of image (just 1, no 3D aspect)
	imageCreateInfo.mipLevels = 1;										// Number of mipmap levels
	imageCreateInfo.arrayLayers = 1;									// Number of levels in image array
	imageCreateInfo.format = format;									// Format type of image
	imageCreateInfo.tiling = tiling;									// How image data should be "tiled" (arranged for optimal reading)
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;			// Layout of image data on creation
	imageCreateInfo.usage = useFlags;									// Bit flags defining what image will be used for
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;					// Number of samples for multi-sampling
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;			// Whether image can be shared between queues

	// Create image
	VkImage image;
	if (vkCreateImage(m_MainDevice.logicalDevice, &imageCreateInfo, nullptr, &image) != VK_SUCCESS)
	{
		constexpr const char* message{ "Failed to create an Image!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// CREATE MEMORY FOR IMAGE

	// Get memory requirements for a type of image
	VkMemoryRequirements memoryRequirements;
	vkGetImageMemoryRequirements(m_MainDevice.logicalDevice, image, &memoryRequirements);

	// Allocate memory using image requirements and user defined properties
	VkMemoryAllocateInfo memoryAllocInfo = {};
	memoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocInfo.allocationSize = memoryRequirements.size;
	memoryAllocInfo.memoryTypeIndex = Utilities::Vulkan::findMemoryTypeIndex(m_MainDevice.physicalDevice, memoryRequirements.memoryTypeBits, propFlags);

	if (vkAllocateMemory(m_MainDevice.logicalDevice, &memoryAllocInfo, nullptr, imageMemory) != VK_SUCCESS)
	{
		constexpr const char* message{ "Failed to allocate memory for image!" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Connect memory to image
	vkBindImageMemory(m_MainDevice.logicalDevice, image, *imageMemory, 0);

	return image;
}

VkImageView Vulkanised::VulkanRenderer::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) const
{
	VkImageViewCreateInfo viewCreateInfo{};
	viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCreateInfo.image = image;										// Image to create view for
	viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;					// Type of image (1D, 2D, 3D, cube, etc)
	viewCreateInfo.format = format;										// Format of image data
	viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;		// Allows remapping of rbga components to other rgba values
	viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	// Subresources allow the view to view only a part of an image
	viewCreateInfo.subresourceRange.aspectMask = aspectFlags;			// Which aspect of image to view (e.g. COLOR_BIT for viewing color)
	viewCreateInfo.subresourceRange.baseMipLevel = 0;					// Start mipmap level to view from
	viewCreateInfo.subresourceRange.levelCount = 1;						// Number of mipmap levels to view
	viewCreateInfo.subresourceRange.baseArrayLayer = 0;					// Start array level to view from
	viewCreateInfo.subresourceRange.layerCount = 1;						// Number of array levels to view

	// Create image view and return it
	VkImageView imageView;
	if (vkCreateImageView(m_MainDevice.logicalDevice, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create an image view" };
		LOGE(message);
		throw std::runtime_error(message);
	}
	return imageView;
}

VkShaderModule Vulkanised::VulkanRenderer::createShaderModule(const std::vector<char>& code) const
{
	// Shader module creation information
	VkShaderModuleCreateInfo shaderModuleCreateInfo{};
	shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderModuleCreateInfo.codeSize = code.size();										// Size of code
	shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());		// Pointer to code (of uint32_t pointer type)
	
	VkShaderModule shaderModule;

	if (vkCreateShaderModule(m_MainDevice.logicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		constexpr const char* message{ "failed to create a shader module" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	return shaderModule;
}

int Vulkanised::VulkanRenderer::createTextureImage(std::string_view fileName)
{
	// Load image file
	int width, height;
	VkDeviceSize imageSize;
	stbi_uc* imageData = loadTextureFile(fileName, &width, &height, &imageSize);

	// Create staging buffer to hold loaded data, ready to copy to device
	VkBuffer imageStagingBuffer;
	VkDeviceMemory imageStagingBufferMemory;
	Utilities::Vulkan::createBuffer(
		m_MainDevice.physicalDevice, 
		m_MainDevice.logicalDevice, 
		imageSize, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&imageStagingBuffer, 
		&imageStagingBufferMemory
	);

	// Copy image data to staging buffer
	void* data;
	vkMapMemory(m_MainDevice.logicalDevice, imageStagingBufferMemory, 0, imageSize, 0, &data);
	memcpy(data, imageData, static_cast<size_t>(imageSize));
	vkUnmapMemory(m_MainDevice.logicalDevice, imageStagingBufferMemory);

	// Free original image data
	stbi_image_free(imageData);

	// Create image to hold final texture
	VkImage texImage;
	VkDeviceMemory texImageMemory;
	texImage = createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &texImageMemory);


	// COPY DATA TO IMAGE
	// Transition image to be DST for copy operation
	Utilities::Vulkan::transitionImageLayout(m_MainDevice.logicalDevice, m_GraphicsQueue, m_GraphicsCommandPool,
		texImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// Copy image data
	Utilities::Vulkan::copyImageBuffer(m_MainDevice.logicalDevice, m_GraphicsQueue, m_GraphicsCommandPool, imageStagingBuffer, texImage, width, height);

	// Transition image to be shader readable for shader usage
	Utilities::Vulkan::transitionImageLayout(m_MainDevice.logicalDevice, m_GraphicsQueue, m_GraphicsCommandPool,
		texImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Add texture data to vector for reference
	m_TextureImages.push_back(texImage);
	m_TextureImageMemory.push_back(texImageMemory);

	// Destroy staging buffers
	vkDestroyBuffer(m_MainDevice.logicalDevice, imageStagingBuffer, nullptr);
	vkFreeMemory(m_MainDevice.logicalDevice, imageStagingBufferMemory, nullptr);

	// Return index of new texture image
	return static_cast<int>(m_TextureImages.size() - 1);
}

int Vulkanised::VulkanRenderer::createTexture(std::string_view fileName)
{
	// Create Texture Image and get its location in array
	int textureImageLoc = createTextureImage(fileName);

	// Create Image View and add to list
	VkImageView imageView = createImageView(m_TextureImages[textureImageLoc], VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
	m_TextureImageViews.push_back(imageView);

	// Create Texture Descriptor
	int descriptorLoc = createTextureDescriptor(imageView);

	// Return location of set with texture
	return descriptorLoc;
}

int Vulkanised::VulkanRenderer::createTextureDescriptor(VkImageView textureImage)
{
	VkDescriptorSet descriptorSet;

	// Descriptor Set Allocation Info
	VkDescriptorSetAllocateInfo setAllocInfo = {};
	setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	setAllocInfo.descriptorPool = m_SamplerDescriptorPool;
	setAllocInfo.descriptorSetCount = 1;
	setAllocInfo.pSetLayouts = &m_SamplerSetLayout;

	// Allocate Descriptor Sets
	VkResult result = vkAllocateDescriptorSets(m_MainDevice.logicalDevice, &setAllocInfo, &descriptorSet);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to allocate Texture Descriptor Sets!");
	}

	// Texture Image Info
	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;	// Image layout when in use
	imageInfo.imageView = textureImage;									// Image to bind to set
	imageInfo.sampler = m_TextureSampler;									// Sampler to use for set

	// Descriptor Write Info
	VkWriteDescriptorSet descriptorWrite = {};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = descriptorSet;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	// Update new descriptor set
	vkUpdateDescriptorSets(m_MainDevice.logicalDevice, 1, &descriptorWrite, 0, nullptr);

	// Add descriptor set to list
	m_SamplerDescriptorSets.push_back(descriptorSet);

	// Return descriptor set location
	return static_cast<int>(m_SamplerDescriptorSets.size() - 1);
}

stbi_uc* Vulkanised::VulkanRenderer::loadTextureFile(std::string_view fileName, int* width, int* height, VkDeviceSize* imageSize)
{
	// Number of channels image uses
	int channels;

	// Load pixel data for image
	std::filesystem::path fileLoc{ std::filesystem::path(Utilities::File::c_Path) / "Textures" / fileName };
	std::string pathString = fileLoc.string();

	LOG("Loading texture: {}", pathString);

	stbi_uc* image = stbi_load(pathString.c_str(), width, height, &channels, STBI_rgb_alpha);

	if (!image)
	{
		std::string formated{ std::format("Failed to load texture file: {}", fileName) };
		LOGE(formated);
		throw std::runtime_error(formated);
	}

	// Calculate image size using given and known data
	if (imageSize)
	{
		*imageSize = static_cast<VkDeviceSize>((*width) * (*height) * 4);
	}

	return image;
}
