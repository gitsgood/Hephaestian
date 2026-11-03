#ifndef HEPHAESTIAN_INPUTMANAGER_H
#define HEPHAESTIAN_INPUTMANAGER_H

#include <array>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace Hephaestian
{

/*
* Collects everything GLFW tells us about the keyboard and mouse, and turns it into per-frame state that systems can query.
* Usage, once per frame:
*	1. InputManager.beginNewFrame();	// Forget last frame's "pressed"/"released" events and mouse deltas
*	2. glfwPollEvents();				// GLFW fires our callbacks, which fill in this frame's state
*	3. Systems query isKeyHeld / wasKeyPressedThisFrame / getMouseMovementDeltaThisFrame...
*
* Key and button codes are just GLFW's (GLFW_KEY_W, GLFW_MOUSE_BUTTON_LEFT...), no need to reinvent those.
*/
class InputManager
{
public:
	InputManager() = default;
	~InputManager() noexcept;

	// Hooks the GLFW callbacks of this window up to this instance. Only one InputManager per window.
	void attachToWindow(GLFWwindow* WindowToReceiveInputFrom);
	void detachFromWindow() noexcept;

	// Call right BEFORE glfwPollEvents
	void beginNewFrame() noexcept;

	// - Keyboard
	[[nodiscard]] bool isKeyHeld(int GlfwKeyCode) const noexcept;
	[[nodiscard]] bool wasKeyPressedThisFrame(int GlfwKeyCode) const noexcept;
	[[nodiscard]] bool wasKeyReleasedThisFrame(int GlfwKeyCode) const noexcept;

	// - Mouse buttons
	[[nodiscard]] bool isMouseButtonHeld(int GlfwMouseButtonCode) const noexcept;
	[[nodiscard]] bool wasMouseButtonPressedThisFrame(int GlfwMouseButtonCode) const noexcept;
	[[nodiscard]] bool wasMouseButtonReleasedThisFrame(int GlfwMouseButtonCode) const noexcept;

	// - Mouse movement (in screen pixels, +X right, +Y down, like GLFW)
	[[nodiscard]] glm::vec2 getMouseCursorPositionInPixels() const noexcept { return m_MouseCursorPositionInPixels; }
	[[nodiscard]] glm::vec2 getMouseMovementDeltaThisFrame() const noexcept { return m_MouseMovementDeltaThisFrameInPixels; }
	[[nodiscard]] glm::vec2 getMouseScrollDeltaThisFrame() const noexcept { return m_MouseScrollDeltaThisFrame; }

	// - Cursor capture (hidden and locked to the window, what you want for mouse look)
	void setMouseCursorCaptured(bool ShouldMouseCursorBeCaptured) noexcept;
	[[nodiscard]] bool isMouseCursorCaptured() const noexcept { return m_IsMouseCursorCaptured; }

private:
	// GLFW only takes plain function pointers, so these find our instance through the window user pointer and forward to it
	static void keyCallbackForwardedFromGlfw(GLFWwindow* Window, int GlfwKeyCode, int PlatformScancode, int GlfwAction, int ModifierKeyBitfield);
	static void mouseButtonCallbackForwardedFromGlfw(GLFWwindow* Window, int GlfwMouseButtonCode, int GlfwAction, int ModifierKeyBitfield);
	static void cursorPositionCallbackForwardedFromGlfw(GLFWwindow* Window, double CursorPositionX, double CursorPositionY);
	static void scrollCallbackForwardedFromGlfw(GLFWwindow* Window, double ScrollOffsetX, double ScrollOffsetY);
	static void windowFocusCallbackForwardedFromGlfw(GLFWwindow* Window, int HasWindowGainedFocus);

	[[nodiscard]] static bool isKeyCodeInRange(int GlfwKeyCode) noexcept { return GlfwKeyCode >= 0 && GlfwKeyCode <= GLFW_KEY_LAST; }
	[[nodiscard]] static bool isMouseButtonCodeInRange(int GlfwMouseButtonCode) noexcept { return GlfwMouseButtonCode >= 0 && GlfwMouseButtonCode <= GLFW_MOUSE_BUTTON_LAST; }

	void releaseEveryHeldKeyAndButton() noexcept;

	GLFWwindow* m_AttachedWindow{ nullptr };

	// Keyboard state. "Held" persists across frames, "pressed"/"released" only live for the frame they happened in.
	// Tracking the events separately (instead of comparing this frame's and last frame's held state) means a tap that is
	// pressed and released inside the same poll still registers as a press.
	std::array<bool, GLFW_KEY_LAST + 1> m_IsKeyHeld{};
	std::array<bool, GLFW_KEY_LAST + 1> m_WasKeyPressedThisFrame{};
	std::array<bool, GLFW_KEY_LAST + 1> m_WasKeyReleasedThisFrame{};

	// Mouse button state, same idea
	std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_IsMouseButtonHeld{};
	std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_WasMouseButtonPressedThisFrame{};
	std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_WasMouseButtonReleasedThisFrame{};

	// Mouse movement
	glm::vec2 m_MouseCursorPositionInPixels{ 0.0f };
	glm::vec2 m_MouseMovementDeltaThisFrameInPixels{ 0.0f };
	glm::vec2 m_MouseScrollDeltaThisFrame{ 0.0f };
	bool m_HasReceivedFirstCursorPositionSinceReset{ false };	// The first position after attaching/capturing would otherwise read as a huge jump

	bool m_IsMouseCursorCaptured{ false };
};

}

#endif // !HEPHAESTIAN_INPUTMANAGER_H
