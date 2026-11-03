#include "Hephaestian/Input/InputManager.h"

#include <Vulkanised/Logger.h>

namespace Hephaestian
{

InputManager::~InputManager() noexcept
{
	detachFromWindow();
}

void InputManager::attachToWindow(GLFWwindow* WindowToReceiveInputFrom)
{
	if (WindowToReceiveInputFrom == nullptr)
	{
		LOGNTHROW("InputManager cannot attach to a null window");
	}

	detachFromWindow();

	m_AttachedWindow = WindowToReceiveInputFrom;
	glfwSetWindowUserPointer(m_AttachedWindow, this);

	glfwSetKeyCallback(m_AttachedWindow, &InputManager::keyCallbackForwardedFromGlfw);
	glfwSetMouseButtonCallback(m_AttachedWindow, &InputManager::mouseButtonCallbackForwardedFromGlfw);
	glfwSetCursorPosCallback(m_AttachedWindow, &InputManager::cursorPositionCallbackForwardedFromGlfw);
	glfwSetScrollCallback(m_AttachedWindow, &InputManager::scrollCallbackForwardedFromGlfw);
	glfwSetWindowFocusCallback(m_AttachedWindow, &InputManager::windowFocusCallbackForwardedFromGlfw);

	// Mouse look feels a lot better without OS mouse acceleration, when the platform supports it
	if (glfwRawMouseMotionSupported() == GLFW_TRUE)
	{
		glfwSetInputMode(m_AttachedWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}

	double InitialCursorPositionX{ 0.0 };
	double InitialCursorPositionY{ 0.0 };
	glfwGetCursorPos(m_AttachedWindow, &InitialCursorPositionX, &InitialCursorPositionY);
	m_MouseCursorPositionInPixels = { static_cast<float>(InitialCursorPositionX), static_cast<float>(InitialCursorPositionY) };
	m_HasReceivedFirstCursorPositionSinceReset = true;
}

void InputManager::detachFromWindow() noexcept
{
	if (m_AttachedWindow == nullptr)
	{
		return;
	}

	// Only clear what we set, in case the window has since been handed to someone else
	if (glfwGetWindowUserPointer(m_AttachedWindow) == this)
	{
		glfwSetKeyCallback(m_AttachedWindow, nullptr);
		glfwSetMouseButtonCallback(m_AttachedWindow, nullptr);
		glfwSetCursorPosCallback(m_AttachedWindow, nullptr);
		glfwSetScrollCallback(m_AttachedWindow, nullptr);
		glfwSetWindowFocusCallback(m_AttachedWindow, nullptr);
		glfwSetWindowUserPointer(m_AttachedWindow, nullptr);
	}

	m_AttachedWindow = nullptr;
	m_IsMouseCursorCaptured = false;
	releaseEveryHeldKeyAndButton();
}

void InputManager::beginNewFrame() noexcept
{
	m_WasKeyPressedThisFrame.fill(false);
	m_WasKeyReleasedThisFrame.fill(false);
	m_WasMouseButtonPressedThisFrame.fill(false);
	m_WasMouseButtonReleasedThisFrame.fill(false);

	m_MouseMovementDeltaThisFrameInPixels = glm::vec2(0.0f);
	m_MouseScrollDeltaThisFrame = glm::vec2(0.0f);
}

bool InputManager::isKeyHeld(int GlfwKeyCode) const noexcept
{
	return isKeyCodeInRange(GlfwKeyCode) && m_IsKeyHeld[static_cast<size_t>(GlfwKeyCode)];
}

bool InputManager::wasKeyPressedThisFrame(int GlfwKeyCode) const noexcept
{
	return isKeyCodeInRange(GlfwKeyCode) && m_WasKeyPressedThisFrame[static_cast<size_t>(GlfwKeyCode)];
}

bool InputManager::wasKeyReleasedThisFrame(int GlfwKeyCode) const noexcept
{
	return isKeyCodeInRange(GlfwKeyCode) && m_WasKeyReleasedThisFrame[static_cast<size_t>(GlfwKeyCode)];
}

bool InputManager::isMouseButtonHeld(int GlfwMouseButtonCode) const noexcept
{
	return isMouseButtonCodeInRange(GlfwMouseButtonCode) && m_IsMouseButtonHeld[static_cast<size_t>(GlfwMouseButtonCode)];
}

bool InputManager::wasMouseButtonPressedThisFrame(int GlfwMouseButtonCode) const noexcept
{
	return isMouseButtonCodeInRange(GlfwMouseButtonCode) && m_WasMouseButtonPressedThisFrame[static_cast<size_t>(GlfwMouseButtonCode)];
}

bool InputManager::wasMouseButtonReleasedThisFrame(int GlfwMouseButtonCode) const noexcept
{
	return isMouseButtonCodeInRange(GlfwMouseButtonCode) && m_WasMouseButtonReleasedThisFrame[static_cast<size_t>(GlfwMouseButtonCode)];
}

void InputManager::setMouseCursorCaptured(bool ShouldMouseCursorBeCaptured) noexcept
{
	if (m_AttachedWindow == nullptr || ShouldMouseCursorBeCaptured == m_IsMouseCursorCaptured)
	{
		return;
	}

	glfwSetInputMode(m_AttachedWindow, GLFW_CURSOR, ShouldMouseCursorBeCaptured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
	m_IsMouseCursorCaptured = ShouldMouseCursorBeCaptured;

	// GLFW moves the virtual cursor around when switching modes, which would read as one giant mouse flick
	m_HasReceivedFirstCursorPositionSinceReset = false;
	m_MouseMovementDeltaThisFrameInPixels = glm::vec2(0.0f);
}

void InputManager::keyCallbackForwardedFromGlfw(GLFWwindow* Window, int GlfwKeyCode, int /*PlatformScancode*/, int GlfwAction, int /*ModifierKeyBitfield*/)
{
	InputManager* InputManagerOwningWindow = static_cast<InputManager*>(glfwGetWindowUserPointer(Window));
	if (InputManagerOwningWindow == nullptr || !isKeyCodeInRange(GlfwKeyCode))	// GLFW_KEY_UNKNOWN is -1
	{
		return;
	}

	const size_t KeyIndex = static_cast<size_t>(GlfwKeyCode);
	if (GlfwAction == GLFW_PRESS)
	{
		InputManagerOwningWindow->m_IsKeyHeld[KeyIndex] = true;
		InputManagerOwningWindow->m_WasKeyPressedThisFrame[KeyIndex] = true;
	}
	else if (GlfwAction == GLFW_RELEASE)
	{
		InputManagerOwningWindow->m_IsKeyHeld[KeyIndex] = false;
		InputManagerOwningWindow->m_WasKeyReleasedThisFrame[KeyIndex] = true;
	}
	// GLFW_REPEAT is ignored on purpose, "held" already covers it
}

void InputManager::mouseButtonCallbackForwardedFromGlfw(GLFWwindow* Window, int GlfwMouseButtonCode, int GlfwAction, int /*ModifierKeyBitfield*/)
{
	InputManager* InputManagerOwningWindow = static_cast<InputManager*>(glfwGetWindowUserPointer(Window));
	if (InputManagerOwningWindow == nullptr || !isMouseButtonCodeInRange(GlfwMouseButtonCode))
	{
		return;
	}

	const size_t MouseButtonIndex = static_cast<size_t>(GlfwMouseButtonCode);
	if (GlfwAction == GLFW_PRESS)
	{
		InputManagerOwningWindow->m_IsMouseButtonHeld[MouseButtonIndex] = true;
		InputManagerOwningWindow->m_WasMouseButtonPressedThisFrame[MouseButtonIndex] = true;
	}
	else if (GlfwAction == GLFW_RELEASE)
	{
		InputManagerOwningWindow->m_IsMouseButtonHeld[MouseButtonIndex] = false;
		InputManagerOwningWindow->m_WasMouseButtonReleasedThisFrame[MouseButtonIndex] = true;
	}
}

void InputManager::cursorPositionCallbackForwardedFromGlfw(GLFWwindow* Window, double CursorPositionX, double CursorPositionY)
{
	InputManager* InputManagerOwningWindow = static_cast<InputManager*>(glfwGetWindowUserPointer(Window));
	if (InputManagerOwningWindow == nullptr)
	{
		return;
	}

	const glm::vec2 NewCursorPositionInPixels{ static_cast<float>(CursorPositionX), static_cast<float>(CursorPositionY) };

	if (InputManagerOwningWindow->m_HasReceivedFirstCursorPositionSinceReset)
	{
		// Accumulate, since GLFW can fire this several times within one poll
		InputManagerOwningWindow->m_MouseMovementDeltaThisFrameInPixels += NewCursorPositionInPixels - InputManagerOwningWindow->m_MouseCursorPositionInPixels;
	}
	InputManagerOwningWindow->m_HasReceivedFirstCursorPositionSinceReset = true;
	InputManagerOwningWindow->m_MouseCursorPositionInPixels = NewCursorPositionInPixels;
}

void InputManager::scrollCallbackForwardedFromGlfw(GLFWwindow* Window, double ScrollOffsetX, double ScrollOffsetY)
{
	InputManager* InputManagerOwningWindow = static_cast<InputManager*>(glfwGetWindowUserPointer(Window));
	if (InputManagerOwningWindow == nullptr)
	{
		return;
	}

	InputManagerOwningWindow->m_MouseScrollDeltaThisFrame += glm::vec2{ static_cast<float>(ScrollOffsetX), static_cast<float>(ScrollOffsetY) };
}

void InputManager::windowFocusCallbackForwardedFromGlfw(GLFWwindow* Window, int HasWindowGainedFocus)
{
	InputManager* InputManagerOwningWindow = static_cast<InputManager*>(glfwGetWindowUserPointer(Window));
	if (InputManagerOwningWindow == nullptr)
	{
		return;
	}

	// Alt-tabbing away while holding W would otherwise keep us walking forever, since the release never reaches us
	if (HasWindowGainedFocus == GLFW_FALSE)
	{
		InputManagerOwningWindow->releaseEveryHeldKeyAndButton();
		InputManagerOwningWindow->setMouseCursorCaptured(false);
	}
}

void InputManager::releaseEveryHeldKeyAndButton() noexcept
{
	m_IsKeyHeld.fill(false);
	m_IsMouseButtonHeld.fill(false);
}

}
