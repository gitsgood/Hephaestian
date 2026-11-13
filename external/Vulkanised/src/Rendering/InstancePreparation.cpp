#include "Vulkanised/VulkanRenderer.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "Vulkanised/Rendering/FrustumCulling.h"
#include "Vulkanised/Rendering/GpuInstanceData.h"

/*
* Turns what the application submitted into what the GPU draws, every frame:
*	1. drop instances outside the camera's frustum (and invalid ones)
*	2. group the survivors by pipeline (opaque, then alpha cutout, then additive) and by mesh, with a counting sort (one pass to count,
*	   one to place, no comparisons)
*	3. write each one's GpuInstanceData straight into this frame's mapped instance buffer, grouped like that
*	4. write one VkDrawIndexedIndirectCommand per (pipeline, mesh) group that has visible instances
* The scene is then one vkCmdDrawIndexedIndirect per pipeline. Textures no longer split batches (they're bindless and picked
* per instance), so a mesh is drawn once however many textures its instances use.
*/

Vulkanised::VulkanRenderer::FrameDrawCommandCounts Vulkanised::VulkanRenderer::prepareFrameInstances(Rendering::FrameResources& FrameToFill, const std::vector<RenderInstance>& SubmittedInstances)
{
	// Groups: one per (pipeline, mesh), pipeline-major in drawing order. [0, MeshCount) are the opaque pipeline's meshes,
	// [MeshCount, 2 * MeshCount) the alpha cutout pipeline's, [2 * MeshCount, 3 * MeshCount) the additive pipeline's.
	constexpr size_t c_PipelineCount{ 3 };
	const size_t MeshCount = m_Meshes.size();
	const size_t GroupCount = MeshCount * c_PipelineCount;
	auto getGroupOfInstance = [MeshCount](const RenderInstance& Instance) -> size_t
	{
		size_t PipelineIndex{ 0 };
		if ((Instance.m_Flags & RenderInstanceFlags::c_Additive) != 0)
		{
			PipelineIndex = 2;
		}
		else if ((Instance.m_Flags & RenderInstanceFlags::c_AlphaCutout) != 0)
		{
			PipelineIndex = 1;
		}
		return PipelineIndex * MeshCount + Instance.meshID;
	};

	m_VisibleInstanceCountPerMesh.assign(GroupCount, 0);
	m_VisibleInstanceIndices.clear();
	m_VisibleInstanceIndices.reserve(SubmittedInstances.size());

	// The frustum to test against: the camera's, or the one captured when culling was frozen (fly out and look at the result)
	const glm::mat4 CurrentViewProjection = m_UboViewProjection.projection * m_UboViewProjection.view;
	if (!m_IsFrustumCullingFrozen)
	{
		m_CullingViewProjection = CurrentViewProjection;
	}
	const Rendering::FrustumPlanes CullingFrustum = Rendering::extractFrustumPlanes(m_CullingViewProjection);

	// 1. WHAT'S VISIBLE (and valid), counted per group
	uint32_t CulledInstanceCount{ 0 };
	uint32_t InvalidInstanceCount{ 0 };
	for (size_t CurrentInstanceIndex{ 0 }; CurrentInstanceIndex < SubmittedInstances.size(); ++CurrentInstanceIndex)
	{
		const RenderInstance& CurrentInstance = SubmittedInstances[CurrentInstanceIndex];
		if (CurrentInstance.meshID >= MeshCount || CurrentInstance.textureID >= m_TextureCount)
		{
			++InvalidInstanceCount;	// A garbage or failed asset: skipped, reported below
			continue;
		}

		if (m_IsFrustumCullingEnabled)
		{
			// The pivot offset moves the mesh away from its own centre (in mesh units), so the sphere grows by that much.
			// Billboards ignore the instance's rotation, which a sphere doesn't care about anyway.
			const MeshAsset& InstanceMesh = m_Meshes[CurrentInstance.meshID];
			const glm::vec3 WorldCentre = glm::vec3(CurrentInstance.transform * glm::vec4(InstanceMesh.m_BoundingSphereCentre, 1.0f));
			const float LocalRadius = InstanceMesh.m_BoundingSphereRadius + glm::length(CurrentInstance.m_PivotOffset);
			const float WorldRadius = LocalRadius * Rendering::getLargestScaleOfTransform(CurrentInstance.transform);
			if (!Rendering::isSphereInsideFrustum(CullingFrustum, WorldCentre, WorldRadius))
			{
				++CulledInstanceCount;
				continue;
			}
		}

		m_VisibleInstanceIndices.push_back(static_cast<uint32_t>(CurrentInstanceIndex));
		++m_VisibleInstanceCountPerMesh[getGroupOfInstance(CurrentInstance)];
	}

	if (InvalidInstanceCount > 0 && !m_HasWarnedAboutInvalidRenderInstances)
	{
		LOGW("{} render instances reference a mesh or texture that doesn't exist: they're skipped (warned once).", InvalidInstanceCount);
		m_HasWarnedAboutInvalidRenderInstances = true;
	}

	// 2. ONE INDIRECT DRAW PER GROUP: where each group's instances start (prefix sums), and the draw itself
	uint32_t IndirectCommandCount{ 0 };
	for (const uint32_t CurrentGroupInstanceCount : m_VisibleInstanceCountPerMesh)
	{
		IndirectCommandCount += CurrentGroupInstanceCount > 0 ? 1 : 0;
	}
	const uint32_t VisibleInstanceCount = static_cast<uint32_t>(m_VisibleInstanceIndices.size());
	ensureFrameBufferCapacity(FrameToFill, VisibleInstanceCount, IndirectCommandCount);

	auto* IndirectCommands = static_cast<VkDrawIndexedIndirectCommand*>(FrameToFill.m_IndirectCommandBuffer.m_MappedData);
	m_InstanceWriteCursorPerMesh.resize(GroupCount);
	FrameDrawCommandCounts CommandCounts{};
	uint32_t FirstInstanceOfCurrentGroup{ 0 };
	uint32_t WrittenCommandCount{ 0 };
	for (size_t CurrentGroupIndex{ 0 }; CurrentGroupIndex < GroupCount; ++CurrentGroupIndex)
	{
		const uint32_t GroupInstanceCount = m_VisibleInstanceCountPerMesh[CurrentGroupIndex];
		m_InstanceWriteCursorPerMesh[CurrentGroupIndex] = FirstInstanceOfCurrentGroup;
		if (GroupInstanceCount == 0)
		{
			continue;
		}

		const MeshAsset& GroupMesh = m_Meshes[CurrentGroupIndex % MeshCount];
		VkDrawIndexedIndirectCommand& CurrentCommand = IndirectCommands[WrittenCommandCount++];
		CurrentCommand.indexCount = GroupMesh.indexCount;
		CurrentCommand.instanceCount = GroupInstanceCount;
		CurrentCommand.firstIndex = GroupMesh.firstIndex;
		CurrentCommand.vertexOffset = GroupMesh.vertexOffset;
		CurrentCommand.firstInstance = FirstInstanceOfCurrentGroup;	// gl_InstanceIndex starts here: the shader reads the right slots
		FirstInstanceOfCurrentGroup += GroupInstanceCount;

		const size_t GroupPipelineIndex = CurrentGroupIndex / MeshCount;
		uint32_t& PipelineCommandCount = GroupPipelineIndex == 0 ? CommandCounts.m_OpaqueCommandCount
			: GroupPipelineIndex == 1 ? CommandCounts.m_AlphaCutoutCommandCount : CommandCounts.m_AdditiveCommandCount;
		++PipelineCommandCount;
	}

	// 3. EVERY VISIBLE INSTANCE INTO ITS GROUP'S RANGE (written straight into mapped GPU memory, no intermediate copy)
	auto* GpuInstances = static_cast<Rendering::GpuInstanceData*>(FrameToFill.m_InstanceBuffer.m_MappedData);
	for (const uint32_t CurrentVisibleInstanceIndex : m_VisibleInstanceIndices)
	{
		const RenderInstance& SourceInstance = SubmittedInstances[CurrentVisibleInstanceIndex];
		Rendering::GpuInstanceData& DestinationInstance = GpuInstances[m_InstanceWriteCursorPerMesh[getGroupOfInstance(SourceInstance)]++];
		DestinationInstance.m_ModelMatrix = SourceInstance.transform;
		DestinationInstance.m_TextureCoordinateRect = SourceInstance.m_TextureCoordinateRect;
		DestinationInstance.m_Tint = SourceInstance.m_Tint;
		DestinationInstance.m_TextureIndex = SourceInstance.textureID;
		DestinationInstance.m_Flags = SourceInstance.m_Flags;
		DestinationInstance.m_PivotOffset = SourceInstance.m_PivotOffset;
	}

	m_GpuMemory.flushBufferWrites(FrameToFill.m_InstanceBuffer, 0, sizeof(Rendering::GpuInstanceData) * std::max<VkDeviceSize>(VisibleInstanceCount, 1));
	m_GpuMemory.flushBufferWrites(FrameToFill.m_IndirectCommandBuffer, 0, sizeof(VkDrawIndexedIndirectCommand) * std::max<VkDeviceSize>(IndirectCommandCount, 1));

	m_FrameStatistics.m_SubmittedInstanceCount = static_cast<uint32_t>(SubmittedInstances.size());
	m_FrameStatistics.m_CulledInstanceCount = CulledInstanceCount;
	m_FrameStatistics.m_DrawnInstanceCount = VisibleInstanceCount;
	m_FrameStatistics.m_DrawCallCount = IndirectCommandCount;
	return CommandCounts;
}

void Vulkanised::VulkanRenderer::ensureFrameBufferCapacity(Rendering::FrameResources& FrameToGrow, uint32_t RequiredInstanceCount, uint32_t RequiredIndirectCommandCount)
{
	// Called after this frame's fence was waited on: nothing on the GPU uses these buffers, so they can simply be replaced
	const std::string FrameName = "(frame " + std::to_string(&FrameToGrow - m_Frames.data()) + ")";

	if (RequiredInstanceCount > FrameToGrow.m_InstanceCapacity || !FrameToGrow.m_InstanceBuffer.isValid())
	{
		uint32_t NewInstanceCapacity = std::max<uint32_t>(FrameToGrow.m_InstanceCapacity, c_InitialInstanceCapacity);
		while (NewInstanceCapacity < RequiredInstanceCount)
		{
			NewInstanceCapacity *= 2;
		}
		m_GpuMemory.destroyBuffer(FrameToGrow.m_InstanceBuffer);
		FrameToGrow.m_InstanceBuffer = m_GpuMemory.createBuffer(sizeof(Rendering::GpuInstanceData) * NewInstanceCapacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			Rendering::GpuMemoryUsage::CpuWritesGpuReads, "Instance data " + FrameName);
		FrameToGrow.m_InstanceCapacity = NewInstanceCapacity;

		// This frame's set 0 points at the instance buffer: point it at the new one (the set isn't in use either).
		// On the very first creation the set doesn't exist yet: createFrameResources writes it once it does.
		if (FrameToGrow.m_FrameDescriptorSet != VK_NULL_HANDLE)
		{
			const VkDescriptorBufferInfo InstanceBufferInfo{ FrameToGrow.m_InstanceBuffer.m_Buffer, 0, VK_WHOLE_SIZE };
			VkWriteDescriptorSet InstanceBufferWrite{};
			InstanceBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			InstanceBufferWrite.dstSet = FrameToGrow.m_FrameDescriptorSet;
			InstanceBufferWrite.dstBinding = 1;
			InstanceBufferWrite.descriptorCount = 1;
			InstanceBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			InstanceBufferWrite.pBufferInfo = &InstanceBufferInfo;
			vkUpdateDescriptorSets(m_MainDevice.logicalDevice, 1, &InstanceBufferWrite, 0, nullptr);
		}
	}

	if (RequiredIndirectCommandCount > FrameToGrow.m_IndirectCommandCapacity || !FrameToGrow.m_IndirectCommandBuffer.isValid())
	{
		uint32_t NewCommandCapacity = std::max<uint32_t>(FrameToGrow.m_IndirectCommandCapacity, c_InitialIndirectCommandCapacity);
		while (NewCommandCapacity < RequiredIndirectCommandCount)
		{
			NewCommandCapacity *= 2;
		}
		m_GpuMemory.destroyBuffer(FrameToGrow.m_IndirectCommandBuffer);
		FrameToGrow.m_IndirectCommandBuffer = m_GpuMemory.createBuffer(sizeof(VkDrawIndexedIndirectCommand) * NewCommandCapacity, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
			Rendering::GpuMemoryUsage::CpuWritesGpuReads, "Indirect draws " + FrameName);
		FrameToGrow.m_IndirectCommandCapacity = NewCommandCapacity;
	}
}
