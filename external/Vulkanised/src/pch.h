#ifndef PCH_HPP
#define PCH_HPP

// STANDARD LIBRARY
// - Logger
#include <iostream>
#include <source_location>
#include <memory>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <print>            // Also includes format somewhere in there
#include <chrono>
#include <queue>
#include <condition_variable>
#include <thread>
// - Mesh
#include <vector>
#include <stdexcept>
// - Vertex
#include <array>
// - Renderer
#include <iostream>
#include <stdexcept>
#include <vector>
#include <memory>
#include <atomic>
#include <set>
#include <algorithm>
// - utils/VulkanUtils
#include <vector>
// - utils/TimeUtils
#include <chrono>
#include <type_traits>
#include <functional>
#include <string>
#include <print>
#include <utility>
// - utils/FileUtils
#include <fstream>
#include <filesystem>
// - utils/CrossPlatUtils

// - main
#include <stdio.h>
#include <stdexcept>
#include <vector>
#include <memory>
#include <atomic>
#include <string_view>


// THIRD PARTY
// - Logger
#include "glaze/glaze.hpp"
// - Vertex
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>
// - Mesh
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
// - Renderer
#include <stb_image.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
// - utils/VulkanUtils
#include <vulkan/vulkan.h>
// - main
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>


// HOMEMADE
// - Logger

// - Vertex

// - main



#endif