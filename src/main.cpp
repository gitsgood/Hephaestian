#include <Vulkanised/Logger.h>
#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/Assets/AssetLibrary.h"
#include "Hephaestian/Input/InputManager.h"
#include "Hephaestian/ECS/Scene.h"
#include "Hephaestian/Settings/HephaestianSettingsManager.h"
#include "Hephaestian/UI/HephaestianSettingsWidget.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <imgui.h>

// Little trick I stole from Rust. Ideally this should be immutable and thread-safe, but at least it is supposed to be somehow thread-safe now. This might come in handy down the line.
Vulkanised::VAtomic<GLFWwindow> window;
Vulkanised::VulkanRenderer renderer;

static void initWindow(std::string_view wName = "Test Window", const int width = 800, const int height = 600)
{
	if (glfwInit() == GLFW_FALSE)
	{
		constexpr const char* message{ "Failed to initialize GLFW" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Set GLFW to not create an OpenGL context
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	// Create the raw pointer
	GLFWwindow* rawWindow = glfwCreateWindow(width, height, wName.data(), nullptr, nullptr);

	if (!rawWindow)
	{
		LOGNTHROW("Failed to create GLFW window");
	}

	// Sets Hephaestian's icon, courtesy of Martin's Legion. Found next to the exe, wherever it's launched from.
	// Not on Windows: there the icon is compiled into the exe (resources/Hephaestian.rc) and GLFW applies it on its own,
	// with a properly drawn image for every size instead of this single 500px one scaled down.
#if !defined(_WIN32)
	const std::string WindowIconFilePath = (Vulkanised::Utilities::File::getRuntimeAssetsDirectory() / "resources" / "Hephaestos.png").string();
	GLFWimage icon{};
	icon.pixels = stbi_load(WindowIconFilePath.c_str(), &icon.width, &icon.height, nullptr, 4);
	if (icon.pixels)
	{
		if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
			glfwSetWindowIcon(rawWindow, 1, &icon);
		stbi_image_free(icon.pixels);
	}
	else
	{
		LOGE("Failed to use Hephaestian's icon.");
	}
#endif

	// Wrap it in a shared_ptr with a custom deleter
	// We pass glfwDestroyWindow as the function to call when the ref count hits 0
	window = std::shared_ptr<GLFWwindow>(rawWindow, glfwDestroyWindow);
}

auto main(int ArgumentCount, char** Arguments) -> int
{
	using namespace Hephaestian;

	// Command line:
	//	"--scene <path>": the scene to open at startup (relative to assets/), instead of HephaestianSettings' startupScene
	//	"--screenshot-and-exit <seconds> <file.bmp>": render for that long, save one frame, quit (automated checks)
	std::optional<std::filesystem::path> StartupSceneFromCommandLine;
	std::optional<float> ScreenshotAndExitAfterSeconds;
	std::filesystem::path ScreenshotAndExitFilePath;
	for (int CurrentArgumentIndex{ 1 }; CurrentArgumentIndex < ArgumentCount; ++CurrentArgumentIndex)
	{
		const std::string_view CurrentArgument{ Arguments[CurrentArgumentIndex] };
		if (CurrentArgument == "--scene" && CurrentArgumentIndex + 1 < ArgumentCount)
		{
			StartupSceneFromCommandLine = Arguments[CurrentArgumentIndex + 1];
		}
		else if (CurrentArgument == "--screenshot-and-exit" && CurrentArgumentIndex + 2 < ArgumentCount)
		{
			ScreenshotAndExitAfterSeconds = std::stof(Arguments[CurrentArgumentIndex + 1]);
			ScreenshotAndExitFilePath = Arguments[CurrentArgumentIndex + 2];
		}
	}

	LOGH("Hephaestian lives");

	// Create window
	initWindow("Hephaestian", 1366, 768);

	// Create Vulkan renderer instance and initialize it with the window
	if (renderer.init(window.load().get()) == EXIT_FAILURE)
	{
		printf("Failed to initialize Vulkan renderer\n");
		return EXIT_FAILURE;
	}

	// Engine settings (HephaestianSettings.json, next to the exe). Everything that depends on a setting subscribes to it,
	// so it follows along no matter who changes it: a GUI, a key press, or someone editing the JSON while the game runs.
	// (Clear colour and light are applied every frame instead, since the loaded scene can override them.)
	HephaestianSettingsManager SettingsManager;
	const SettingsChangeSubscription RendererSettingsSubscription = SettingsManager.subscribeToSettingsChanges(
		[](const HephaestianSettings& NewSettings, const HephaestianSettings& PreviousSettings)
		{
			renderer.setInfiniteGridVisible(NewSettings.m_ShowInfiniteGrid);	// Also runs once right now, applying the loaded value
			if (NewSettings.m_ShowInfiniteGrid != PreviousSettings.m_ShowInfiniteGrid)
			{
				LOG("Infinite grid {}", NewSettings.m_ShowInfiniteGrid ? "shown" : "hidden");
			}
		});

	InputManager Input;
	Input.attachToWindow(window.load().get());

	// ImGui AFTER the InputManager: it chains onto the GLFW callbacks that are already installed, so both keep getting input
	renderer.initialiseImGui(window.load().get());

	// --- SCENE ---
	// Every model, texture and primitive is loaded once and shared by whichever scenes use it
	Assets::AssetLibrary AssetLibrary(renderer);
	Scene GameScene;
	const std::filesystem::path StartupSceneFilePath = Vulkanised::Utilities::File::getRuntimeAssetsDirectory()
		/ StartupSceneFromCommandLine.value_or(SettingsManager.getCurrentSettings().m_StartupSceneRelativePath);
	if (!GameScene.loadFromJsonFile(StartupSceneFilePath, AssetLibrary))
	{
		LOGE("The startup scene couldn't be loaded: pick one in the settings window.");
	}

	UI::HephaestianSettingsWidget SettingsWidget(SettingsManager, GameScene, AssetLibrary, renderer);

	LOG("Controls: left click to capture the mouse, Escape to release it, WASD/QE to move, Shift to sprint, F12 for a screenshot. Settings and scenes are in the ImGui window.");

	float LastFrameTimeInSeconds = static_cast<float>(glfwGetTime());

	// --- GAME LOOP ---
	while (!glfwWindowShouldClose(window.load().get()))
	{
		Input.beginNewFrame();
		glfwPollEvents();
		SettingsManager.pollSettingsFileForExternalChanges();	// Picks up edits to HephaestianSettings.json (checks twice a second at most)

		const float CurrentFrameTimeInSeconds = static_cast<float>(glfwGetTime());
		// Clamped so a breakpoint, a window drag or a resize doesn't teleport everything on the next frame
		const float DeltaTimeInSeconds = glm::min(CurrentFrameTimeInSeconds - LastFrameTimeInSeconds, 0.1f);
		LastFrameTimeInSeconds = CurrentFrameTimeInSeconds;

		// 1. UI (built first, so ImGui already knows this frame whether the mouse is over one of its windows)
		renderer.beginImGuiFrame();
		SettingsWidget.drawSettingsWindow();

		// 2. APPLICATION-LEVEL INPUT
		ImGuiIO& ImGuiInputOutput = ImGui::GetIO();
		if (Input.wasMouseButtonPressedThisFrame(GLFW_MOUSE_BUTTON_LEFT) && !ImGuiInputOutput.WantCaptureMouse)
		{
			Input.setMouseCursorCaptured(true);	// Clicking the 3D view grabs the mouse, clicking the settings window doesn't
		}
		if (Input.wasKeyPressedThisFrame(GLFW_KEY_ESCAPE))
		{
			Input.setMouseCursorCaptured(false);
		}
		if (Input.wasKeyPressedThisFrame(GLFW_KEY_F12))
		{
			const auto ScreenshotTimestampInSeconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			renderer.requestScreenshot(Vulkanised::Utilities::File::getExecutableDirectory() / "Screenshots" / ("Hephaestian_" + std::to_string(ScreenshotTimestampInSeconds) + ".bmp"));
		}
		if (ScreenshotAndExitAfterSeconds.has_value() && CurrentFrameTimeInSeconds >= ScreenshotAndExitAfterSeconds.value())
		{
			if (!renderer.isScreenshotPending() && !ScreenshotAndExitFilePath.empty())
			{
				renderer.requestScreenshot(ScreenshotAndExitFilePath);	// Saved by this frame's SubmitFrame
				ScreenshotAndExitFilePath.clear();
			}
			else if (!renderer.isScreenshotPending())
			{
				glfwSetWindowShouldClose(window.load().get(), GLFW_TRUE);	// The screenshot frame has been submitted: done

				// Automated runs read their performance numbers from here
				const Vulkanised::Rendering::FrameStatistics& Statistics = renderer.getFrameStatistics();
				LOG("Frame statistics: frame {:.3f} ms, CPU submit {:.3f} ms, CPU waiting on GPU {:.3f} ms, GPU {:.3f} ms, instances {} submitted / {} culled / {} drawn, {} draw calls",
					Statistics.m_FrameIntervalInMilliseconds, Statistics.m_CpuSubmitTimeInMilliseconds, Statistics.m_CpuWaitForGpuTimeInMilliseconds, Statistics.m_GpuFrameTimeInMilliseconds,
					Statistics.m_SubmittedInstanceCount, Statistics.m_CulledInstanceCount, Statistics.m_DrawnInstanceCount, Statistics.m_DrawCallCount);
			}
		}
		// While looking around, the (hidden, locked) cursor must not hover or click ImGui windows behind our back
		if (Input.isMouseCursorCaptured())
		{
			ImGuiInputOutput.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		}
		else
		{
			ImGuiInputOutput.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		}

		// 3. ECS SYSTEMS (movement, spinning, transform hierarchy)
		GameScene.updateSimulation(Input, DeltaTimeInSeconds);

		// 4. ENVIRONMENT (the scene's clear colour/light where it sets them, the engine settings otherwise), then
		//    BUILD RENDER PACKET + SUBMIT TO RENDERER (active camera, then one instance per submesh of every model, then the UI on top)
		GameScene.applyEnvironmentToRenderer(renderer, SettingsManager.getCurrentSettings());
		GameScene.renderToRenderer(renderer);
	}

	// GLFW gets terminated by the renderer's cleanup, so let go of the window's callbacks before that
	Input.detachFromWindow();
	renderer.cleanup();

	return EXIT_SUCCESS;
}
