/**
 * @file InputManager.h
 * @brief Keyboard and mouse input, turned from GLFW callbacks into per-frame state.
 */
#ifndef HEPHAESTIAN_INPUTMANAGER_H
#define HEPHAESTIAN_INPUTMANAGER_H

#include <array>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace Hephaestian
{

/**
 * @brief Collects everything GLFW tells us about the keyboard and mouse, and turns it into per-frame state that systems
 * can query.
 *
 * Usage, once per frame:
 * 1. InputManager.beginNewFrame(): forgets last frame's "pressed"/"released" events and mouse deltas.
 * 2. glfwPollEvents(): GLFW fires our callbacks, which fill in this frame's state.
 * 3. Systems query isKeyHeld, wasKeyPressedThisFrame, getMouseMovementDeltaThisFrame...
 *
 * Key and button codes are just GLFW's (GLFW_KEY_W, GLFW_MOUSE_BUTTON_LEFT...), no need to reinvent those.
 */
class InputManager
{
public:
	/** @brief Not attached to any window yet: see attachToWindow. */
	InputManager() = default;
	/** @brief Detaches from its window, if any. */
	~InputManager() noexcept;

	/**
	 * @brief Hooks this window's GLFW callbacks up to this instance (and turns on raw mouse motion where supported).
	 *
	 * Only one InputManager per window: it uses the window's user pointer. Attaching again detaches from the previous
	 * window first.
	 * @param WindowToReceiveInputFrom The window to listen to. Throws if null.
	 */
	void attachToWindow(GLFWwindow* WindowToReceiveInputFrom);
	/** @brief Removes our callbacks (only if they're still ours), releases the cursor, and lets go of every held key. */
	void detachFromWindow() noexcept;

	/** @brief Forgets last frame's events and mouse deltas. Call right BEFORE glfwPollEvents. */
	void beginNewFrame() noexcept;

	// - Keyboard
	/**
	 * @brief Whether a key is down right now.
	 * @param GlfwKeyCode GLFW_KEY_... (out of range codes are simply not held).
	 * @return True while the key is held, frame after frame.
	 */
	[[nodiscard]] bool isKeyHeld(int GlfwKeyCode) const noexcept;
	/**
	 * @brief Whether a key went down this frame (a tap pressed and released within one poll still counts).
	 * @param GlfwKeyCode GLFW_KEY_...
	 * @return True only on the frame it was pressed.
	 */
	[[nodiscard]] bool wasKeyPressedThisFrame(int GlfwKeyCode) const noexcept;
	/**
	 * @brief Whether a key was let go of this frame.
	 * @param GlfwKeyCode GLFW_KEY_...
	 * @return True only on the frame it was released.
	 */
	[[nodiscard]] bool wasKeyReleasedThisFrame(int GlfwKeyCode) const noexcept;

	// - Mouse buttons
	/**
	 * @brief Whether a mouse button is down right now.
	 * @param GlfwMouseButtonCode GLFW_MOUSE_BUTTON_...
	 * @return True while the button is held.
	 */
	[[nodiscard]] bool isMouseButtonHeld(int GlfwMouseButtonCode) const noexcept;
	/**
	 * @brief Whether a mouse button went down this frame.
	 * @param GlfwMouseButtonCode GLFW_MOUSE_BUTTON_...
	 * @return True only on the frame it was pressed.
	 */
	[[nodiscard]] bool wasMouseButtonPressedThisFrame(int GlfwMouseButtonCode) const noexcept;
	/**
	 * @brief Whether a mouse button was let go of this frame.
	 * @param GlfwMouseButtonCode GLFW_MOUSE_BUTTON_...
	 * @return True only on the frame it was released.
	 */
	[[nodiscard]] bool wasMouseButtonReleasedThisFrame(int GlfwMouseButtonCode) const noexcept;

	// - Mouse movement (in screen pixels, +X right, +Y down, like GLFW)
	/**
	 * @brief Where the cursor is.
	 * @return In window pixels, +X right, +Y down.
	 */
	[[nodiscard]] glm::vec2 getMouseCursorPositionInPixels() const noexcept { return m_MouseCursorPositionInPixels; }
	/**
	 * @brief How far the mouse moved this frame (all of this frame's movement events added up).
	 * @return In pixels, +X right, +Y down. Zero on the frame the cursor gets captured or released.
	 */
	[[nodiscard]] glm::vec2 getMouseMovementDeltaThisFrame() const noexcept { return m_MouseMovementDeltaThisFrameInPixels; }
	/**
	 * @brief How far the wheel (or touchpad) scrolled this frame.
	 * @return GLFW's scroll units, +Y away from the user.
	 */
	[[nodiscard]] glm::vec2 getMouseScrollDeltaThisFrame() const noexcept { return m_MouseScrollDeltaThisFrame; }

	// - Cursor capture (hidden and locked to the window, what you want for mouse look)
	/**
	 * @brief Hides the cursor and locks it to the window (mouse look), or gives it back.
	 *
	 * Released automatically when the window loses focus.
	 * @param ShouldMouseCursorBeCaptured True to capture, false to release.
	 */
	void setMouseCursorCaptured(bool ShouldMouseCursorBeCaptured) noexcept;
	/**
	 * @brief Whether the cursor is captured.
	 * @return True while hidden and locked to the window.
	 */
	[[nodiscard]] bool isMouseCursorCaptured() const noexcept { return m_IsMouseCursorCaptured; }

private:
	// GLFW only takes plain function pointers, so these find our instance through the window user pointer and forward to it
	/**
	 * @brief GLFW's key callback: records presses and releases (repeats are ignored, "held" covers them).
	 * @param Window The window the event came from.
	 * @param GlfwKeyCode GLFW_KEY_... (GLFW_KEY_UNKNOWN is ignored).
	 * @param PlatformScancode Unused.
	 * @param GlfwAction GLFW_PRESS, GLFW_RELEASE or GLFW_REPEAT.
	 * @param ModifierKeyBitfield Unused.
	 */
	static void keyCallbackForwardedFromGlfw(GLFWwindow* Window, int GlfwKeyCode, int PlatformScancode, int GlfwAction, int ModifierKeyBitfield);
	/**
	 * @brief GLFW's mouse button callback: records presses and releases.
	 * @param Window The window the event came from.
	 * @param GlfwMouseButtonCode GLFW_MOUSE_BUTTON_...
	 * @param GlfwAction GLFW_PRESS or GLFW_RELEASE.
	 * @param ModifierKeyBitfield Unused.
	 */
	static void mouseButtonCallbackForwardedFromGlfw(GLFWwindow* Window, int GlfwMouseButtonCode, int GlfwAction, int ModifierKeyBitfield);
	/**
	 * @brief GLFW's cursor callback: adds up this frame's movement (it can fire several times per poll).
	 * @param Window The window the event came from.
	 * @param CursorPositionX New cursor X, in window pixels.
	 * @param CursorPositionY New cursor Y, in window pixels.
	 */
	static void cursorPositionCallbackForwardedFromGlfw(GLFWwindow* Window, double CursorPositionX, double CursorPositionY);
	/**
	 * @brief GLFW's scroll callback: adds up this frame's scrolling.
	 * @param Window The window the event came from.
	 * @param ScrollOffsetX Horizontal scroll.
	 * @param ScrollOffsetY Vertical scroll.
	 */
	static void scrollCallbackForwardedFromGlfw(GLFWwindow* Window, double ScrollOffsetX, double ScrollOffsetY);
	/**
	 * @brief GLFW's focus callback: losing focus lets go of every key and the cursor, since their releases would never
	 * reach us.
	 * @param Window The window the event came from.
	 * @param HasWindowGainedFocus GLFW_TRUE when gaining focus, GLFW_FALSE when losing it.
	 */
	static void windowFocusCallbackForwardedFromGlfw(GLFWwindow* Window, int HasWindowGainedFocus);

	/**
	 * @brief Whether a key code can index the key arrays.
	 * @param GlfwKeyCode The code to check.
	 * @return True for 0 to GLFW_KEY_LAST.
	 */
	[[nodiscard]] static bool isKeyCodeInRange(int GlfwKeyCode) noexcept { return GlfwKeyCode >= 0 && GlfwKeyCode <= GLFW_KEY_LAST; }
	/**
	 * @brief Whether a mouse button code can index the button arrays.
	 * @param GlfwMouseButtonCode The code to check.
	 * @return True for 0 to GLFW_MOUSE_BUTTON_LAST.
	 */
	[[nodiscard]] static bool isMouseButtonCodeInRange(int GlfwMouseButtonCode) noexcept { return GlfwMouseButtonCode >= 0 && GlfwMouseButtonCode <= GLFW_MOUSE_BUTTON_LAST; }

	/** @brief Marks every key and mouse button as not held. */
	void releaseEveryHeldKeyAndButton() noexcept;

	GLFWwindow* m_AttachedWindow{ nullptr };	///< The window we listen to (null when detached)

	// Keyboard state. "Held" persists across frames, "pressed"/"released" only live for the frame they happened in.
	// Tracking the events separately (instead of comparing this frame's and last frame's held state) means a tap that is
	// pressed and released inside the same poll still registers as a press.
	std::array<bool, GLFW_KEY_LAST + 1> m_IsKeyHeld{};					///< Per key: down right now
	std::array<bool, GLFW_KEY_LAST + 1> m_WasKeyPressedThisFrame{};		///< Per key: went down this frame
	std::array<bool, GLFW_KEY_LAST + 1> m_WasKeyReleasedThisFrame{};	///< Per key: went up this frame

	// Mouse button state, same idea
	std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_IsMouseButtonHeld{};					///< Per button: down right now
	std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_WasMouseButtonPressedThisFrame{};	///< Per button: went down this frame
	std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> m_WasMouseButtonReleasedThisFrame{};	///< Per button: went up this frame

	// Mouse movement
	glm::vec2 m_MouseCursorPositionInPixels{ 0.0f };			///< See getMouseCursorPositionInPixels
	glm::vec2 m_MouseMovementDeltaThisFrameInPixels{ 0.0f };	///< See getMouseMovementDeltaThisFrame
	glm::vec2 m_MouseScrollDeltaThisFrame{ 0.0f };				///< See getMouseScrollDeltaThisFrame
	bool m_HasReceivedFirstCursorPositionSinceReset{ false };	///< The first position after attaching/capturing would otherwise read as a huge jump

	bool m_IsMouseCursorCaptured{ false };	///< See isMouseCursorCaptured
};

}

#endif // !HEPHAESTIAN_INPUTMANAGER_H
