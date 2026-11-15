/**
 * @file pch.h
 * @brief The precompiled header: standard and third party headers that nearly every file includes and rarely change.
 */
#ifndef PCH_H
#define PCH_H

// Standard library
#include <stdexcept>
#include <random>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

// Third party
// - EnTT is a ~3.5MB single header, so it REALLY wants to be precompiled
#include <entt/entt.hpp>
// - The hash maps (a ~5000 line header, used all over)
#include <ankerl/unordered_dense.h>
// - GLFW + GLM
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#endif // !PCH_H
