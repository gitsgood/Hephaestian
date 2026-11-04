#include <Vulkanised/Logger.h>
#include <Vulkanised/VulkanRenderer.h>

#include "Hephaestian/Assets/ModelImporter.h"
#include "Hephaestian/Input/InputManager.h"
#include "Hephaestian/ECS/Scene.h"
#include "Hephaestian/ECS/Components.h"
#include "Hephaestian/Settings/HephaestianSettingsManager.h"
#include "Hephaestian/UI/HephaestianSettingsWidget.h"
#include "Hephaestian/Simulation/QuarryRollingBallDemo.h"

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
	// "--bspline-surface": roll on the B-spline surface from the start, "--follow-ball": start with the chase camera
	std::optional<float> ScreenshotAndExitAfterSeconds;
	std::filesystem::path ScreenshotAndExitFilePath;
	bool ShouldStartOnBSplineSurface{ false };
	bool ShouldStartWithChaseCamera{ false };
	for (int CurrentArgumentIndex{ 1 }; CurrentArgumentIndex < ArgumentCount; ++CurrentArgumentIndex)
	{
		ShouldStartOnBSplineSurface = ShouldStartOnBSplineSurface || std::string_view(Arguments[CurrentArgumentIndex]) == "--bspline-surface";
		ShouldStartWithChaseCamera = ShouldStartWithChaseCamera || std::string_view(Arguments[CurrentArgumentIndex]) == "--follow-ball";
		if (std::string_view(Arguments[CurrentArgumentIndex]) == "--screenshot-and-exit" && CurrentArgumentIndex + 2 < ArgumentCount)
		{
			ScreenshotAndExitAfterSeconds = std::stof(Arguments[CurrentArgumentIndex + 1]);
			ScreenshotAndExitFilePath = Arguments[CurrentArgumentIndex + 2];
		}
	}

	LOGH("Bruh");

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

	//const std::vector<Vulkanised::Vertex> SquareQuadVertices = {
	//	{ { -0.4, 0.4, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },		// 0
	//	{ { -0.4, -0.4, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },		// 1
	//	{ { 0.4, -0.4, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },		// 2
	//	{ { 0.4, 0.4, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },		// 3
	//};
	//const std::vector<Vulkanised::Vertex> TallQuadVertices = {
	//	{ { -0.25, 0.6, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },		// 0
	//	{ { -0.25, -0.6, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },		// 1
	//	{ { 0.25, -0.6, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },		// 2
	//	{ { 0.25, 0.6, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 1.0f} , { 0.0f, 0.0f, 1.0f } },		// 3
	//};
	//const std::vector<uint32_t> QuadIndices = { 0, 1, 2, 2, 3, 0 };

	//const uint32_t BibiTextureIdentifier = static_cast<uint32_t>(renderer.createTexture("bibi.jpg"));
	//const uint32_t TrumpTextureIdentifier = static_cast<uint32_t>(renderer.createTexture("trump.jpg"));

	//const uint32_t SquareQuadMeshIdentifier = renderer.loadMesh(SquareQuadVertices, QuadIndices, static_cast<int>(BibiTextureIdentifier));
	//const uint32_t TallQuadMeshIdentifier = renderer.loadMesh(TallQuadVertices, QuadIndices, static_cast<int>(TrumpTextureIdentifier));

	//// Single mesh models (what the two quads used to be)
	//const uint32_t BibiSquareModelIdentifier = renderer.createModelFromSingleMesh(SquareQuadMeshIdentifier, BibiTextureIdentifier);
	//const uint32_t TrumpTallModelIdentifier = renderer.createModelFromSingleMesh(TallQuadMeshIdentifier, TrumpTextureIdentifier);

	//// A multi-mesh model: a little "totem" made of three pieces that reuse already-uploaded meshes, each with its own texture and offset.
	//// A model file loader would build the exact same thing (or a std::vector<ModelSubmeshGeometry> for renderer.loadModel).
	//const uint32_t TotemModelIdentifier = renderer.createModel({
	//	{ TallQuadMeshIdentifier,   TrumpTextureIdentifier, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)) },
	//	{ SquareQuadMeshIdentifier, BibiTextureIdentifier,  glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)) },
	//	{ SquareQuadMeshIdentifier, BibiTextureIdentifier,  glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, -0.01f)), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)) },	// Back face, so it isn't invisible from behind
	//});

	// --- SCENE SETUP PHASE ---
	Scene GameScene;
	entt::registry& Registry = GameScene.getRegistry();

	// The two spinning quads from before, now as entities
	//const entt::entity SpinningBibiEntity = GameScene.createEntity("Spinning Bibi");
	//Registry.get<Components::TransformComponent>(SpinningBibiEntity).m_LocalPosition = { -1.0f, 0.0f, -1.0f };
	//Registry.emplace<Components::ModelRendererComponent>(SpinningBibiEntity, BibiSquareModelIdentifier);
	//Registry.emplace<Components::ConstantRotationComponent>(SpinningBibiEntity, glm::vec3(0.0f, 0.0f, 1.0f), 10.0f);

	//const entt::entity SpinningTrumpEntity = GameScene.createEntity("Spinning Trump");
	//Registry.get<Components::TransformComponent>(SpinningTrumpEntity).m_LocalPosition = { 1.0f, 0.0f, -3.0f };
	//Registry.emplace<Components::ModelRendererComponent>(SpinningTrumpEntity, TrumpTallModelIdentifier);
	//Registry.emplace<Components::ConstantRotationComponent>(SpinningTrumpEntity, glm::vec3(0.0f, 0.0f, 1.0f), -100.0f);

	//// The multi-mesh totem, slowly turning around
	//const entt::entity TotemEntity = GameScene.createEntity("Totem");
	//Registry.get<Components::TransformComponent>(TotemEntity).m_LocalPosition = { 0.0f, -0.5f, -5.0f };
	//Registry.emplace<Components::ModelRendererComponent>(TotemEntity, TotemModelIdentifier);
	//Registry.emplace<Components::ConstantRotationComponent>(TotemEntity, glm::vec3(0.0f, 1.0f, 0.0f), 30.0f);

	// A field of random quads (they used to get a new random transform every frame, now they're proper entities)
	//for (int CurrentRandomQuadIndex{ 0 }; CurrentRandomQuadIndex < 100; ++CurrentRandomQuadIndex)
	//{
	//	const entt::entity RandomQuadEntity = GameScene.createEntity("Random Quad");
	//	Registry.replace<Components::TransformComponent>(RandomQuadEntity, generateRandomTransformComponent());
	//	Registry.emplace<Components::ModelRendererComponent>(RandomQuadEntity, BibiSquareModelIdentifier);
	//}

	// The player: an invisible entity that moves with WASD/QE + mouse. The main camera rides along as its child.
	const entt::entity PlayerEntity = GameScene.createEntity("Player");
	Registry.get<Components::TransformComponent>(PlayerEntity).m_LocalPosition = { 0.0f, 1.7f, 3.0f };	// Eye height, otherwise the camera sits exactly in the grid's plane (y = 0) and sees it edge-on
	Registry.emplace<Components::FreeFlyMovementControllerComponent>(PlayerEntity);

	const entt::entity PlayerCameraEntity = GameScene.createEntity("Player Camera");
	Registry.emplace<Components::CameraComponent>(PlayerCameraEntity);
	GameScene.attachEntityToParent(PlayerCameraEntity, PlayerEntity);	// Local transform is identity, so the camera sits exactly at the player's eyes

	// A second camera, attached to the spinning totem to show off the "attach a camera to anything" part.
	// Its local transform is the offset from the totem: 3 units back, 1 up, looking slightly down at it.
	//const entt::entity TotemCameraEntity = GameScene.createEntity("Totem Camera");
	//auto& TotemCameraTransform = Registry.get<Components::TransformComponent>(TotemCameraEntity);
	//TotemCameraTransform.m_LocalPosition = { 0.0f, 1.0f, 3.0f };
	//TotemCameraTransform.m_LocalRotation = glm::angleAxis(glm::radians(-15.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	//Registry.emplace<Components::CameraComponent>(TotemCameraEntity, 75.0f);
	//GameScene.attachEntityToParent(TotemCameraEntity, TotemEntity);

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

	// QUARRY: LiDAR scan -> ground triangulation + B-spline surface, with balls rolling on it (MAT301 chapters 9 and 12)
	Simulation::QuarryRollingBallDemo QuarryDemo(renderer, GameScene);
	if (QuarryDemo.loadTerrain(Vulkanised::Utilities::File::getRuntimeAssetsDirectory() / "Terrain" / "laz_mesh.obj"))
	{
		glm::vec3 SuggestedCameraPosition{ 0.0f };
		glm::quat SuggestedCameraRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		QuarryDemo.calculateSuggestedCameraPlacement(SuggestedCameraPosition, SuggestedCameraRotation);
		auto& PlayerTransform = Registry.get<Components::TransformComponent>(PlayerEntity);
		PlayerTransform.m_LocalPosition = SuggestedCameraPosition;
		PlayerTransform.m_LocalRotation = SuggestedCameraRotation;
		Registry.get<Components::FreeFlyMovementControllerComponent>(PlayerEntity).m_MovementSpeedInUnitsPerSecond = 12.0f;	// The quarry is ~100 m long

		QuarryDemo.setPlayerCameraEntity(PlayerCameraEntity);
		if (ShouldStartOnBSplineSurface) { QuarryDemo.setRollingSurface(Simulation::RollingSurfaceChoice::BSplineSurface); }
		if (ShouldStartWithChaseCamera) { QuarryDemo.setCameraFollowsTrackedBall(true); }
	}

	LOG("Controls: left click to capture the mouse, Escape to release it, WASD/QE to move, Shift to sprint. Settings are in the ImGui window.");// , C to switch camera.");

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
		QuarryDemo.drawPhysicsWindow(glm::vec3(Registry.get<Components::WorldTransformComponent>(PlayerCameraEntity).m_WorldTransformMatrix[3]));

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
			const auto ScreenshotTimestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			renderer.requestScreenshot(Vulkanised::Utilities::File::getExecutableDirectory() / "Screenshots" / ("Hephaestian_" + std::to_string(ScreenshotTimestamp) + ".bmp"));
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

		// 3. PHYSICS (balls on the quarry), then the ECS SYSTEMS (movement, spinning, transform hierarchy)
		QuarryDemo.update(DeltaTimeInSeconds);
		GameScene.updateSimulation(Input, DeltaTimeInSeconds);

		// 4. BUILD RENDER PACKET + SUBMIT TO RENDERER (active camera, then one instance per submesh of every model, then the UI on top)
		GameScene.renderToRenderer(renderer);
	}

	// GLFW gets terminated by the renderer's cleanup, so let go of the window's callbacks before that
	Input.detachFromWindow();
	renderer.cleanup();

	return EXIT_SUCCESS;
}
