#include <Vulkanised/Logger.h>
#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/Assets/ModelImporter.h"
#include "Hephaestian/Input/InputManager.h"
#include "Hephaestian/ECS/Scene.h"
#include "Hephaestian/ECS/Components.h"
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

	// Sets Hephaestian's icon, courtesy of Martin's Legion
	GLFWimage icon{};
	icon.pixels = stbi_load("Assets/Engine/Hephaestos.png", &icon.width, &icon.height, nullptr, 4);
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

	// Wrap it in a shared_ptr with a custom deleter
	// We pass glfwDestroyWindow as the function to call when the ref count hits 0
	window = std::shared_ptr<GLFWwindow>(rawWindow, glfwDestroyWindow);
}

// Same idea as the old RandomTransform, but split into position/rotation/scale so it can live in a TransformComponent
//static auto generateRandomTransformComponent() -> Hephaestian::Components::TransformComponent
//{
//	// Make the engine static so it seeds ONCE on the first call, then persists.
//	static std::random_device RandomDevice;
//	static std::mt19937 RandomNumberGenerator(RandomDevice());
//
//	std::uniform_real_distribution<float> RandomTranslationDistribution(-10.0f, 10.0f);
//	std::uniform_real_distribution<float> RandomRotationAngleDistribution(0.0f, glm::two_pi<float>());
//	std::uniform_real_distribution<float> RandomScaleDistribution(0.5f, 2.0f);
//	std::uniform_real_distribution<float> RandomAxisComponentDistribution(-1.0f, 1.0f);
//
//	glm::vec3 RandomRotationAxis{ RandomAxisComponentDistribution(RandomNumberGenerator), RandomAxisComponentDistribution(RandomNumberGenerator), RandomAxisComponentDistribution(RandomNumberGenerator) };
//	// Handle the rare case of a zero vector to avoid NaN errors during normalization
//	if (glm::length(RandomRotationAxis) < 0.001f)
//	{
//		RandomRotationAxis = glm::vec3{ 0.0f, 1.0f, 0.0f };
//	}
//
//	Hephaestian::Components::TransformComponent RandomTransform{};
//	RandomTransform.m_LocalPosition = { RandomTranslationDistribution(RandomNumberGenerator), RandomTranslationDistribution(RandomNumberGenerator), RandomTranslationDistribution(RandomNumberGenerator) - 12.0f };	// Pushed back so it starts in front of the camera
//	RandomTransform.m_LocalRotation = glm::angleAxis(RandomRotationAngleDistribution(RandomNumberGenerator), glm::normalize(RandomRotationAxis));
//	RandomTransform.m_LocalScale = glm::vec3(RandomScaleDistribution(RandomNumberGenerator));	// Uniform, to avoid distortion
//	return RandomTransform;
//}

auto main(int ArgumentCount, char** Arguments) -> int
{
	using namespace Hephaestian;

	// "--screenshot-and-exit <seconds> <file.bmp>": render for that long, save one frame, quit (automated checks)
	std::optional<float> ScreenshotAndExitAfterSeconds;
	std::filesystem::path ScreenshotAndExitFilePath;
	for (int CurrentArgumentIndex{ 1 }; CurrentArgumentIndex < ArgumentCount; ++CurrentArgumentIndex)
	{
		if (std::string_view(Arguments[CurrentArgumentIndex]) == "--screenshot-and-exit" && CurrentArgumentIndex + 2 < ArgumentCount)
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
	HephaestianSettingsManager SettingsManager;
	const SettingsChangeSubscription RendererSettingsSubscription = SettingsManager.subscribeToSettingsChanges(
		[](const HephaestianSettings& NewSettings, const HephaestianSettings& PreviousSettings)
		{
			// Also runs once right now, applying the loaded values
			renderer.setInfiniteGridVisible(NewSettings.m_ShowInfiniteGrid);
			renderer.setClearColour({ NewSettings.m_BackgroundClearColour[0], NewSettings.m_BackgroundClearColour[1], NewSettings.m_BackgroundClearColour[2] });
			renderer.setLightPosition({ NewSettings.m_LightWorldPosition[0], NewSettings.m_LightWorldPosition[1], NewSettings.m_LightWorldPosition[2] });
			renderer.setLightColour({ NewSettings.m_LightColour[0], NewSettings.m_LightColour[1], NewSettings.m_LightColour[2] });

			if (NewSettings.m_ShowInfiniteGrid != PreviousSettings.m_ShowInfiniteGrid)
			{
				LOG("Infinite grid {}", NewSettings.m_ShowInfiniteGrid ? "shown" : "hidden");
			}
		});

	InputManager Input;
	Input.attachToWindow(window.load().get());

	// ImGui AFTER the InputManager: it chains onto the GLFW callbacks that are already installed, so both keep getting input
	renderer.initialiseImGui(window.load().get());
	UI::HephaestianSettingsWidget SettingsWidget(SettingsManager);

	// --- ASSET LOADING PHASE ---


	// --- SCENE SETUP PHASE ---
	Scene GameScene;
	entt::registry& Registry = GameScene.getRegistry();

	// The player: an invisible entity that moves with WASD/QE + mouse. The main camera rides along as its child.
	const entt::entity PlayerEntity = GameScene.createEntity("Player");
	Registry.get<Components::TransformComponent>(PlayerEntity).m_LocalPosition = { 0.0f, 1.7f, 3.0f };	// Eye height, otherwise the camera sits exactly in the grid's plane (y = 0) and sees it edge-on
	Registry.emplace<Components::FreeFlyMovementControllerComponent>(PlayerEntity);

	const entt::entity PlayerCameraEntity = GameScene.createEntity("Player Camera");
	Registry.emplace<Components::CameraComponent>(PlayerCameraEntity);
	GameScene.attachEntityToParent(PlayerCameraEntity, PlayerEntity);	// Local transform is identity, so the camera sits exactly at the player's eyes


	// IMPORTED MODELS: everything in assets/Models gets loaded as-is (its own size, position and orientation from the file)
	Assets::ModelImporter ModelFileImporter(renderer);
	std::error_code ModelsDirectoryIterationError;
	for (const std::filesystem::directory_entry& CurrentDirectoryEntry : std::filesystem::directory_iterator(Vulkanised::Utilities::File::getRuntimeAssetsDirectory() / "Models", ModelsDirectoryIterationError))
	{
		if (!CurrentDirectoryEntry.is_regular_file() || !Assets::ModelImporter::isModelFileExtensionSupported(CurrentDirectoryEntry.path()))
		{
			continue;	// .bin, .mtl, textures, README...
		}

		const std::optional<Assets::ImportedModel> ImportedModelResult = ModelFileImporter.importModelFromFile(CurrentDirectoryEntry.path());
		if (ImportedModelResult.has_value())
		{
			const entt::entity ImportedModelEntity = GameScene.createEntity(CurrentDirectoryEntry.path().filename().string());
			Registry.emplace<Components::ModelRendererComponent>(ImportedModelEntity, ImportedModelResult->m_ModelIdentifier);
		}
	}

	GameScene.setActiveCameraEntity(PlayerCameraEntity);

	LOG("Controls: left click to capture the mouse, Escape to release it, WASD/QE to move, Shift to sprint, F12 for a screenshot. Settings are in the ImGui window.");// , C to switch camera.");

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
		//if (Input.wasKeyPressedThisFrame(GLFW_KEY_C))
		//{
		//	const bool IsPlayerCameraActive = GameScene.getActiveCameraEntity() == PlayerCameraEntity;
		//	GameScene.setActiveCameraEntity(IsPlayerCameraActive ? TotemCameraEntity : PlayerCameraEntity);
		//}

		// 3. ECS SYSTEMS (movement, spinning, transform hierarchy)
		GameScene.updateSimulation(Input, DeltaTimeInSeconds);

		// 4. BUILD RENDER PACKET + SUBMIT TO RENDERER (active camera, then one instance per submesh of every model, then the UI on top)
		GameScene.renderToRenderer(renderer);
	}

	// GLFW gets terminated by the renderer's cleanup, so let go of the window's callbacks before that
	Input.detachFromWindow();
	renderer.cleanup();

	return EXIT_SUCCESS;
}
