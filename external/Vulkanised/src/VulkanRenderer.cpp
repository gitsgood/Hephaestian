#include "Vulkanised/VulkanRenderer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

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

	// The renderer only BORROWS the window: whoever created it owns it (and already destroys it via their own shared_ptr).
	// Giving this shared_ptr glfwDestroyWindow as well meant two unrelated owners would both destroy the same window.
	m_Window = std::shared_ptr<GLFWwindow>(newWindow, [](GLFWwindow*) {});

	try
	{
		createInstance();
		setupDebugMessenger();
		createSurface();
		getPhysicalDevice();
		createLogicalDevice();
		// VMA can use whatever both the instance (1.4) and the device support, and the device is at least 1.3
		m_GpuMemory.initialise(m_Instance, m_MainDevice.physicalDevice, m_MainDevice.logicalDevice, std::min<uint32_t>(m_PhysicalDeviceProperties.apiVersion, VK_API_VERSION_1_4));
		createSwapchain();
		chooseDepthBufferFormat();	// Before the pipelines: they need to know the depth format
		createDescriptorSetLayout();
		createGraphicsPipeline();
		createInfiniteGridPipeline();
		createDepthBufferImage();
		// Uploads (geometry, textures) go to the GPU in batches through here
		m_ResourceUploader.initialise(m_MainDevice.logicalDevice, m_GraphicsQueue, static_cast<uint32_t>(getQueueFamilies(m_MainDevice.physicalDevice).graphicsFamily), m_GpuMemory);
		createTextureSamplers();
		createUnifiedBuffers();
		createDescriptorPool();
		createFrameResources();
		createRenderFinishedSemaphores();

		// UBO DEFAULTS
		// - View & Projection
		// Projection is now rebuilt every frame from the camera lens (see setCameraViewAndLens), this is just a sane default
		updateProjectionMatrixFromCameraLens();
		m_UboViewProjection.view = glm::lookAt(glm::vec3(2.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		// - LIGHTING DEFAULTS
		m_UboViewProjection.lightPos = glm::vec4(0.0f, 2.0f, 2.0f, 1.0f); // Light floating up and forward
		m_UboViewProjection.viewPos = glm::vec4(2.0f, 0.0f, 2.0f, 1.0f);  // Must match the lookAt camera pos!
		m_UboViewProjection.lightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // White light
	}
	catch (const std::runtime_error &e)
	{
		LOGE(e.what());
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

uint32_t Vulkanised::VulkanRenderer::loadMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, int texId)
{
	// --- ADD THIS SAFEGUARD ---
	if (vertices.empty() || indices.empty())
	{
		LOGE("Attempted to load a mesh with 0 vertices or 0 indices! Aborting load.");
		return 0; // Return a default/fallback mesh ID instead of crashing
	}

	VkDeviceSize vertexSize = sizeof(Vertex) * vertices.size();
	VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();

	// Bigger models eat through the unified buffers a lot faster: grow them instead of writing past their end
	const VkDeviceSize UsedVertexBufferBytes = static_cast<VkDeviceSize>(m_CurrentVertexOffset) * sizeof(Vertex);
	const VkDeviceSize UsedIndexBufferBytes = static_cast<VkDeviceSize>(m_CurrentIndexOffset) * sizeof(uint32_t);
	try
	{
		if (UsedVertexBufferBytes + vertexSize > m_GlobalVertexBuffer.m_SizeInBytes)
		{
			growUnifiedBuffer(m_GlobalVertexBuffer, UsedVertexBufferBytes, UsedVertexBufferBytes + vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "vertex");
		}
		if (UsedIndexBufferBytes + indexSize > m_GlobalIndexBuffer.m_SizeInBytes)
		{
			growUnifiedBuffer(m_GlobalIndexBuffer, UsedIndexBufferBytes, UsedIndexBufferBytes + indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "index");
		}
	}
	catch (const std::exception&)
	{
		// growUnifiedBuffer already logged why (most likely out of GPU memory). The old buffers are untouched.
		LOGE("Couldn't make room for {} vertices / {} indices in the unified geometry buffers. Aborting load.", vertices.size(), indices.size());
		return 0;
	}

	// 1-3. Queue the copies into the unified buffers (at the new offsets). They go to the GPU with everything else loaded
	//      before the next frame, in one batch, without the CPU waiting for any of it.
	m_ResourceUploader.queueBufferUpload(vertices.data(), vertexSize, m_GlobalVertexBuffer.m_Buffer, static_cast<VkDeviceSize>(m_CurrentVertexOffset) * sizeof(Vertex));
	m_ResourceUploader.queueBufferUpload(indices.data(), indexSize, m_GlobalIndexBuffer.m_Buffer, static_cast<VkDeviceSize>(m_CurrentIndexOffset) * sizeof(uint32_t));

	// 4. Save the Asset Descriptor
	MeshAsset asset{};
	asset.firstIndex = m_CurrentIndexOffset;
	asset.indexCount = static_cast<uint32_t>(indices.size());
	asset.vertexOffset = m_CurrentVertexOffset;
	asset.texId = texId;
	m_Meshes.push_back(asset);

	// 5. Advance Offsets
	m_CurrentVertexOffset += static_cast<uint32_t>(vertices.size());
	m_CurrentIndexOffset += static_cast<uint32_t>(indices.size());

	return static_cast<uint32_t>(m_Meshes.size() - 1);
}

void Vulkanised::VulkanRenderer::SubmitFrame(const std::vector<RenderInstance>& instances)
{
	// Finish ImGui's frame FIRST, before anything below can bail out early (an out-of-date swapchain): every
	// ImGui::NewFrame must be matched by a Render, or the next NewFrame asserts. Render only builds CPU-side draw data.
	m_ShouldRenderImGuiThisFrame = false;
	if (m_HasImGuiFrameBegun)
	{
		ImGui::Render();
		m_HasImGuiFrameBegun = false;
		m_ShouldRenderImGuiThisFrame = true;
	}

	// Frame interval: from the start of the previous SubmitFrame to the start of this one
	const std::chrono::steady_clock::time_point SubmitFrameStartTime = std::chrono::steady_clock::now();
	if (m_PreviousSubmitFrameStartTime != std::chrono::steady_clock::time_point{} && !m_WasPreviousFrameAScreenshotFrame)
	{
		recordFrameStatistic(m_FrameStatistics.m_FrameIntervalInMilliseconds, std::chrono::duration<float, std::milli>(SubmitFrameStartTime - m_PreviousSubmitFrameStartTime).count());
	}
	m_PreviousSubmitFrameStartTime = SubmitFrameStartTime;

	// The window was resized (or minimised): rebuild the swapchain first, instead of waiting for the driver to complain
	if (doesSwapchainNeedRecreation())
	{
		recreateSwapchain();
	}

	Rendering::FrameResources& CurrentFrame = m_Frames[m_CurrentFrame];

	// 0. Wait until the GPU is done with this frame's resources (from their use c_MaxFrameDraws frames ago)...
	vkWaitForFences(m_MainDevice.logicalDevice, 1, &CurrentFrame.m_InFlightFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
	const std::chrono::steady_clock::time_point GpuWaitEndTime = std::chrono::steady_clock::now();
	recordFrameStatistic(m_FrameStatistics.m_CpuWaitForGpuTimeInMilliseconds, std::chrono::duration<float, std::milli>(GpuWaitEndTime - SubmitFrameStartTime).count());
	readFinishedTimestampQueries(CurrentFrame);

	// ...then get an image to draw into. The fence is only reset once this frame is sure to be submitted: it used to be
	// reset BEFORE acquiring, so an out-of-date acquire returned with it unsignalled, and the next wait on it never ended.
	uint32_t SwapchainImageIndex{ 0 };
	const VkResult AcquireResult = vkAcquireNextImageKHR(m_MainDevice.logicalDevice, m_Swapchain, std::numeric_limits<uint64_t>::max(),
		CurrentFrame.m_SwapchainImageAvailableSemaphore, VK_NULL_HANDLE, &SwapchainImageIndex);
	if (AcquireResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		recreateSwapchain();
		return;
	}
	if (AcquireResult != VK_SUCCESS && AcquireResult != VK_SUBOPTIMAL_KHR)	// Suboptimal still works: the size check above deals with it next frame
	{
		LOGNTHROW("Failed to acquire a swapchain image (VkResult {})", static_cast<int>(AcquireResult));
	}
	vkResetFences(m_MainDevice.logicalDevice, 1, &CurrentFrame.m_InFlightFence);

	// Everything loaded since the last frame goes to the GPU now, in one batch, ahead of this frame on the same queue
	m_ResourceUploader.submitPendingUploads();

	// 1. Sort the incoming ECS payload to maximize batching (Group by Mesh, then Texture)
	std::vector<RenderInstance> sortedInstances = instances;

	// The SSBO only has room for MAX_INSTANCES transforms. Multi-mesh models multiply the instance count, so guard it.
	if (sortedInstances.size() > MAX_INSTANCES)
	{
		LOGW("{} render instances submitted, but only {} fit in the instance buffer. Dropping the rest.", sortedInstances.size(), MAX_INSTANCES);
		sortedInstances.resize(MAX_INSTANCES);
	}

	std::sort(sortedInstances.begin(), sortedInstances.end(), [](const RenderInstance& a, const RenderInstance& b) {
		if (a.meshID != b.meshID) return a.meshID < b.meshID;
		return a.textureID < b.textureID;
		});

	// 2. Write every transform into this frame's (persistently mapped) instance buffer
	glm::mat4* InstanceTransforms = static_cast<glm::mat4*>(CurrentFrame.m_InstanceBuffer.m_MappedData);
	for (size_t CurrentInstanceIndex{ 0 }; CurrentInstanceIndex < sortedInstances.size(); ++CurrentInstanceIndex)
	{
		InstanceTransforms[CurrentInstanceIndex] = sortedInstances[CurrentInstanceIndex].transform;
	}
	m_GpuMemory.flushBufferWrites(CurrentFrame.m_InstanceBuffer, 0, sizeof(glm::mat4) * std::max<size_t>(sortedInstances.size(), 1));
	m_FrameStatistics.m_SubmittedInstanceCount = static_cast<uint32_t>(instances.size());
	m_FrameStatistics.m_DrawnInstanceCount = static_cast<uint32_t>(sortedInstances.size());
	m_FrameStatistics.m_CulledInstanceCount = 0;

	// 3. Camera and light
	updateProjectionMatrixFromCameraLens();	// Cheap, and keeps the aspect ratio right after any resize
	memcpy(CurrentFrame.m_CameraUniformBuffer.m_MappedData, &m_UboViewProjection, sizeof(UboViewProjection));
	m_GpuMemory.flushBufferWrites(CurrentFrame.m_CameraUniformBuffer);

	// 4. Record. The whole pool is reset at once (cheaper than resetting the buffer on its own).
	vkResetCommandPool(m_MainDevice.logicalDevice, CurrentFrame.m_CommandPool, 0);
	prepareScreenshotBufferIfRequested();	// A screenshot was asked for: this frame also copies its finished image into a CPU-readable buffer
	recordCommands(CurrentFrame, SwapchainImageIndex, sortedInstances);

	// 5. Submit (synchronization2): wait for the image to be available before writing colour, then signal this image's
	//    render-finished semaphore once every command is done, and the frame's fence
	VkSemaphoreSubmitInfo ImageAvailableWait{};
	ImageAvailableWait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	ImageAvailableWait.semaphore = CurrentFrame.m_SwapchainImageAvailableSemaphore;
	ImageAvailableWait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSemaphoreSubmitInfo RenderFinishedSignal{};
	RenderFinishedSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	RenderFinishedSignal.semaphore = m_RenderFinishedSemaphores[SwapchainImageIndex];
	RenderFinishedSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	VkCommandBufferSubmitInfo CommandBufferSubmitInfo{};
	CommandBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	CommandBufferSubmitInfo.commandBuffer = CurrentFrame.m_CommandBuffer;

	VkSubmitInfo2 SubmitInfo{};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	SubmitInfo.waitSemaphoreInfoCount = 1;
	SubmitInfo.pWaitSemaphoreInfos = &ImageAvailableWait;
	SubmitInfo.commandBufferInfoCount = 1;
	SubmitInfo.pCommandBufferInfos = &CommandBufferSubmitInfo;
	SubmitInfo.signalSemaphoreInfoCount = 1;
	SubmitInfo.pSignalSemaphoreInfos = &RenderFinishedSignal;

	if (vkQueueSubmit2(m_GraphicsQueue, 1, &SubmitInfo, CurrentFrame.m_InFlightFence) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to submit a frame's command buffer");
	}

	const bool IsScreenshotFrame = m_ShouldCaptureScreenshotThisFrame;
	if (IsScreenshotFrame)
	{
		// Only on screenshot frames: the copy has to be finished before reading it (waits for this frame only, not the whole queue)
		vkWaitForFences(m_MainDevice.logicalDevice, 1, &CurrentFrame.m_InFlightFence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		writeCapturedScreenshotToFile();
	}

	// 6. Present
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &m_RenderFinishedSemaphores[SwapchainImageIndex];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &m_Swapchain;
	presentInfo.pImageIndices = &SwapchainImageIndex;

	const VkResult PresentResult = vkQueuePresentKHR(m_PresentationQueue, &presentInfo);

	// Screenshot frames wait for the GPU and write a file: they would drag the averages up, so they (and the interval
	// that ends on the next frame) are left out
	if (!IsScreenshotFrame)
	{
		recordFrameStatistic(m_FrameStatistics.m_CpuSubmitTimeInMilliseconds, std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - GpuWaitEndTime).count());
	}
	m_WasPreviousFrameAScreenshotFrame = IsScreenshotFrame;

	// The frame went out either way (its fence will signal), so move on to the next frame's resources first
	m_CurrentFrame = (m_CurrentFrame + 1) % Utilities::Vulkan::c_MaxFrameDraws;

	if (PresentResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		recreateSwapchain();
	}
	else if (PresentResult != VK_SUCCESS && PresentResult != VK_SUBOPTIMAL_KHR)
	{
		LOGNTHROW("Failed to present a frame (VkResult {})", static_cast<int>(PresentResult));
	}
}

void Vulkanised::VulkanRenderer::recordFrameStatistic(float& SmoothedValue, float NewValue) const noexcept
{
	// Exponential moving average: each frame counts for 5%, so a value settles over ~20 frames instead of flickering
	constexpr float c_NewValueWeight{ 0.05f };
	SmoothedValue = (SmoothedValue == 0.0f) ? NewValue : SmoothedValue + (NewValue - SmoothedValue) * c_NewValueWeight;
}

void Vulkanised::VulkanRenderer::createFrameResources()
{
	// Timestamps need the graphics queue to support them (timestampValidBits) and a non-zero tick length
	uint32_t QueueFamilyCount{ 0 };
	vkGetPhysicalDeviceQueueFamilyProperties(m_MainDevice.physicalDevice, &QueueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> QueueFamilies(QueueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_MainDevice.physicalDevice, &QueueFamilyCount, QueueFamilies.data());
	const int GraphicsQueueFamilyIndex = getQueueFamilies(m_MainDevice.physicalDevice).graphicsFamily;
	m_CanWriteTimestamps = QueueFamilies[GraphicsQueueFamilyIndex].timestampValidBits > 0 && m_PhysicalDeviceProperties.limits.timestampPeriod > 0.0f;
	m_FrameStatistics.m_IsGpuTimingAvailable = m_CanWriteTimestamps;
	if (!m_CanWriteTimestamps)
	{
		LOGW("This GPU's graphics queue can't write timestamps, GPU frame times won't be measured.");
	}

	VkCommandPoolCreateInfo CommandPoolCreateInfo{};
	CommandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	CommandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;	// Its one buffer is re-recorded every frame
	CommandPoolCreateInfo.queueFamilyIndex = static_cast<uint32_t>(GraphicsQueueFamilyIndex);

	VkFenceCreateInfo FenceCreateInfo{};
	FenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	FenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;	// So the very first wait on it returns straight away

	VkSemaphoreCreateInfo SemaphoreCreateInfo{};
	SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	for (size_t CurrentFrameIndex{ 0 }; CurrentFrameIndex < m_Frames.size(); ++CurrentFrameIndex)
	{
		Rendering::FrameResources& CurrentFrame = m_Frames[CurrentFrameIndex];

		if (vkCreateCommandPool(m_MainDevice.logicalDevice, &CommandPoolCreateInfo, nullptr, &CurrentFrame.m_CommandPool) != VK_SUCCESS)
		{
			LOGNTHROW("Failed to create a frame's command pool");
		}
		VkCommandBufferAllocateInfo CommandBufferAllocateInfo{};
		CommandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		CommandBufferAllocateInfo.commandPool = CurrentFrame.m_CommandPool;
		CommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		CommandBufferAllocateInfo.commandBufferCount = 1;
		if (vkAllocateCommandBuffers(m_MainDevice.logicalDevice, &CommandBufferAllocateInfo, &CurrentFrame.m_CommandBuffer) != VK_SUCCESS)
		{
			LOGNTHROW("Failed to allocate a frame's command buffer");
		}

		if (vkCreateFence(m_MainDevice.logicalDevice, &FenceCreateInfo, nullptr, &CurrentFrame.m_InFlightFence) != VK_SUCCESS
			|| vkCreateSemaphore(m_MainDevice.logicalDevice, &SemaphoreCreateInfo, nullptr, &CurrentFrame.m_SwapchainImageAvailableSemaphore) != VK_SUCCESS)
		{
			LOGNTHROW("Failed to create a frame's fence or semaphore");
		}

		// Persistently mapped, written straight into every frame
		CurrentFrame.m_InstanceBuffer = m_GpuMemory.createBuffer(sizeof(glm::mat4) * MAX_INSTANCES, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			Rendering::GpuMemoryUsage::CpuWritesGpuReads, "Instance data (frame " + std::to_string(CurrentFrameIndex) + ")");
		CurrentFrame.m_CameraUniformBuffer = m_GpuMemory.createBuffer(sizeof(UboViewProjection), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			Rendering::GpuMemoryUsage::CpuWritesGpuReads, "Camera and light (frame " + std::to_string(CurrentFrameIndex) + ")");

		if (m_CanWriteTimestamps)
		{
			VkQueryPoolCreateInfo QueryPoolCreateInfo{};
			QueryPoolCreateInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			QueryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			QueryPoolCreateInfo.queryCount = 2;
			if (vkCreateQueryPool(m_MainDevice.logicalDevice, &QueryPoolCreateInfo, nullptr, &CurrentFrame.m_TimestampQueryPool) != VK_SUCCESS)
			{
				LOGNTHROW("Failed to create a timestamp query pool");
			}
		}

		// Set 0: camera uniforms (binding 0) and instance data (binding 1)
		VkDescriptorSetAllocateInfo DescriptorSetAllocateInfo{};
		DescriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		DescriptorSetAllocateInfo.descriptorPool = m_DescriptorPool;
		DescriptorSetAllocateInfo.descriptorSetCount = 1;
		DescriptorSetAllocateInfo.pSetLayouts = &m_DescriptorSetLayout;
		if (vkAllocateDescriptorSets(m_MainDevice.logicalDevice, &DescriptorSetAllocateInfo, &CurrentFrame.m_FrameDescriptorSet) != VK_SUCCESS)
		{
			LOGNTHROW("Failed to allocate a frame's descriptor set");
		}

		const VkDescriptorBufferInfo CameraBufferInfo{ CurrentFrame.m_CameraUniformBuffer.m_Buffer, 0, sizeof(UboViewProjection) };
		const VkDescriptorBufferInfo InstanceBufferInfo{ CurrentFrame.m_InstanceBuffer.m_Buffer, 0, VK_WHOLE_SIZE };
		std::array<VkWriteDescriptorSet, 2> DescriptorWrites{};
		DescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DescriptorWrites[0].dstSet = CurrentFrame.m_FrameDescriptorSet;
		DescriptorWrites[0].dstBinding = 0;
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		DescriptorWrites[0].pBufferInfo = &CameraBufferInfo;
		DescriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DescriptorWrites[1].dstSet = CurrentFrame.m_FrameDescriptorSet;
		DescriptorWrites[1].dstBinding = 1;
		DescriptorWrites[1].descriptorCount = 1;
		DescriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		DescriptorWrites[1].pBufferInfo = &InstanceBufferInfo;
		vkUpdateDescriptorSets(m_MainDevice.logicalDevice, static_cast<uint32_t>(DescriptorWrites.size()), DescriptorWrites.data(), 0, nullptr);
	}
}

void Vulkanised::VulkanRenderer::destroyFrameResources() noexcept
{
	for (Rendering::FrameResources& CurrentFrame : m_Frames)
	{
		// The descriptor set goes back with its pool, and the command buffer with its command pool
		vkDestroyQueryPool(m_MainDevice.logicalDevice, CurrentFrame.m_TimestampQueryPool, nullptr);
		m_GpuMemory.destroyBuffer(CurrentFrame.m_CameraUniformBuffer);
		m_GpuMemory.destroyBuffer(CurrentFrame.m_InstanceBuffer);
		vkDestroySemaphore(m_MainDevice.logicalDevice, CurrentFrame.m_SwapchainImageAvailableSemaphore, nullptr);
		vkDestroyFence(m_MainDevice.logicalDevice, CurrentFrame.m_InFlightFence, nullptr);
		vkDestroyCommandPool(m_MainDevice.logicalDevice, CurrentFrame.m_CommandPool, nullptr);
		CurrentFrame = {};
	}
}

void Vulkanised::VulkanRenderer::createRenderFinishedSemaphores()
{
	VkSemaphoreCreateInfo SemaphoreCreateInfo{};
	SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	m_RenderFinishedSemaphores.resize(m_SwapchainImages.size(), VK_NULL_HANDLE);
	for (VkSemaphore& CurrentSemaphore : m_RenderFinishedSemaphores)
	{
		if (vkCreateSemaphore(m_MainDevice.logicalDevice, &SemaphoreCreateInfo, nullptr, &CurrentSemaphore) != VK_SUCCESS)
		{
			LOGNTHROW("Failed to create a render finished semaphore");
		}
	}
}

void Vulkanised::VulkanRenderer::destroyRenderFinishedSemaphores() noexcept
{
	for (VkSemaphore CurrentSemaphore : m_RenderFinishedSemaphores)
	{
		vkDestroySemaphore(m_MainDevice.logicalDevice, CurrentSemaphore, nullptr);
	}
	m_RenderFinishedSemaphores.clear();
}

void Vulkanised::VulkanRenderer::readFinishedTimestampQueries(Rendering::FrameResources& FinishedFrame)
{
	if (!m_CanWriteTimestamps || !FinishedFrame.m_HasTimestampQueriesToRead)
	{
		return;
	}

	// Called right after this frame's fence was waited on, so the results are there (no WAIT flag: never stall here)
	std::array<uint64_t, 2> TimestampTicks{};
	const VkResult QueryResult = vkGetQueryPoolResults(m_MainDevice.logicalDevice, FinishedFrame.m_TimestampQueryPool, 0, 2,
		sizeof(TimestampTicks), TimestampTicks.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
	if (QueryResult == VK_SUCCESS && TimestampTicks[1] >= TimestampTicks[0])
	{
		const double ElapsedNanoseconds = static_cast<double>(TimestampTicks[1] - TimestampTicks[0]) * m_PhysicalDeviceProperties.limits.timestampPeriod;
		recordFrameStatistic(m_FrameStatistics.m_GpuFrameTimeInMilliseconds, static_cast<float>(ElapsedNanoseconds / 1.0e6));
	}
	FinishedFrame.m_HasTimestampQueriesToRead = false;
}

void Vulkanised::VulkanRenderer::requestScreenshot(const std::filesystem::path& OutputBitmapFilePath)
{
	m_PendingScreenshotFilePath = OutputBitmapFilePath;
}

void Vulkanised::VulkanRenderer::prepareScreenshotBufferIfRequested()
{
	m_ShouldCaptureScreenshotThisFrame = false;
	if (!m_PendingScreenshotFilePath.has_value())
	{
		return;
	}

	const bool CanCopyFromSwapchain = m_SwapchainImageFormat == VK_FORMAT_B8G8R8A8_UNORM || m_SwapchainImageFormat == VK_FORMAT_B8G8R8A8_SRGB
		|| m_SwapchainImageFormat == VK_FORMAT_R8G8B8A8_UNORM || m_SwapchainImageFormat == VK_FORMAT_R8G8B8A8_SRGB;
	if (!CanCopyFromSwapchain)
	{
		LOGW("Screenshots only support 8-bit RGBA/BGRA swapchains (this one is VkFormat {}).", static_cast<int>(m_SwapchainImageFormat));
		m_PendingScreenshotFilePath.reset();
		return;
	}

	const VkDeviceSize ScreenshotSizeInBytes = static_cast<VkDeviceSize>(m_SwapchainExtent.width) * m_SwapchainExtent.height * 4;
	m_ScreenshotBuffer = m_GpuMemory.createBuffer(ScreenshotSizeInBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, Rendering::GpuMemoryUsage::GpuWritesCpuReads, "Screenshot readback");
	m_ShouldCaptureScreenshotThisFrame = true;
}

void Vulkanised::VulkanRenderer::recordScreenshotCopy(VkCommandBuffer CommandBuffer, VkImage SwapchainImage)
{
	// Rendering left the image as a colour attachment: make it a copy source, copy it out, then hand it over for presenting
	Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
		.m_Image = SwapchainImage, .m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.m_OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .m_NewLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .m_AccessesThatMustFinish = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_COPY_BIT, .m_AccessesThatMustWait = VK_ACCESS_2_TRANSFER_READ_BIT });

	VkBufferImageCopy CopyRegion{};
	CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	CopyRegion.imageExtent = { m_SwapchainExtent.width, m_SwapchainExtent.height, 1 };
	vkCmdCopyImageToBuffer(CommandBuffer, SwapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_ScreenshotBuffer.m_Buffer, 1, &CopyRegion);

	// The CPU reads the copy once the frame's fence signals, but a fence alone doesn't make GPU writes visible to the host
	VkMemoryBarrier2 CopyToHostBarrier{};
	CopyToHostBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	CopyToHostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	CopyToHostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	CopyToHostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
	CopyToHostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
	VkDependencyInfo CopyToHostDependency{};
	CopyToHostDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	CopyToHostDependency.memoryBarrierCount = 1;
	CopyToHostDependency.pMemoryBarriers = &CopyToHostBarrier;
	vkCmdPipelineBarrier2(CommandBuffer, &CopyToHostDependency);

	Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
		.m_Image = SwapchainImage, .m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.m_OldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, .m_NewLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_COPY_BIT, .m_AccessesThatMustFinish = VK_ACCESS_2_NONE,
		.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_NONE, .m_AccessesThatMustWait = VK_ACCESS_2_NONE });
}

void Vulkanised::VulkanRenderer::writeCapturedScreenshotToFile()
{
	const std::filesystem::path OutputFilePath = m_PendingScreenshotFilePath.value_or("screenshot.bmp");
	const uint32_t ScreenshotWidthInPixels = m_SwapchainExtent.width;
	const uint32_t ScreenshotHeightInPixels = m_SwapchainExtent.height;
	constexpr uint32_t c_BytesPerPixel{ 4 };
	const uint32_t PixelDataSizeInBytes = ScreenshotWidthInPixels * ScreenshotHeightInPixels * c_BytesPerPixel;

	m_GpuMemory.invalidateBufferForReading(m_ScreenshotBuffer);	// Make the GPU's copy visible to the CPU (free on coherent memory)
	const void* MappedPixels = m_ScreenshotBuffer.m_MappedData;

	// Written as a 32-bit, top-down .bmp: the simplest image format there is, no library needed
	std::error_code DirectoryCreationError;
	std::filesystem::create_directories(OutputFilePath.parent_path(), DirectoryCreationError);
	std::ofstream BitmapFile(OutputFilePath, std::ios::binary);
	if (BitmapFile)
	{
		// BMP stores every number little-endian: lowest byte first
		auto writeLittleEndianNumber = [&BitmapFile](uint32_t NumberToWrite, int ByteCountToWrite)
		{
			for (int CurrentByteIndex{ 0 }; CurrentByteIndex < ByteCountToWrite; ++CurrentByteIndex)
			{
				BitmapFile.put(static_cast<char>((NumberToWrite >> (8 * CurrentByteIndex)) & 0xFF));
			}
		};

		constexpr uint32_t c_FileHeaderSizeInBytes{ 14 };
		constexpr uint32_t c_InfoHeaderSizeInBytes{ 40 };				// BITMAPINFOHEADER
		constexpr uint32_t c_PixelDataOffsetInBytes{ c_FileHeaderSizeInBytes + c_InfoHeaderSizeInBytes };
		constexpr uint32_t c_PixelsPerMetreAt72DotsPerInch{ 2835 };

		// File header
		BitmapFile.put('B'); BitmapFile.put('M');
		writeLittleEndianNumber(c_PixelDataOffsetInBytes + PixelDataSizeInBytes, 4);	// Whole file size
		writeLittleEndianNumber(0, 4);												// Reserved
		writeLittleEndianNumber(c_PixelDataOffsetInBytes, 4);						// Where the pixels start
		// Info header
		writeLittleEndianNumber(c_InfoHeaderSizeInBytes, 4);
		writeLittleEndianNumber(ScreenshotWidthInPixels, 4);
		writeLittleEndianNumber(static_cast<uint32_t>(-static_cast<int32_t>(ScreenshotHeightInPixels)), 4);	// Negative height = rows stored top-down, like the swapchain
		writeLittleEndianNumber(1, 2);												// Colour planes (always 1)
		writeLittleEndianNumber(c_BytesPerPixel * 8, 2);								// Bits per pixel
		writeLittleEndianNumber(0, 4);												// No compression
		writeLittleEndianNumber(PixelDataSizeInBytes, 4);
		writeLittleEndianNumber(c_PixelsPerMetreAt72DotsPerInch, 4);					// Horizontal resolution
		writeLittleEndianNumber(c_PixelsPerMetreAt72DotsPerInch, 4);					// Vertical resolution
		writeLittleEndianNumber(0, 4);												// Palette colours (none)
		writeLittleEndianNumber(0, 4);												// Important colours (all)

		// BMP wants each pixel as Blue, Green, Red, Alpha: RGBA swapchains get their red and blue swapped
		std::vector<unsigned char> PixelBytes(static_cast<const unsigned char*>(MappedPixels), static_cast<const unsigned char*>(MappedPixels) + PixelDataSizeInBytes);
		const bool IsSwapchainRedFirst = m_SwapchainImageFormat == VK_FORMAT_R8G8B8A8_UNORM || m_SwapchainImageFormat == VK_FORMAT_R8G8B8A8_SRGB;
		for (std::size_t CurrentPixelFirstByteIndex{ 0 }; CurrentPixelFirstByteIndex < PixelBytes.size(); CurrentPixelFirstByteIndex += c_BytesPerPixel)
		{
			if (IsSwapchainRedFirst) { std::swap(PixelBytes[CurrentPixelFirstByteIndex], PixelBytes[CurrentPixelFirstByteIndex + 2]); }
			PixelBytes[CurrentPixelFirstByteIndex + 3] = 255;	// Opaque, whatever the swapchain's alpha ended up as
		}
		BitmapFile.write(reinterpret_cast<const char*>(PixelBytes.data()), static_cast<std::streamsize>(PixelBytes.size()));
		LOG("Screenshot saved to {}", OutputFilePath.string());
	}
	else
	{
		LOGE("Couldn't write screenshot {}", OutputFilePath.string());
	}

	m_GpuMemory.destroyBuffer(m_ScreenshotBuffer);
	m_ShouldCaptureScreenshotThisFrame = false;
	m_PendingScreenshotFilePath.reset();
}

void Vulkanised::VulkanRenderer::initialiseImGui(GLFWwindow* WindowToReceiveImGuiInput)
{
	if (m_IsImGuiInitialised)
	{
		LOGW("ImGui is already initialised, ignoring the second initialiseImGui call.");
		return;
	}
	if (WindowToReceiveImGuiInput == nullptr)
	{
		LOGNTHROW("initialiseImGui needs a window");
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	// imgui.ini (window positions/sizes) next to the exe, like every other runtime file, not wherever the working directory is
	m_ImGuiIniFilePath = (Utilities::File::getExecutableDirectory() / "imgui.ini").string();
	ImGui::GetIO().IniFilename = m_ImGuiIniFilePath.c_str();

	// true = install callbacks, CHAINING to whatever was installed before (e.g. Hephaestian's InputManager), so both get input
	ImGui_ImplGlfw_InitForVulkan(WindowToReceiveImGuiInput, true);

	ImGui_ImplVulkan_InitInfo ImGuiVulkanInitInfo{};
	ImGuiVulkanInitInfo.ApiVersion = VK_API_VERSION_1_4;	// Must match createInstance's appInfo.apiVersion
	ImGuiVulkanInitInfo.Instance = m_Instance;
	ImGuiVulkanInitInfo.PhysicalDevice = m_MainDevice.physicalDevice;
	ImGuiVulkanInitInfo.Device = m_MainDevice.logicalDevice;
	ImGuiVulkanInitInfo.QueueFamily = static_cast<uint32_t>(getQueueFamilies(m_MainDevice.physicalDevice).graphicsFamily);
	ImGuiVulkanInitInfo.Queue = m_GraphicsQueue;
	ImGuiVulkanInitInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE * 2;	// Let the backend own its pool, so it can't eat into ours
	ImGuiVulkanInitInfo.MinImageCount = 2;
	ImGuiVulkanInitInfo.ImageCount = std::max<uint32_t>(2, static_cast<uint32_t>(m_SwapchainImages.size()));
	// Drawn inside our own rendering (dynamic rendering): ImGui's pipeline must declare the same attachment formats
	ImGuiVulkanInitInfo.UseDynamicRendering = true;
	ImGuiVulkanInitInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	ImGuiVulkanInitInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	ImGuiVulkanInitInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_SwapchainOverlayFormat;	// Overlay pass (UNORM view). ImGui copies it.
	ImGuiVulkanInitInfo.PipelineInfoMain.PipelineRenderingCreateInfo.depthAttachmentFormat = m_DepthBufferFormat;
	ImGuiVulkanInitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	ImGuiVulkanInitInfo.CheckVkResultFn = [](VkResult ImGuiVulkanResult)
	{
		if (ImGuiVulkanResult < 0)
		{
			LOGE("ImGui Vulkan backend error: VkResult {}", static_cast<int>(ImGuiVulkanResult));
		}
	};

	if (!ImGui_ImplVulkan_Init(&ImGuiVulkanInitInfo))
	{
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		LOGNTHROW("Failed to initialise the ImGui Vulkan backend");
	}

	m_IsImGuiInitialised = true;
	LOG("ImGui {} initialised", IMGUI_VERSION);
}

void Vulkanised::VulkanRenderer::beginImGuiFrame()
{
	if (!m_IsImGuiInitialised)
	{
		return;
	}
	if (m_HasImGuiFrameBegun)
	{
		// Two begins without a SubmitFrame in between: close the old frame so ImGui's own frame bookkeeping stays balanced
		ImGui::EndFrame();
	}

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	m_HasImGuiFrameBegun = true;
}

void Vulkanised::VulkanRenderer::shutdownImGui() noexcept
{
	if (!m_IsImGuiInitialised)
	{
		return;
	}
	if (m_HasImGuiFrameBegun)
	{
		ImGui::EndFrame();
		m_HasImGuiFrameBegun = false;
	}

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();	// Also puts back the GLFW callbacks it chained onto
	ImGui::DestroyContext();
	m_IsImGuiInitialised = false;
	m_ShouldRenderImGuiThisFrame = false;
}

uint32_t Vulkanised::VulkanRenderer::createModel(const std::vector<ModelSubmesh>& SubmeshesMakingUpTheModel)
{
	ModelAsset NewModelAsset{};
	NewModelAsset.m_Submeshes.reserve(SubmeshesMakingUpTheModel.size());

	for (const ModelSubmesh& CurrentSubmesh : SubmeshesMakingUpTheModel)
	{
		// A submesh pointing at garbage would only be caught every single frame by recordCommands, so catch it once here instead
		if (CurrentSubmesh.m_MeshIdentifier >= m_Meshes.size())
		{
			LOGW("Model submesh references mesh {}, which does not exist. Skipping that submesh.", CurrentSubmesh.m_MeshIdentifier);
			continue;
		}
		if (CurrentSubmesh.m_TextureIdentifier >= m_TextureCount)
		{
			LOGW("Model submesh references texture {}, which does not exist. Skipping that submesh.", CurrentSubmesh.m_TextureIdentifier);
			continue;
		}
		NewModelAsset.m_Submeshes.push_back(CurrentSubmesh);
	}

	if (NewModelAsset.m_Submeshes.empty())
	{
		LOGW("Created a model with no valid submeshes, it will not render anything.");
	}

	m_Models.push_back(std::move(NewModelAsset));
	return static_cast<uint32_t>(m_Models.size() - 1);
}

uint32_t Vulkanised::VulkanRenderer::loadModel(const std::vector<ModelSubmeshGeometry>& SubmeshGeometriesMakingUpTheModel)
{
	std::vector<ModelSubmesh> UploadedSubmeshes;
	UploadedSubmeshes.reserve(SubmeshGeometriesMakingUpTheModel.size());

	for (const ModelSubmeshGeometry& CurrentSubmeshGeometry : SubmeshGeometriesMakingUpTheModel)
	{
		const size_t MeshCountBeforeUpload = m_Meshes.size();
		const uint32_t UploadedMeshIdentifier = loadMesh(CurrentSubmeshGeometry.m_Vertices, CurrentSubmeshGeometry.m_Indices, static_cast<int>(CurrentSubmeshGeometry.m_TextureIdentifier));

		// loadMesh returns 0 on failure without adding anything, so the only reliable check is whether the list grew
		if (m_Meshes.size() == MeshCountBeforeUpload)
		{
			LOGW("A submesh of the model failed to upload. Skipping that submesh.");
			continue;
		}

		UploadedSubmeshes.push_back({ UploadedMeshIdentifier, CurrentSubmeshGeometry.m_TextureIdentifier, CurrentSubmeshGeometry.m_LocalTransformRelativeToModelRoot });
	}

	return createModel(UploadedSubmeshes);
}

uint32_t Vulkanised::VulkanRenderer::createModelFromSingleMesh(uint32_t MeshIdentifier, uint32_t TextureIdentifier)
{
	return createModel({ ModelSubmesh{ MeshIdentifier, TextureIdentifier, glm::mat4(1.0f) } });
}

bool Vulkanised::VulkanRenderer::isModelIdentifierValid(uint32_t ModelIdentifier) const noexcept
{
	return ModelIdentifier < m_Models.size();
}

const ModelAsset& Vulkanised::VulkanRenderer::getModelAsset(uint32_t ModelIdentifier) const
{
	if (!isModelIdentifierValid(ModelIdentifier))
	{
		LOGNTHROW("Requested model {}, but only {} models exist.", ModelIdentifier, m_Models.size());
	}
	return m_Models[ModelIdentifier];
}

void Vulkanised::VulkanRenderer::setCameraViewAndLens(const glm::mat4& CameraViewMatrix, const glm::vec3& CameraWorldPosition, float VerticalFieldOfViewInRadians, float NearClippingPlaneDistance, float FarClippingPlaneDistance) noexcept
{
	m_UboViewProjection.view = CameraViewMatrix;
	m_UboViewProjection.viewPos = glm::vec4(CameraWorldPosition, 1.0f);	// Specular lighting needs to know where the eye actually is

	m_CameraVerticalFieldOfViewInRadians = VerticalFieldOfViewInRadians;
	m_CameraNearClippingPlaneDistance = NearClippingPlaneDistance;
	m_CameraFarClippingPlaneDistance = FarClippingPlaneDistance;
}

float Vulkanised::VulkanRenderer::getSwapchainAspectRatio() const noexcept
{
	// Minimised windows have a 0 height, don't divide by it
	if (m_SwapchainExtent.height == 0)
	{
		return 1.0f;
	}
	return static_cast<float>(m_SwapchainExtent.width) / static_cast<float>(m_SwapchainExtent.height);
}

void Vulkanised::VulkanRenderer::updateProjectionMatrixFromCameraLens() noexcept
{
	// Explicitly the zero-to-one depth version, which is what Vulkan expects (GLM_FORCE_DEPTH_ZERO_TO_ONE in pch.h is defined after glm is included, so it never applied)
	m_UboViewProjection.projection = glm::perspectiveRH_ZO(m_CameraVerticalFieldOfViewInRadians, getSwapchainAspectRatio(), m_CameraNearClippingPlaneDistance, m_CameraFarClippingPlaneDistance);

	// Vulkan's clip space Y points down, GLM's points up
	m_UboViewProjection.projection[1][1] *= -1;
}

void Vulkanised::VulkanRenderer::cleanup() noexcept
{
	// Wait until no actions being run on device before destroying
	vkDeviceWaitIdle(m_MainDevice.logicalDevice);

	// ImGui owns Vulkan objects of its own (pipeline, font texture, descriptor pool), so it goes before the device does
	shutdownImGui();

	if (m_BindlessTexturePool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(m_MainDevice.logicalDevice, m_BindlessTexturePool, nullptr);
		m_BindlessTexturePool = VK_NULL_HANDLE;
	}
	if (m_BindlessTextureSetLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(m_MainDevice.logicalDevice, m_BindlessTextureSetLayout, nullptr);
		m_BindlessTextureSetLayout = VK_NULL_HANDLE;
	}
	for (VkSampler* CurrentSampler : { &m_SmoothTextureSampler, &m_PixelatedTextureSampler })
	{
		vkDestroySampler(m_MainDevice.logicalDevice, *CurrentSampler, nullptr);
		*CurrentSampler = VK_NULL_HANDLE;
	}

	for (Rendering::AllocatedImage& CurrentTextureImage : m_TextureImages)
	{
		m_GpuMemory.destroyImage(CurrentTextureImage);
	}
	m_TextureImages.clear();
	m_GpuMemory.destroyBuffer(m_ScreenshotBuffer);	// Only alive if the app closed with a screenshot still pending

	// Frames in flight (their descriptor sets go back with m_DescriptorPool, just below)
	destroyFrameResources();
	destroyRenderFinishedSemaphores();

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

	// Destroy Unified Geometry Buffers (These survive window resizes, so they only die on app exit)
	m_GpuMemory.destroyBuffer(m_GlobalVertexBuffer);
	m_GpuMemory.destroyBuffer(m_GlobalIndexBuffer);

	// Uploads still queued are dropped, submitted ones waited for (the device is idle already anyway)
	m_ResourceUploader.shutdown();


	if (m_InfiniteGridPipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(m_MainDevice.logicalDevice, m_InfiniteGridPipeline, nullptr);
		m_InfiniteGridPipeline = VK_NULL_HANDLE;
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
	// Every buffer and image is gone by now, so the allocator can go (it reports anything that leaked)
	m_GpuMemory.shutdown();

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

void Vulkanised::VulkanRenderer::createUnifiedBuffers()
{
	// Create one massive 64MB buffer for all vertices (TRANSFER_SRC too, so it can be copied into a bigger one when it fills up)
	m_GlobalVertexBuffer = m_GpuMemory.createBuffer(UNIFIED_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		Rendering::GpuMemoryUsage::GpuOnly, "Unified vertex buffer");

	// Create one massive 64MB buffer for all indices
	m_GlobalIndexBuffer = m_GpuMemory.createBuffer(UNIFIED_BUFFER_SIZE, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		Rendering::GpuMemoryUsage::GpuOnly, "Unified index buffer");
}

void Vulkanised::VulkanRenderer::growUnifiedBuffer(Rendering::AllocatedBuffer& BufferToGrow, VkDeviceSize UsedBytesToKeep, VkDeviceSize RequiredCapacityInBytes,
	VkBufferUsageFlags BufferUsage, std::string_view BufferNameForLogging)
{
	// Doubling keeps the number of (slow) grows logarithmic when lots of meshes are loaded one after another
	const VkDeviceSize PreviousCapacityInBytes = BufferToGrow.m_SizeInBytes;
	VkDeviceSize NewCapacityInBytes = std::max(PreviousCapacityInBytes, UNIFIED_BUFFER_SIZE);
	while (NewCapacityInBytes < RequiredCapacityInBytes)
	{
		NewCapacityInBytes *= 2;
	}

	Rendering::AllocatedBuffer GrownBuffer;
	try
	{
		GrownBuffer = m_GpuMemory.createBuffer(NewCapacityInBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | BufferUsage,
			Rendering::GpuMemoryUsage::GpuOnly, std::string("Unified ") + std::string(BufferNameForLogging) + " buffer");
	}
	catch (const std::exception&)
	{
		LOGE("Failed to grow the unified {} buffer to {} MB.", BufferNameForLogging, NewCapacityInBytes / (1024 * 1024));
		throw;
	}

	// The copy goes into the current upload batch, AFTER any upload into the old buffer that's still queued (it's ordered
	// behind them), and the batch is submitted and waited on before the old buffer dies. Waiting on the batch's fence also
	// covers every frame submitted before it (same queue), so nothing in flight still reads the old buffer either.
	// Draw commands are re-recorded every frame, so they simply pick up the new buffer.
	m_ResourceUploader.queueBufferToBufferCopy(BufferToGrow.m_Buffer, GrownBuffer.m_Buffer, UsedBytesToKeep);
	m_ResourceUploader.submitPendingUploadsAndWait();

	m_GpuMemory.destroyBuffer(BufferToGrow);
	LOG("Grew the unified {} buffer from {} MB to {} MB.", BufferNameForLogging, PreviousCapacityInBytes / (1024 * 1024), NewCapacityInBytes / (1024 * 1024));
	BufferToGrow = GrownBuffer;
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
		LOGNTHROW("validation layers requested, but not available!");
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

	// Add debug extension if validation layers are enabled (plus validation features, which turns synchronisation validation on)
	if (Utilities::Vulkan::enableValidationLayers)
	{
		instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		instanceExtensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
	}

	// Check if instance extensions are supported...
	if(!checkInstanceExtensionSupport(&instanceExtensions))
	{
		LOGNTHROW("VkInstance does not support required extensions!");
	}

	createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
	createInfo.ppEnabledExtensionNames = instanceExtensions.data();

	// Set up validation layers if enabled
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	// Synchronisation validation: catches missing barriers and CPU/GPU races (e.g. the CPU writing a buffer the GPU is still reading)
	constexpr std::array<VkValidationFeatureEnableEXT, 1> c_EnabledValidationFeatures{ VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT };
	VkValidationFeaturesEXT ValidationFeatures{};
	if (Utilities::Vulkan::enableValidationLayers)
	{
		createInfo.enabledLayerCount = static_cast<uint32_t>(Utilities::Vulkan::validationLayers.size());
		createInfo.ppEnabledLayerNames = Utilities::Vulkan::validationLayers.data();

		populateDebugMessengerCreateInfo(debugCreateInfo);
		ValidationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
		ValidationFeatures.enabledValidationFeatureCount = static_cast<uint32_t>(c_EnabledValidationFeatures.size());
		ValidationFeatures.pEnabledValidationFeatures = c_EnabledValidationFeatures.data();
		debugCreateInfo.pNext = &ValidationFeatures;
		createInfo.pNext = &debugCreateInfo;
	}
	else {
		createInfo.enabledLayerCount = 0;
		createInfo.ppEnabledLayerNames = nullptr;
		createInfo.pNext = nullptr;
	}

	// Create instance
	if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create a Vulkan Instance!");
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
	// (The priority must outlive the loop: vkCreateDevice reads it later. It used to be a loop-local, read after it died.)
	static constexpr float c_QueuePriority{ 1.0f };
	for (int queueFamilyIndex : queueFamilyIndices)
	{
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamilyIndex;					// Index of the family to create a queue from
		queueCreateInfo.queueCount = 1;											// Number of queues to create
		queueCreateInfo.pQueuePriorities = &c_QueuePriority;					// Vulkan needs to know how to handle multiple queues, so decide priority (1 = highest priority)

		queueCreateInfos.push_back(queueCreateInfo);
	}

	// Information to create logical device (sometimes called "device")
	VkDeviceCreateInfo deviceCreateInfo{};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());							// Number of Queue Create infos
	deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();													// List of queue create infos so device can create required queues
	// Required extensions, plus the optional ones this GPU has
	std::vector<const char*> EnabledDeviceExtensions(Utilities::Vulkan::deviceExtensions.begin(), Utilities::Vulkan::deviceExtensions.end());
	uint32_t AvailableExtensionCount{ 0 };
	vkEnumerateDeviceExtensionProperties(m_MainDevice.physicalDevice, nullptr, &AvailableExtensionCount, nullptr);
	std::vector<VkExtensionProperties> AvailableExtensions(AvailableExtensionCount);
	vkEnumerateDeviceExtensionProperties(m_MainDevice.physicalDevice, nullptr, &AvailableExtensionCount, AvailableExtensions.data());
	m_IsSwapchainMutableFormatSupported = std::ranges::any_of(AvailableExtensions, [](const VkExtensionProperties& CurrentExtension)
		{ return strcmp(CurrentExtension.extensionName, VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME) == 0; });
	if (m_IsSwapchainMutableFormatSupported)
	{
		EnabledDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME);	// sRGB + UNORM views of the swapchain images
	}
	else
	{
		LOGW("This GPU can't view swapchain images in two formats: the grid and ImGui will look washed out on the sRGB swapchain.");
	}
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(EnabledDeviceExtensions.size());
	deviceCreateInfo.ppEnabledExtensionNames = EnabledDeviceExtensions.data();

	// Physical Device Features the logical device will be using (checkDeviceSuitable already made sure they're all there)
	// - Vulkan 1.3: dynamic rendering (no render pass/framebuffer objects) and synchronization2 (clearer barriers)
	VkPhysicalDeviceVulkan13Features EnabledVulkan13Features{};
	EnabledVulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	EnabledVulkan13Features.dynamicRendering = VK_TRUE;
	EnabledVulkan13Features.synchronization2 = VK_TRUE;
	EnabledVulkan13Features.shaderDemoteToHelperInvocation = VK_TRUE;	// "discard" compiles to this with SPIR-V 1.6 (required on every 1.3 GPU)

	// - Vulkan 1.2: descriptor indexing, so every texture can live in one big array the shaders index into ("bindless")
	VkPhysicalDeviceVulkan12Features EnabledVulkan12Features{};
	EnabledVulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	EnabledVulkan12Features.pNext = &EnabledVulkan13Features;
	EnabledVulkan12Features.descriptorIndexing = VK_TRUE;
	EnabledVulkan12Features.runtimeDescriptorArray = VK_TRUE;
	EnabledVulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
	EnabledVulkan12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	EnabledVulkan12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
	EnabledVulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

	// - Vulkan 1.0: anisotropic filtering, and indirect draws (many draws per call, each with its own first instance)
	VkPhysicalDeviceFeatures2 EnabledFeatures{};
	EnabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	EnabledFeatures.pNext = &EnabledVulkan12Features;
	EnabledFeatures.features.samplerAnisotropy = VK_TRUE;
	EnabledFeatures.features.multiDrawIndirect = VK_TRUE;
	EnabledFeatures.features.drawIndirectFirstInstance = VK_TRUE;

	deviceCreateInfo.pNext = &EnabledFeatures;	// Features go through pNext now (pEnabledFeatures can't express 1.2/1.3 features)
	deviceCreateInfo.pEnabledFeatures = nullptr;

	// Create the logical device for the given physical device
	if (vkCreateDevice(m_MainDevice.physicalDevice, &deviceCreateInfo, nullptr, &m_MainDevice.logicalDevice) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create a Logical Device!");
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
		LOGNTHROW("failed to set up debug messenger!");
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
		LOGNTHROW("failed to create a surface");
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
	if (swapchainDetails.surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
	{
		swapchainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;	// So a finished frame can be copied out (requestScreenshot)
	}
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

	// The scene is drawn through an sRGB view (lighting in linear space, encoded on write). The grid and ImGui work with
	// colours that are ALREADY sRGB, so they are drawn through a second, UNORM view of the same images, which stores their
	// numbers untouched (through the sRGB view they would come out washed out). Needs a mutable-format swapchain.
	const VkFormat OverlayFormat = getUnormFormatOfSrgbFormat(surfaceFormat.format);
	const std::array<VkFormat, 2> SwapchainViewFormats{ surfaceFormat.format, OverlayFormat };
	VkImageFormatListCreateInfo SwapchainViewFormatList{};
	m_SwapchainOverlayFormat = surfaceFormat.format;
	if (m_IsSwapchainMutableFormatSupported && OverlayFormat != surfaceFormat.format)
	{
		SwapchainViewFormatList.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
		SwapchainViewFormatList.viewFormatCount = static_cast<uint32_t>(SwapchainViewFormats.size());
		SwapchainViewFormatList.pViewFormats = SwapchainViewFormats.data();
		swapchainCreateInfo.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
		swapchainCreateInfo.pNext = &SwapchainViewFormatList;
		m_SwapchainOverlayFormat = OverlayFormat;
	}

	// Create actual swapchain
	if (vkCreateSwapchainKHR(m_MainDevice.logicalDevice, &swapchainCreateInfo, nullptr, &m_Swapchain) != VK_SUCCESS)
	{
		LOGNTHROW("failed to create a swapchain");
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
		Utilities::Vulkan::SwapchainImage NewSwapchainImage{};
		NewSwapchainImage.m_Image = image;
		NewSwapchainImage.m_ImageView = createImageView(image, m_SwapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
		if (m_SwapchainOverlayFormat != m_SwapchainImageFormat)
		{
			NewSwapchainImage.m_OverlayImageView = createImageView(image, m_SwapchainOverlayFormat, VK_IMAGE_ASPECT_COLOR_BIT);
		}
		m_SwapchainImages.push_back(NewSwapchainImage);
	}
}

VkFormat Vulkanised::VulkanRenderer::getUnormFormatOfSrgbFormat(VkFormat SrgbFormat) noexcept
{
	switch (SrgbFormat)
	{
	case VK_FORMAT_B8G8R8A8_SRGB: return VK_FORMAT_B8G8R8A8_UNORM;
	case VK_FORMAT_R8G8B8A8_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
	default:					  return SrgbFormat;	// Not an sRGB format (or one we don't pair up): no separate overlay view
	}
}

void Vulkanised::VulkanRenderer::chooseDepthBufferFormat()
{
	// Plain 32-bit depth first: nothing uses stencil, and a depth+stencil format costs extra memory and bandwidth for nothing
	m_DepthBufferFormat = chooseSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkImageAspectFlags Vulkanised::VulkanRenderer::getDepthBufferAspectMask() const noexcept
{
	// Barriers on a depth+stencil image must cover both aspects
	const bool HasStencil = m_DepthBufferFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || m_DepthBufferFormat == VK_FORMAT_D24_UNORM_S8_UINT;
	return HasStencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_DEPTH_BIT;
}

void Vulkanised::VulkanRenderer::createDescriptorSetLayout()
{
	// ViewProjection Binding
	VkDescriptorSetLayoutBinding vpLayoutBinding = {};
	vpLayoutBinding.binding = 0;
	vpLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	vpLayoutBinding.descriptorCount = 1;
	vpLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	// SSBO Transform Binding
	VkDescriptorSetLayoutBinding ssboLayoutBinding = {};
	ssboLayoutBinding.binding = 1;
	ssboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	ssboLayoutBinding.descriptorCount = 1;
	ssboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	std::vector<VkDescriptorSetLayoutBinding> layoutBindings = { vpLayoutBinding, ssboLayoutBinding };

	VkDescriptorSetLayoutCreateInfo layoutCreateInfo{};
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
	layoutCreateInfo.pBindings = layoutBindings.data();

	if (vkCreateDescriptorSetLayout(m_MainDevice.logicalDevice, &layoutCreateInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
	{
		LOGNTHROW("failed to create descriptor set layout!");
	}

	// SET 1: EVERY TEXTURE, in one big array the shaders index into ("bindless"). A texture identifier IS its index here.
	// - Partially bound: slots no texture was written to yet are fine, as long as nothing samples them
	// - Update after bind: new textures can be written while frames in flight still use the set (loading during gameplay)
	VkPhysicalDeviceVulkan12Properties Vulkan12Properties{};
	Vulkan12Properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
	VkPhysicalDeviceProperties2 DeviceProperties{};
	DeviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	DeviceProperties.pNext = &Vulkan12Properties;
	vkGetPhysicalDeviceProperties2(m_MainDevice.physicalDevice, &DeviceProperties);
	// A combined image sampler counts as a sampled image AND a sampler
	m_BindlessTextureCapacity = std::min({ Utilities::Vulkan::c_MaxBindlessTextures,
		Vulkan12Properties.maxDescriptorSetUpdateAfterBindSampledImages, Vulkan12Properties.maxPerStageDescriptorUpdateAfterBindSampledImages,
		Vulkan12Properties.maxDescriptorSetUpdateAfterBindSamplers, Vulkan12Properties.maxPerStageDescriptorUpdateAfterBindSamplers });

	VkDescriptorSetLayoutBinding BindlessTexturesBinding{};
	BindlessTexturesBinding.binding = 0;
	BindlessTexturesBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	BindlessTexturesBinding.descriptorCount = m_BindlessTextureCapacity;
	BindlessTexturesBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	constexpr VkDescriptorBindingFlags c_BindlessTexturesBindingFlags{ VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT };
	VkDescriptorSetLayoutBindingFlagsCreateInfo BindingFlagsCreateInfo{};
	BindingFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	BindingFlagsCreateInfo.bindingCount = 1;
	BindingFlagsCreateInfo.pBindingFlags = &c_BindlessTexturesBindingFlags;

	VkDescriptorSetLayoutCreateInfo BindlessLayoutCreateInfo{};
	BindlessLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	BindlessLayoutCreateInfo.pNext = &BindingFlagsCreateInfo;
	BindlessLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	BindlessLayoutCreateInfo.bindingCount = 1;
	BindlessLayoutCreateInfo.pBindings = &BindlessTexturesBinding;

	if (vkCreateDescriptorSetLayout(m_MainDevice.logicalDevice, &BindlessLayoutCreateInfo, nullptr, &m_BindlessTextureSetLayout) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the bindless texture descriptor set layout");
	}
}

void Vulkanised::VulkanRenderer::createGraphicsPipeline()
{
	// Read in SPIR-V code of shaders
	auto vertexShaderCode = Utilities::File::readFile(Utilities::File::getRuntimeAssetsDirectory() / Utilities::File::c_ShadersFolderName / "shader.vert.spv");
	auto fragmentShaderCode = Utilities::File::readFile(Utilities::File::getRuntimeAssetsDirectory() / Utilities::File::c_ShadersFolderName / "shader.frag.spv");

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
	std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{ Vertex::getAttributeDescriptions() };

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
	std::array<VkDescriptorSetLayout, 2> descriptorSetLayouts = { m_DescriptorSetLayout, m_BindlessTextureSetLayout };
	// Which texture of the bindless array a draw samples (4 bytes, pushed per draw)
	const VkPushConstantRange TextureIndexPushConstantRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t) };

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &TextureIndexPushConstantRange;

	// Create pipeline layout
	if (vkCreatePipelineLayout(m_MainDevice.logicalDevice, &pipelineLayoutCreateInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
	{
		LOGNTHROW("failed to create pipeline layout!");
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
	// Dynamic rendering: instead of a render pass, the pipeline only needs to know the formats it will draw into
	VkPipelineRenderingCreateInfo PipelineRenderingCreateInfo{};
	PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_SwapchainImageFormat;
	PipelineRenderingCreateInfo.depthAttachmentFormat = m_DepthBufferFormat;
	graphicsPipelineCreateInfo.pNext = &PipelineRenderingCreateInfo;
	graphicsPipelineCreateInfo.renderPass = VK_NULL_HANDLE;

	// Pipeline derivatives : Can create multiple pipelines that derive from one another for optimisation
	graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;		// existing pipeline to derive from...
	graphicsPipelineCreateInfo.basePipelineIndex = -1;					// or index of pipeline being created to derive from (in case creating multiple at once)

	// Create graphics pipeline
	if (vkCreateGraphicsPipelines(m_MainDevice.logicalDevice, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS)
	{
		LOGNTHROW("failed to create pipeline(s)!");
	}

	// Destroy shader modules, no longer needed after pipeline created
	vkDestroyShaderModule(m_MainDevice.logicalDevice, fragmentShaderModule, nullptr);
	vkDestroyShaderModule(m_MainDevice.logicalDevice, vertexShaderModule, nullptr);
}

void Vulkanised::VulkanRenderer::createInfiniteGridPipeline()
{
	const std::vector<uint32_t> InfiniteGridVertexShaderCode = Utilities::File::readFile(Utilities::File::getRuntimeAssetsDirectory() / Utilities::File::c_ShadersFolderName / "infinite_grid.vert.spv");
	const std::vector<uint32_t> InfiniteGridFragmentShaderCode = Utilities::File::readFile(Utilities::File::getRuntimeAssetsDirectory() / Utilities::File::c_ShadersFolderName / "infinite_grid.frag.spv");

	VkShaderModule InfiniteGridVertexShaderModule = createShaderModule(InfiniteGridVertexShaderCode);
	VkShaderModule InfiniteGridFragmentShaderModule = createShaderModule(InfiniteGridFragmentShaderCode);

	std::array<VkPipelineShaderStageCreateInfo, 2> InfiniteGridShaderStages{};
	InfiniteGridShaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	InfiniteGridShaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	InfiniteGridShaderStages[0].module = InfiniteGridVertexShaderModule;
	InfiniteGridShaderStages[0].pName = "main";
	InfiniteGridShaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	InfiniteGridShaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	InfiniteGridShaderStages[1].module = InfiniteGridFragmentShaderModule;
	InfiniteGridShaderStages[1].pName = "main";

	// No vertex buffer at all: the vertex shader builds the quad from gl_VertexIndex
	VkPipelineVertexInputStateCreateInfo EmptyVertexInputCreateInfo{};
	EmptyVertexInputCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo InputAssemblyCreateInfo{};
	InputAssemblyCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	InputAssemblyCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Viewport and scissor are dynamic, exactly like the main pipeline
	const std::array<VkDynamicState, 2> DynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo DynamicStateCreateInfo{};
	DynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	DynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(DynamicStates.size());
	DynamicStateCreateInfo.pDynamicStates = DynamicStates.data();

	VkPipelineViewportStateCreateInfo ViewportStateCreateInfo{};
	ViewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	ViewportStateCreateInfo.viewportCount = 1;
	ViewportStateCreateInfo.scissorCount = 1;

	// No culling: the grid has to be visible from below too
	VkPipelineRasterizationStateCreateInfo RasterizerCreateInfo{};
	RasterizerCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	RasterizerCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	RasterizerCreateInfo.lineWidth = 1.0f;
	RasterizerCreateInfo.cullMode = VK_CULL_MODE_NONE;
	RasterizerCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineMultisampleStateCreateInfo MultisamplingCreateInfo{};
	MultisamplingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	MultisamplingCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Classic alpha blending, the grid lines are mostly transparent
	VkPipelineColorBlendAttachmentState AlphaBlendedColourAttachment{};
	AlphaBlendedColourAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	AlphaBlendedColourAttachment.blendEnable = VK_TRUE;
	AlphaBlendedColourAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	AlphaBlendedColourAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	AlphaBlendedColourAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	AlphaBlendedColourAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	AlphaBlendedColourAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	AlphaBlendedColourAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo ColourBlendingCreateInfo{};
	ColourBlendingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	ColourBlendingCreateInfo.attachmentCount = 1;
	ColourBlendingCreateInfo.pAttachments = &AlphaBlendedColourAttachment;

	// Tested against the depth buffer (meshes hide the grid) but never written to it (the grid never hides anything)
	VkPipelineDepthStencilStateCreateInfo DepthStencilCreateInfo{};
	DepthStencilCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	DepthStencilCreateInfo.depthTestEnable = VK_TRUE;
	DepthStencilCreateInfo.depthWriteEnable = VK_FALSE;
	DepthStencilCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS;

	VkGraphicsPipelineCreateInfo InfiniteGridPipelineCreateInfo{};
	InfiniteGridPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	InfiniteGridPipelineCreateInfo.stageCount = static_cast<uint32_t>(InfiniteGridShaderStages.size());
	InfiniteGridPipelineCreateInfo.pStages = InfiniteGridShaderStages.data();
	InfiniteGridPipelineCreateInfo.pVertexInputState = &EmptyVertexInputCreateInfo;
	InfiniteGridPipelineCreateInfo.pInputAssemblyState = &InputAssemblyCreateInfo;
	InfiniteGridPipelineCreateInfo.pViewportState = &ViewportStateCreateInfo;
	InfiniteGridPipelineCreateInfo.pDynamicState = &DynamicStateCreateInfo;
	InfiniteGridPipelineCreateInfo.pRasterizationState = &RasterizerCreateInfo;
	InfiniteGridPipelineCreateInfo.pMultisampleState = &MultisamplingCreateInfo;
	InfiniteGridPipelineCreateInfo.pColorBlendState = &ColourBlendingCreateInfo;
	InfiniteGridPipelineCreateInfo.pDepthStencilState = &DepthStencilCreateInfo;
	InfiniteGridPipelineCreateInfo.layout = m_PipelineLayout;	// Same layout as the main pipeline: the grid only uses set 0 (the camera UBO)
	VkPipelineRenderingCreateInfo PipelineRenderingCreateInfo{};	// Same attachments as the main pipeline (dynamic rendering)
	PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_SwapchainOverlayFormat;	// Drawn in the overlay pass, through the UNORM view
	PipelineRenderingCreateInfo.depthAttachmentFormat = m_DepthBufferFormat;
	InfiniteGridPipelineCreateInfo.pNext = &PipelineRenderingCreateInfo;
	InfiniteGridPipelineCreateInfo.renderPass = VK_NULL_HANDLE;
	InfiniteGridPipelineCreateInfo.basePipelineIndex = -1;

	const VkResult PipelineCreationResult = vkCreateGraphicsPipelines(m_MainDevice.logicalDevice, VK_NULL_HANDLE, 1, &InfiniteGridPipelineCreateInfo, nullptr, &m_InfiniteGridPipeline);

	vkDestroyShaderModule(m_MainDevice.logicalDevice, InfiniteGridFragmentShaderModule, nullptr);
	vkDestroyShaderModule(m_MainDevice.logicalDevice, InfiniteGridVertexShaderModule, nullptr);

	if (PipelineCreationResult != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the infinite grid pipeline!");
	}
}

void Vulkanised::VulkanRenderer::createDepthBufferImage()
{
	// Depth Buffer Image (and its view), the size of the swapchain, in the format chooseDepthBufferFormat picked
	VkImageCreateInfo DepthImageCreateInfo{};
	DepthImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	DepthImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	DepthImageCreateInfo.extent = { m_SwapchainExtent.width, m_SwapchainExtent.height, 1 };
	DepthImageCreateInfo.mipLevels = 1;
	DepthImageCreateInfo.arrayLayers = 1;
	DepthImageCreateInfo.format = m_DepthBufferFormat;
	DepthImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	DepthImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	DepthImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	DepthImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	DepthImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	m_DepthBuffer = m_GpuMemory.createImage(DepthImageCreateInfo, VK_IMAGE_ASPECT_DEPTH_BIT, "Depth buffer");
}

void Vulkanised::VulkanRenderer::createTextureSamplers()
{
	// Shared by every texture: each one picks one of these two when it's created (TextureOptions::m_Filtering)
	VkSamplerCreateInfo SamplerCreateInfo{};
	SamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	SamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	SamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	SamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	SamplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	SamplerCreateInfo.minLod = 0.0f;
	SamplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;	// Every mip level the texture has (this was 0, so mip 0 was the only one ever used)

	// - Smooth: trilinear (blends between mip levels too) plus anisotropic filtering for surfaces seen at grazing angles
	SamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	SamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	SamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	SamplerCreateInfo.anisotropyEnable = VK_TRUE;
	SamplerCreateInfo.maxAnisotropy = std::min(16.0f, m_PhysicalDeviceProperties.limits.maxSamplerAnisotropy);
	if (vkCreateSampler(m_MainDevice.logicalDevice, &SamplerCreateInfo, nullptr, &m_SmoothTextureSampler) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the smooth texture sampler");
	}

	// - Pixelated: nearest texel, nearest mip, no anisotropy: hard pixel edges up close, no shimmering far away
	SamplerCreateInfo.magFilter = VK_FILTER_NEAREST;
	SamplerCreateInfo.minFilter = VK_FILTER_NEAREST;
	SamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	SamplerCreateInfo.anisotropyEnable = VK_FALSE;
	SamplerCreateInfo.maxAnisotropy = 1.0f;
	if (vkCreateSampler(m_MainDevice.logicalDevice, &SamplerCreateInfo, nullptr, &m_PixelatedTextureSampler) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the pixelated texture sampler");
	}
}

void Vulkanised::VulkanRenderer::createDescriptorPool()
{
	// FRAME DESCRIPTOR POOL: set 0 (camera uniforms + instance data) of every frame in flight. Never reset, never grows.
	constexpr uint32_t c_FrameCount{ static_cast<uint32_t>(Utilities::Vulkan::c_MaxFrameDraws) };
	const std::array<VkDescriptorPoolSize, 2> FramePoolSizes{ {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, c_FrameCount },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, c_FrameCount } } };

	VkDescriptorPoolCreateInfo poolCreateInfo{};
	poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolCreateInfo.maxSets = c_FrameCount;
	poolCreateInfo.poolSizeCount = static_cast<uint32_t>(FramePoolSizes.size());
	poolCreateInfo.pPoolSizes = FramePoolSizes.data();

	if (vkCreateDescriptorPool(m_MainDevice.logicalDevice, &poolCreateInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
	{
		LOGNTHROW("failed to create the uniform descriptor pool");
	}

	// BINDLESS TEXTURE POOL: exactly one set (every texture), allocated once and only ever added to
	const VkDescriptorPoolSize BindlessPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_BindlessTextureCapacity };
	VkDescriptorPoolCreateInfo BindlessPoolCreateInfo{};
	BindlessPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	BindlessPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	BindlessPoolCreateInfo.maxSets = 1;
	BindlessPoolCreateInfo.poolSizeCount = 1;
	BindlessPoolCreateInfo.pPoolSizes = &BindlessPoolSize;
	if (vkCreateDescriptorPool(m_MainDevice.logicalDevice, &BindlessPoolCreateInfo, nullptr, &m_BindlessTexturePool) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to create the bindless texture descriptor pool");
	}

	VkDescriptorSetAllocateInfo BindlessSetAllocateInfo{};
	BindlessSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	BindlessSetAllocateInfo.descriptorPool = m_BindlessTexturePool;
	BindlessSetAllocateInfo.descriptorSetCount = 1;
	BindlessSetAllocateInfo.pSetLayouts = &m_BindlessTextureSetLayout;
	if (vkAllocateDescriptorSets(m_MainDevice.logicalDevice, &BindlessSetAllocateInfo, &m_BindlessTextureSet) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to allocate the bindless texture descriptor set");
	}
	LOG("Bindless textures: room for {} in one descriptor set.", m_BindlessTextureCapacity);
}

void Vulkanised::VulkanRenderer::cleanupSwapchain()
{
	// Only what's sized like the window lives and dies with the swapchain. Per-frame buffers, command pools and descriptor
	// sets used to be torn down and rebuilt here on every resize too (the upload command pool included).

	// 1. Swapchain image views (the images themselves belong to the swapchain)
	for (Utilities::Vulkan::SwapchainImage& CurrentSwapchainImage : m_SwapchainImages)
	{
		vkDestroyImageView(m_MainDevice.logicalDevice, CurrentSwapchainImage.m_ImageView, nullptr);
		vkDestroyImageView(m_MainDevice.logicalDevice, CurrentSwapchainImage.m_OverlayImageView, nullptr);	// Null (ignored) without a separate overlay view
	}
	m_SwapchainImages.clear();

	// 2. The swapchain
	if (m_Swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(m_MainDevice.logicalDevice, m_Swapchain, nullptr);
		m_Swapchain = VK_NULL_HANDLE;
	}

	// 3. Depth buffer (must match the swapchain size)
	m_GpuMemory.destroyImage(m_DepthBuffer);
}

void Vulkanised::VulkanRenderer::recreateSwapchain()
{
	// 1. Minimised: there's nothing to render into, so wait (handling events) until the window has a size again
	int FramebufferWidth{ 0 };
	int FramebufferHeight{ 0 };
	glfwGetFramebufferSize(m_Window.load().get(), &FramebufferWidth, &FramebufferHeight);
	while (FramebufferWidth == 0 || FramebufferHeight == 0)
	{
		glfwWaitEvents();
		glfwGetFramebufferSize(m_Window.load().get(), &FramebufferWidth, &FramebufferHeight);
	}

	// 2. Nothing may still be using the old images
	vkDeviceWaitIdle(m_MainDevice.logicalDevice);

	// 3. Rebuild what depends on the window size
	const size_t PreviousSwapchainImageCount = m_SwapchainImages.size();
	cleanupSwapchain();
	createSwapchain();
	createDepthBufferImage();

	// 4. One render-finished semaphore per swapchain image: only redone if the driver handed out a different number of images
	if (m_SwapchainImages.size() != PreviousSwapchainImageCount)
	{
		destroyRenderFinishedSemaphores();
		createRenderFinishedSemaphores();
	}
}

bool Vulkanised::VulkanRenderer::doesSwapchainNeedRecreation() const
{
	int FramebufferWidth{ 0 };
	int FramebufferHeight{ 0 };
	glfwGetFramebufferSize(m_Window.load().get(), &FramebufferWidth, &FramebufferHeight);
	return static_cast<uint32_t>(FramebufferWidth) != m_SwapchainExtent.width || static_cast<uint32_t>(FramebufferHeight) != m_SwapchainExtent.height;
}

void Vulkanised::VulkanRenderer::recordCommands(Rendering::FrameResources& FrameToRecord, uint32_t SwapchainImageIndex, const std::vector<RenderInstance>& instances)
{
	const VkCommandBuffer CommandBuffer = FrameToRecord.m_CommandBuffer;
	const VkImage SwapchainImage = m_SwapchainImages[SwapchainImageIndex].m_Image;
	const bool IsInfiniteGridDrawn = m_IsInfiniteGridVisible && m_InfiniteGridPipeline != VK_NULL_HANDLE;
	const bool IsOverlayPassNeeded = IsInfiniteGridDrawn || m_ShouldRenderImGuiThisFrame;

	// Re-recorded from scratch every frame, so the driver can optimise it for exactly one submit
	VkCommandBufferBeginInfo CommandBufferBeginInfo{};
	CommandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	CommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(CommandBuffer, &CommandBufferBeginInfo) != VK_SUCCESS)
	{
		LOGNTHROW("Failed to start recording a Command Buffer!");
	}

	// GPU timing: one timestamp before everything, one after everything (read back once this frame's fence signals)
	if (m_CanWriteTimestamps)
	{
		vkCmdResetQueryPool(CommandBuffer, FrameToRecord.m_TimestampQueryPool, 0, 2);
		vkCmdWriteTimestamp2(CommandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, FrameToRecord.m_TimestampQueryPool, 0);
	}

	// 1. ATTACHMENTS READY TO BE DRAWN INTO (with dynamic rendering, these transitions are ours to record, not the render pass's)
	// - Colour: whatever the presentation engine left in it is thrown away (UNDEFINED). The acquire semaphore is waited on at
	//   COLOR_ATTACHMENT_OUTPUT, so starting the transition after that same stage chains it behind the semaphore.
	Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
		.m_Image = SwapchainImage, .m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.m_OldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .m_NewLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .m_AccessesThatMustFinish = VK_ACCESS_2_NONE,
		.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .m_AccessesThatMustWait = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT });
	// - Depth: ONE depth buffer is shared by every frame in flight, so the previous frame's depth tests must be done with it
	//   before this frame clears it
	Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
		.m_Image = m_DepthBuffer.m_Image, .m_AspectMask = getDepthBufferAspectMask(),
		.m_OldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .m_NewLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		.m_AccessesThatMustFinish = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
		.m_AccessesThatMustWait = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT });

	// 2. BEGIN RENDERING straight into the swapchain image and the depth buffer (no render pass or framebuffer objects)
	VkRenderingAttachmentInfo ColourAttachment{};
	ColourAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	ColourAttachment.imageView = m_SwapchainImages[SwapchainImageIndex].m_ImageView;	// sRGB view: lighting results are linear
	ColourAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	ColourAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	ColourAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	// The clear colour is picked in sRGB: converted to linear, the sRGB view encodes it straight back to the exact picked value
	const glm::vec3 LinearClearColour = Rendering::convertSrgbColourToLinear(m_ClearColour);
	ColourAttachment.clearValue.color = { { LinearClearColour.r, LinearClearColour.g, LinearClearColour.b, 1.0f } };

	VkRenderingAttachmentInfo DepthAttachment{};
	DepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	DepthAttachment.imageView = m_DepthBuffer.m_ImageView;
	DepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	// The overlay pass (grid, ImGui) keeps testing against the scene's depth, otherwise nobody reads depth after the frame
	DepthAttachment.storeOp = IsOverlayPassNeeded ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
	DepthAttachment.clearValue.depthStencil = { 1.0f, 0 };

	VkRenderingInfo RenderingInfo{};
	RenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	RenderingInfo.renderArea = { { 0, 0 }, m_SwapchainExtent };
	RenderingInfo.layerCount = 1;
	RenderingInfo.colorAttachmentCount = 1;
	RenderingInfo.pColorAttachments = &ColourAttachment;
	RenderingInfo.pDepthAttachment = &DepthAttachment;
	vkCmdBeginRendering(CommandBuffer, &RenderingInfo);

	VkViewport Viewport{};
	Viewport.width = static_cast<float>(m_SwapchainExtent.width);
	Viewport.height = static_cast<float>(m_SwapchainExtent.height);
	Viewport.minDepth = 0.0f;
	Viewport.maxDepth = 1.0f;
	const VkRect2D Scissor{ { 0, 0 }, m_SwapchainExtent };
	vkCmdSetViewport(CommandBuffer, 0, 1, &Viewport);
	vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

	vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

	// BIND UNIFIED BUFFERS ONCE!
	VkBuffer vertexBuffers[] = { m_GlobalVertexBuffer.m_Buffer };
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(CommandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(CommandBuffer, m_GlobalIndexBuffer.m_Buffer, 0, VK_INDEX_TYPE_UINT32);

	// Both descriptor sets are bound ONCE: this frame's camera + instance data, and every texture there is (bindless)
	const std::array<VkDescriptorSet, 2> FrameDescriptorSets{ FrameToRecord.m_FrameDescriptorSet, m_BindlessTextureSet };
	vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, static_cast<uint32_t>(FrameDescriptorSets.size()), FrameDescriptorSets.data(), 0, nullptr);

	// INSTANCED RENDERING BATCH LOOP
	m_FrameStatistics.m_DrawCallCount = 0;
	size_t firstInstance = 0;
	while (firstInstance < instances.size())
	{
		uint32_t currentMesh = instances[firstInstance].meshID;
		uint32_t currentTex = instances[firstInstance].textureID;

		// Count how many consecutive identical instances we have
		uint32_t instanceCount = 0;
		while (firstInstance + instanceCount < instances.size() &&
			instances[firstInstance + instanceCount].meshID == currentMesh &&
			instances[firstInstance + instanceCount].textureID == currentTex)
		{
			instanceCount++;
		}

		// --- SAFETY GUARDS ---
		// If the ECS passed a garbage Mesh ID or an aborted asset, skip this entire batch!
		if (currentMesh >= m_Meshes.size())
		{
			LOGW("Invalid mesh ID {} requested for render. Skipping batch.", currentMesh);
			firstInstance += instanceCount;
			continue;
		}

		// Same for textures!
		if (currentTex >= m_TextureCount)
		{
			LOGW("Invalid texture ID {} requested for render. Skipping batch.", currentTex);
			firstInstance += instanceCount;
			continue;
		}
		// -----------------------------

		// The texture is picked by index inside the shader (bindless): no descriptor set to switch, just a push constant
		vkCmdPushConstants(CommandBuffer, m_PipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &currentTex);

		// Execute Instanced Draw!
		MeshAsset& asset = m_Meshes[currentMesh];
		vkCmdDrawIndexed(CommandBuffer, asset.indexCount, instanceCount, asset.firstIndex, asset.vertexOffset, static_cast<uint32_t>(firstInstance));
		++m_FrameStatistics.m_DrawCallCount;

		// Advance to next batch
		firstInstance += instanceCount;
	}

	vkCmdEndRendering(CommandBuffer);

	// 2b. OVERLAY: the grid and ImGui, drawn through the UNORM view of the same image (their colours are already sRGB, the
	//     sRGB view would encode them a second time). Depth is kept from the scene, so the grid still hides behind meshes.
	if (IsOverlayPassNeeded)
	{
		// The scene's colour and depth writes land before the overlay loads them
		VkMemoryBarrier2 SceneToOverlayBarrier{};
		SceneToOverlayBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		SceneToOverlayBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		SceneToOverlayBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		SceneToOverlayBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		SceneToOverlayBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
			| VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		VkDependencyInfo SceneToOverlayDependency{};
		SceneToOverlayDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		SceneToOverlayDependency.memoryBarrierCount = 1;
		SceneToOverlayDependency.pMemoryBarriers = &SceneToOverlayBarrier;
		vkCmdPipelineBarrier2(CommandBuffer, &SceneToOverlayDependency);

		const Utilities::Vulkan::SwapchainImage& CurrentSwapchainImage = m_SwapchainImages[SwapchainImageIndex];
		VkRenderingAttachmentInfo OverlayColourAttachment = ColourAttachment;
		OverlayColourAttachment.imageView = CurrentSwapchainImage.m_OverlayImageView != VK_NULL_HANDLE ? CurrentSwapchainImage.m_OverlayImageView : CurrentSwapchainImage.m_ImageView;
		OverlayColourAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		VkRenderingAttachmentInfo OverlayDepthAttachment = DepthAttachment;
		OverlayDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		OverlayDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;	// Nobody reads depth after this

		VkRenderingInfo OverlayRenderingInfo = RenderingInfo;
		OverlayRenderingInfo.pColorAttachments = &OverlayColourAttachment;
		OverlayRenderingInfo.pDepthAttachment = &OverlayDepthAttachment;
		vkCmdBeginRendering(CommandBuffer, &OverlayRenderingInfo);
		vkCmdSetViewport(CommandBuffer, 0, 1, &Viewport);
		vkCmdSetScissor(CommandBuffer, 0, 1, &Scissor);

		// INFINITE GRID: transparent, so it blends over whatever opaque geometry is already there
		if (IsInfiniteGridDrawn)
		{
			vkCmdBindPipeline(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_InfiniteGridPipeline);
			vkCmdBindDescriptorSets(CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &FrameToRecord.m_FrameDescriptorSet, 0, nullptr);
			vkCmdDraw(CommandBuffer, 6, 1, 0, 0);	// One quad, two triangles, all made up in the vertex shader
		}

		// DEAR IMGUI, very last so it sits on top of everything (it binds its own pipeline, viewport and scissor)
		if (m_ShouldRenderImGuiThisFrame)
		{
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), CommandBuffer);
		}

		vkCmdEndRendering(CommandBuffer);
	}

	// 3. HAND THE IMAGE TO THE PRESENTATION ENGINE (copying it out first if a screenshot was asked for)
	if (m_ShouldCaptureScreenshotThisFrame)
	{
		recordScreenshotCopy(CommandBuffer, SwapchainImage);	// Leaves it ready to present
	}
	else
	{
		// Presenting waits on the render-finished semaphore, signalled once ALL commands are done: nothing to wait for here
		Utilities::Vulkan::recordImageLayoutTransition(CommandBuffer, {
			.m_Image = SwapchainImage, .m_AspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.m_OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .m_NewLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.m_StagesThatMustFinish = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .m_AccessesThatMustFinish = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.m_StagesThatMustWait = VK_PIPELINE_STAGE_2_NONE, .m_AccessesThatMustWait = VK_ACCESS_2_NONE });
	}

	if (m_CanWriteTimestamps)
	{
		vkCmdWriteTimestamp2(CommandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, FrameToRecord.m_TimestampQueryPool, 1);
		FrameToRecord.m_HasTimestampQueriesToRead = true;
	}
	if (vkEndCommandBuffer(CommandBuffer) != VK_SUCCESS)
	{
		LOGNTHROW("failed to stop recording a command buffer");
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
		LOGNTHROW("Can't find GPU's that support Vulkan instance!");
	}

	// Get list of physical devices
	std::vector<VkPhysicalDevice> deviceList(deviceCount);
	vkEnumeratePhysicalDevices(m_Instance, &deviceCount, deviceList.data());

	// Best suitable device wins (it used to be the FIRST suitable one, so on a machine with an integrated GPU too,
	// whichever the driver happened to list first got picked)
	uint64_t BestDeviceScore{ 0 };
	m_MainDevice.physicalDevice = VK_NULL_HANDLE;
	for (const VkPhysicalDevice CurrentDevice : deviceList)
	{
		VkPhysicalDeviceProperties CurrentDeviceProperties{};
		vkGetPhysicalDeviceProperties(CurrentDevice, &CurrentDeviceProperties);
		if (!checkDeviceSuitable(CurrentDevice))
		{
			LOG("GPU '{}' doesn't have what Vulkanised needs (Vulkan 1.3, dynamic rendering, descriptor indexing...), skipping it.", CurrentDeviceProperties.deviceName);
			continue;
		}

		const uint64_t CurrentDeviceScore = scorePhysicalDevice(CurrentDevice);
		if (CurrentDeviceScore > BestDeviceScore)
		{
			BestDeviceScore = CurrentDeviceScore;
			m_MainDevice.physicalDevice = CurrentDevice;
		}
	}

	if (m_MainDevice.physicalDevice == VK_NULL_HANDLE)
	{
		LOGNTHROW("No GPU supports what Vulkanised needs (Vulkan 1.3 with dynamic rendering, synchronization2 and descriptor indexing).");
	}

	vkGetPhysicalDeviceProperties(m_MainDevice.physicalDevice, &m_PhysicalDeviceProperties);
	LOG("Rendering on '{}' (Vulkan {}.{}.{})", m_PhysicalDeviceProperties.deviceName, VK_API_VERSION_MAJOR(m_PhysicalDeviceProperties.apiVersion),
		VK_API_VERSION_MINOR(m_PhysicalDeviceProperties.apiVersion), VK_API_VERSION_PATCH(m_PhysicalDeviceProperties.apiVersion));
}

uint64_t Vulkanised::VulkanRenderer::scorePhysicalDevice(VkPhysicalDevice device)
{
	VkPhysicalDeviceProperties DeviceProperties{};
	vkGetPhysicalDeviceProperties(device, &DeviceProperties);

	// The device type decides first (a weak discrete GPU still beats a strong integrated one, as it has its own memory),
	// then the amount of GPU-local memory breaks ties
	uint64_t DeviceTypeScore{ 1 };
	switch (DeviceProperties.deviceType)
	{
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:	DeviceTypeScore = 4; break;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:	DeviceTypeScore = 3; break;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:	DeviceTypeScore = 2; break;
	default:									DeviceTypeScore = 1; break;	// CPU (software) renderers and anything else
	}

	VkPhysicalDeviceMemoryProperties MemoryProperties{};
	vkGetPhysicalDeviceMemoryProperties(device, &MemoryProperties);
	uint64_t DeviceLocalMemoryInMegabytes{ 0 };
	for (uint32_t CurrentHeapIndex{ 0 }; CurrentHeapIndex < MemoryProperties.memoryHeapCount; ++CurrentHeapIndex)
	{
		if ((MemoryProperties.memoryHeaps[CurrentHeapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
		{
			DeviceLocalMemoryInMegabytes += MemoryProperties.memoryHeaps[CurrentHeapIndex].size / (1024 * 1024);
		}
	}

	constexpr uint64_t c_MemoryScoreRange{ 1ull << 32 };	// Megabytes never get near this, so the device type always dominates
	return DeviceTypeScore * c_MemoryScoreRange + std::min(DeviceLocalMemoryInMegabytes, c_MemoryScoreRange - 1);
}

bool Vulkanised::VulkanRenderer::checkInstanceExtensionSupport(const std::vector<const char*> *checkExtensions)
{
	// Need to get number of extensions to create array of correct size to hold extensions
	uint32_t extensionCount{ 0 };
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

	// Create a list of vkExtensionProperties using count
	std::vector<VkExtensionProperties> extensions(extensionCount);
	vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

	// Layers bring extensions of their own (the validation layer provides VK_EXT_validation_features)
	if (Utilities::Vulkan::enableValidationLayers)
	{
		for (const char* CurrentLayerName : Utilities::Vulkan::validationLayers)
		{
			uint32_t LayerExtensionCount{ 0 };
			vkEnumerateInstanceExtensionProperties(CurrentLayerName, &LayerExtensionCount, nullptr);
			std::vector<VkExtensionProperties> LayerExtensions(LayerExtensionCount);
			vkEnumerateInstanceExtensionProperties(CurrentLayerName, &LayerExtensionCount, LayerExtensions.data());
			extensions.insert(extensions.end(), LayerExtensions.begin(), LayerExtensions.end());
		}
	}

	// Check if given extensions are in list of available extensions
	for (const auto &checkExtension : *checkExtensions)
	{
		bool hasExtension{ false };
		for(const auto &extension : extensions)
		{
			if (strcmp(checkExtension, extension.extensionName) == 0)	// strcmp returns 0 on a MATCH (this used to accept any mismatch)
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
	// Vulkan 1.3 at least: everything below is core there
	VkPhysicalDeviceProperties DeviceProperties{};
	vkGetPhysicalDeviceProperties(device, &DeviceProperties);
	if (DeviceProperties.apiVersion < VK_API_VERSION_1_3)
	{
		return false;
	}

	// Information about what the device can do: exactly the features createLogicalDevice turns on
	VkPhysicalDeviceVulkan13Features SupportedVulkan13Features{};
	SupportedVulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceVulkan12Features SupportedVulkan12Features{};
	SupportedVulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	SupportedVulkan12Features.pNext = &SupportedVulkan13Features;
	VkPhysicalDeviceFeatures2 SupportedFeatures{};
	SupportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	SupportedFeatures.pNext = &SupportedVulkan12Features;
	vkGetPhysicalDeviceFeatures2(device, &SupportedFeatures);

	const bool AreRequiredFeaturesSupported =
		SupportedFeatures.features.samplerAnisotropy && SupportedFeatures.features.multiDrawIndirect && SupportedFeatures.features.drawIndirectFirstInstance
		&& SupportedVulkan12Features.descriptorIndexing && SupportedVulkan12Features.runtimeDescriptorArray && SupportedVulkan12Features.descriptorBindingPartiallyBound
		&& SupportedVulkan12Features.descriptorBindingSampledImageUpdateAfterBind && SupportedVulkan12Features.descriptorBindingVariableDescriptorCount
		&& SupportedVulkan12Features.shaderSampledImageArrayNonUniformIndexing
		&& SupportedVulkan13Features.dynamicRendering && SupportedVulkan13Features.synchronization2 && SupportedVulkan13Features.shaderDemoteToHelperInvocation;
	if (!AreRequiredFeaturesSupported)
	{
		return false;
	}

	Utilities::Vulkan::QueueFamilyIndices indices = getQueueFamilies(device);

	bool areExtensionsSupported = checkDeviceExtensionSupport(device);

	bool isSwapchainValid{ false };
	if (areExtensionsSupported)
	{
		Utilities::Vulkan::SwapchainDetails swapchainDetails = getSwapchainDetails(device);
		isSwapchainValid = !swapchainDetails.presentationModes.empty() && !swapchainDetails.formats.empty();
	}

	return indices.isValid() && areExtensionsSupported && isSwapchainValid;
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
	// Warnings and errors only: verbose/info is mostly the loader narrating what it does (and slows every call down)
	createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
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

// An 8-bit sRGB format first: the GPU then encodes our LINEAR lighting results to sRGB on write, which is what the screen
// expects. UNORM (the old choice) stores the numbers as they are, so lighting happened in "gamma space": too dark and harsh.
VkSurfaceFormatKHR Vulkanised::VulkanRenderer::chooseBestSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
	// If only 1 format available AND is undefined, then this means ALL formats are available (no restrictions)
	if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
	{
		return { VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	}

	constexpr std::array<VkFormat, 4> c_FormatsInOrderOfPreference{ VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
	for (const VkFormat CurrentPreferredFormat : c_FormatsInOrderOfPreference)
	{
		for (const VkSurfaceFormatKHR& CurrentSurfaceFormat : formats)
		{
			if (CurrentSurfaceFormat.format == CurrentPreferredFormat && CurrentSurfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				if (getUnormFormatOfSrgbFormat(CurrentPreferredFormat) == CurrentPreferredFormat)
				{
					LOGW("No sRGB swapchain format available, lighting will be computed in gamma space (colours will look off).");
				}
				return CurrentSurfaceFormat;
			}
		}
	}

	// If can't find optimal format, then just return first format
	LOGW("None of the usual 8-bit swapchain formats is available, using VkFormat {}.", static_cast<int>(formats[0].format));
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
		LOGNTHROW("failed to create an image view");
	}
	return imageView;
}

VkShaderModule Vulkanised::VulkanRenderer::createShaderModule(const std::vector<uint32_t>& code) const
{
	// Shader module creation information
	VkShaderModuleCreateInfo shaderModuleCreateInfo{};
	shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderModuleCreateInfo.codeSize = code.size() * sizeof(uint32_t);										// Size of code
	//shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());		// Pointer to code (of uint32_t pointer type)
	shaderModuleCreateInfo.pCode = code.data();											// Because why the fuck are we going through the char step bruh
	
	VkShaderModule shaderModule;

	if (vkCreateShaderModule(m_MainDevice.logicalDevice, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		LOGNTHROW("failed to create a shader module");
	}

	return shaderModule;
}

int Vulkanised::VulkanRenderer::createTextureImage(std::string_view fileName, const Rendering::TextureOptions& Options)
{
	// Load image file
	int width, height;
	VkDeviceSize imageSize;
	stbi_uc* imageData = loadTextureFile(fileName, &width, &height, &imageSize);

	const int CreatedTextureImageIndex = createTextureImageFromRgbaPixels(imageData, static_cast<uint32_t>(width), static_cast<uint32_t>(height), Options);

	// Free original image data
	stbi_image_free(imageData);

	return CreatedTextureImageIndex;
}

int Vulkanised::VulkanRenderer::createTextureImageFromRgbaPixels(const unsigned char* RgbaPixels, uint32_t WidthInPixels, uint32_t HeightInPixels, const Rendering::TextureOptions& Options)
{
	const VkDeviceSize imageSize{ static_cast<VkDeviceSize>(WidthInPixels) * HeightInPixels * 4 };

	// Colour textures are stored as sRGB: sampling them returns LINEAR values, which is what the lighting maths needs
	const VkFormat TextureFormat = Options.m_ColourSpace == Rendering::TextureColourSpace::Colour ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

	// A full mip chain (each level half the size of the previous, down to 1x1), built on the GPU by the uploader. Needs the
	// format to support linearly filtered blits, which every desktop GPU does for these two.
	uint32_t MipLevelCount{ 1 };
	if (Options.m_ShouldGenerateMipmaps && canGenerateMipmapsForFormat(TextureFormat))
	{
		MipLevelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(WidthInPixels, HeightInPixels)))) + 1;
	}

	// Create image to hold final texture (and the view the shaders read it through)
	VkImageCreateInfo TextureImageCreateInfo{};
	TextureImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	TextureImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	TextureImageCreateInfo.extent = { WidthInPixels, HeightInPixels, 1 };
	TextureImageCreateInfo.mipLevels = MipLevelCount;
	TextureImageCreateInfo.arrayLayers = 1;
	TextureImageCreateInfo.format = TextureFormat;
	TextureImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	TextureImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	TextureImageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| (MipLevelCount > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);	// Each mip level is blitted from the one above it
	TextureImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	TextureImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	Rendering::AllocatedImage TextureImage = m_GpuMemory.createImage(TextureImageCreateInfo, VK_IMAGE_ASPECT_COLOR_BIT, "Texture");

	// The pixels are copied into staging memory right now (the caller may free them), and reach the image, mips and all,
	// ready to be sampled, with the next upload batch (before the next frame)
	m_ResourceUploader.queueTextureUpload(TextureImage, RgbaPixels, imageSize);

	// Add texture data to vector for reference
	m_TextureImages.push_back(TextureImage);

	// Return index of new texture image
	return static_cast<int>(m_TextureImages.size() - 1);
}

bool Vulkanised::VulkanRenderer::canGenerateMipmapsForFormat(VkFormat TextureFormat) const
{
	constexpr VkFormatFeatureFlags c_RequiredFeatures{ VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT };
	VkFormatProperties FormatProperties{};
	vkGetPhysicalDeviceFormatProperties(m_MainDevice.physicalDevice, TextureFormat, &FormatProperties);
	return (FormatProperties.optimalTilingFeatures & c_RequiredFeatures) == c_RequiredFeatures;
}

int Vulkanised::VulkanRenderer::createTexture(std::string_view fileName, const Rendering::TextureOptions& Options)
{
	// Create Texture Image and get its location in array
	return createTextureViewAndDescriptorForImage(createTextureImage(fileName, Options), Options);
}

int Vulkanised::VulkanRenderer::createTextureFromRgbaPixels(const unsigned char* RgbaPixels, uint32_t WidthInPixels, uint32_t HeightInPixels, const Rendering::TextureOptions& Options)
{
	if (RgbaPixels == nullptr || WidthInPixels == 0 || HeightInPixels == 0)
	{
		LOGW("Tried to create a texture from no pixels, using the default white texture instead.");
		return static_cast<int>(getDefaultWhiteTextureIdentifier());
	}
	return createTextureViewAndDescriptorForImage(createTextureImageFromRgbaPixels(RgbaPixels, WidthInPixels, HeightInPixels, Options), Options);
}

int Vulkanised::VulkanRenderer::createTextureFromEncodedImageInMemory(const unsigned char* EncodedImageBytes, size_t EncodedImageByteCount, const Rendering::TextureOptions& Options)
{
	// stb_image takes an int for the size, so anything above 2GB can't be decoded in one go (and shouldn't be a texture anyway)
	if (EncodedImageBytes == nullptr || EncodedImageByteCount == 0 || EncodedImageByteCount > static_cast<size_t>(std::numeric_limits<int>::max()))
	{
		LOGW("Tried to decode an empty (or absurdly big) in-memory image, using the default white texture instead.");
		return static_cast<int>(getDefaultWhiteTextureIdentifier());
	}

	int DecodedWidthInPixels{ 0 };
	int DecodedHeightInPixels{ 0 };
	int ChannelCountInSourceImage{ 0 };
	stbi_uc* DecodedRgbaPixels = stbi_load_from_memory(EncodedImageBytes, static_cast<int>(EncodedImageByteCount), &DecodedWidthInPixels, &DecodedHeightInPixels, &ChannelCountInSourceImage, STBI_rgb_alpha);
	if (DecodedRgbaPixels == nullptr)
	{
		LOGW("Failed to decode an in-memory image ({}), using the default white texture instead.", stbi_failure_reason());
		return static_cast<int>(getDefaultWhiteTextureIdentifier());
	}

	const int CreatedTextureIdentifier = createTextureFromRgbaPixels(DecodedRgbaPixels, static_cast<uint32_t>(DecodedWidthInPixels), static_cast<uint32_t>(DecodedHeightInPixels), Options);
	stbi_image_free(DecodedRgbaPixels);
	return CreatedTextureIdentifier;
}

uint32_t Vulkanised::VulkanRenderer::getDefaultWhiteTextureIdentifier()
{
	// Created on first use, so code that never needs it doesn't see its texture identifiers shift by one
	if (!m_DefaultWhiteTextureIdentifier.has_value())
	{
		constexpr std::array<unsigned char, 4> c_SingleWhitePixel{ 255, 255, 255, 255 };
		m_DefaultWhiteTextureIdentifier = static_cast<uint32_t>(createTextureFromRgbaPixels(c_SingleWhitePixel.data(), 1, 1));
	}
	return m_DefaultWhiteTextureIdentifier.value();
}

int Vulkanised::VulkanRenderer::createTextureViewAndDescriptorForImage(int TextureImageIndex, const Rendering::TextureOptions& Options)
{
	// The image already has its view (the allocator made it): create the texture descriptor, and return location of set with texture
	const VkSampler ChosenSampler = Options.m_Filtering == Rendering::TextureFiltering::Pixelated ? m_PixelatedTextureSampler : m_SmoothTextureSampler;
	return createTextureDescriptor(m_TextureImages[TextureImageIndex].m_ImageView, ChosenSampler);
}

int Vulkanised::VulkanRenderer::createTextureDescriptor(VkImageView textureImage, VkSampler TextureSampler)
{
	// The texture's identifier is simply the next free slot of the bindless array
	if (m_TextureCount >= m_BindlessTextureCapacity)
	{
		LOGNTHROW("Out of texture slots ({} max). Raise Utilities::Vulkan::c_MaxBindlessTextures if this is legit.", m_BindlessTextureCapacity);
	}

	VkDescriptorImageInfo TextureImageInfo{};
	TextureImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	TextureImageInfo.imageView = textureImage;
	TextureImageInfo.sampler = TextureSampler;

	VkWriteDescriptorSet TextureDescriptorWrite{};
	TextureDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	TextureDescriptorWrite.dstSet = m_BindlessTextureSet;
	TextureDescriptorWrite.dstBinding = 0;
	TextureDescriptorWrite.dstArrayElement = m_TextureCount;
	TextureDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	TextureDescriptorWrite.descriptorCount = 1;
	TextureDescriptorWrite.pImageInfo = &TextureImageInfo;

	// Allowed while frames in flight use the set (update-after-bind): those frames never sample this new slot
	vkUpdateDescriptorSets(m_MainDevice.logicalDevice, 1, &TextureDescriptorWrite, 0, nullptr);

	return static_cast<int>(m_TextureCount++);
}

stbi_uc* Vulkanised::VulkanRenderer::loadTextureFile(std::string_view fileName, int* width, int* height, VkDeviceSize* imageSize)
{
	// Number of channels image uses
	int channels;

	// Load pixel data for image
	// Relative names ("bibi.jpg") live in assets/Textures. Absolute paths are used as-is, which is what model importers
	// will need, since a model's textures usually sit next to the model file rather than in the Textures folder.
	const std::filesystem::path RequestedTexturePath{ fileName };
	std::filesystem::path fileLoc{ RequestedTexturePath.is_absolute() ? RequestedTexturePath : Utilities::File::getRuntimeAssetsDirectory() / Utilities::File::c_TexturesFolderName / RequestedTexturePath };
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
