#ifndef VULKANUTILS_H
#define VULKANUTILS_H

#include <vector>

#include <vulkan/vulkan.h>

#include "Vulkanised/Logger.h"

namespace Vulkanised::Utilities::Vulkan
{

    inline constexpr int c_MaxFrameDraws{ 2 };
    inline constexpr uint32_t c_MaxBindlessTextures{ 16384 };   // Size of the one texture array every shader indexes into (clamped to what the GPU allows)

    // using inline so header-only definition doesn't produce duplicate symbols when included from multiple translation units (TUs).
    inline const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };

    #ifdef NDEBUG
    inline bool enableValidationLayers = false;
    #else
    inline bool enableValidationLayers = true;
    #endif

    // Make these inline to avoid multiple-definition linker errors when included from multiple TUs.
    inline VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) 
    {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        }
        else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    inline void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) 
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    inline const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    // Indices (locations) of queue families (if they exist at all)
    struct QueueFamilyIndices
    {
        int graphicsFamily{ -1 };	    // Location of the graphics queue family
        int presentationFamily{ -1 };   // Location of presentation queue family

        // Check if queue families are valid
        [[nodiscard]] bool isValid() const noexcept
        {
            return graphicsFamily >= 0 && presentationFamily >= 0;
        }
    };

    struct SwapchainDetails
    {
        VkSurfaceCapabilitiesKHR surfaceCapabilities;       // Surface properties, e.g. image, size/extent
        std::vector<VkSurfaceFormatKHR> formats;            // Surface image formats, e.g. RBGA and size of each color
        std::vector<VkPresentModeKHR> presentationModes;    // How images should be presented to screen
    };

    struct SwapchainImage
    {
        VkImage m_Image{ VK_NULL_HANDLE };
        VkImageView m_ImageView{ VK_NULL_HANDLE };          // In the swapchain format (sRGB): the scene is drawn through this one
        VkImageView m_OverlayImageView{ VK_NULL_HANDLE };   // Same image as UNORM, for already-sRGB colours (grid, ImGui). Null if not supported.
    };

    // Everything one image layout transition (synchronization2) needs to know
    struct ImageLayoutTransition
    {
        VkImage m_Image{ VK_NULL_HANDLE };
        VkImageAspectFlags m_AspectMask{ VK_IMAGE_ASPECT_COLOR_BIT };
        VkImageLayout m_OldLayout{ VK_IMAGE_LAYOUT_UNDEFINED };    // UNDEFINED = don't care about what's in it now (its contents may be thrown away)
        VkImageLayout m_NewLayout{ VK_IMAGE_LAYOUT_UNDEFINED };
        VkPipelineStageFlags2 m_StagesThatMustFinish{ VK_PIPELINE_STAGE_2_NONE };   // ...with these accesses made visible...
        VkAccessFlags2 m_AccessesThatMustFinish{ VK_ACCESS_2_NONE };
        VkPipelineStageFlags2 m_StagesThatMustWait{ VK_PIPELINE_STAGE_2_NONE };     // ...before these stages may do these accesses
        VkAccessFlags2 m_AccessesThatMustWait{ VK_ACCESS_2_NONE };
        uint32_t m_FirstMipLevel{ 0 };
        uint32_t m_MipLevelCount{ VK_REMAINING_MIP_LEVELS };
    };

    // Records one image barrier (vkCmdPipelineBarrier2). Several in one call would be cheaper, but one is what's needed everywhere so far.
    inline void recordImageLayoutTransition(VkCommandBuffer CommandBuffer, const ImageLayoutTransition& Transition)
    {
        VkImageMemoryBarrier2 ImageBarrier{};
        ImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        ImageBarrier.srcStageMask = Transition.m_StagesThatMustFinish;
        ImageBarrier.srcAccessMask = Transition.m_AccessesThatMustFinish;
        ImageBarrier.dstStageMask = Transition.m_StagesThatMustWait;
        ImageBarrier.dstAccessMask = Transition.m_AccessesThatMustWait;
        ImageBarrier.oldLayout = Transition.m_OldLayout;
        ImageBarrier.newLayout = Transition.m_NewLayout;
        ImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ImageBarrier.image = Transition.m_Image;
        ImageBarrier.subresourceRange = { Transition.m_AspectMask, Transition.m_FirstMipLevel, Transition.m_MipLevelCount, 0, VK_REMAINING_ARRAY_LAYERS };

        VkDependencyInfo DependencyInfo{};
        DependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        DependencyInfo.imageMemoryBarrierCount = 1;
        DependencyInfo.pImageMemoryBarriers = &ImageBarrier;
        vkCmdPipelineBarrier2(CommandBuffer, &DependencyInfo);
    }

}

#endif // !VULKANUTILS_H
