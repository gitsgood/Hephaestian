#include <Vulkanised/Logger.h>
#include <Vulkanised/VulkanRenderer.h>

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
		constexpr const char* message{ "Failed to create GLFW window" };
		LOGE(message);
		throw std::runtime_error(message);
	}

	// Wrap it in a shared_ptr with a custom deleter
	// We pass glfwDestroyWindow as the function to call when the ref count hits 0
	window = std::shared_ptr<GLFWwindow>(rawWindow, glfwDestroyWindow);
}

auto main() -> int
{
	LOGH("Bruh");

	// Create window
	initWindow("Test Window", 1366, 768);

	// Create Vulkan renderer instance and initialize it with the window
	if (renderer.init(window.load().get()) == EXIT_FAILURE)
	{
		printf("Failed to initialize Vulkan renderer\n");
		return EXIT_FAILURE;
	}

	// --- ASSET LOADING PHASE ---
	std::vector<Vulkanised::Vertex> meshVertices1 = {
		{ { -0.4, 0.4, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },		// 0
		{ { -0.4, -0.4, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },		// 1
		{ { 0.4, -0.4, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },		// 2
		{ { 0.4, 0.4, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },		// 3
	};
	std::vector<uint32_t> meshIndices1 = { 0, 1, 2, 2, 3, 0 };
	uint32_t tex1 = renderer.createTexture("bibi.jpg");
	uint32_t mesh1 = renderer.loadMesh(meshVertices1, meshIndices1, tex1);

	std::vector<Vulkanised::Vertex> meshVertices2 = {
		{ { -0.25, 0.6, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } },		// 0
		{ { -0.25, -0.6, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },		// 1
		{ { 0.25, -0.6, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },		// 2
		{ { 0.25, 0.6, 0.0 },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 1.0f} , { 0.0f, 0.0f, 1.0f } },		// 3
	};
	uint32_t tex2 = renderer.createTexture("trump.jpg");
	uint32_t mesh2 = renderer.loadMesh(meshVertices2, meshIndices1, tex2);

	float angle = 0.0f;
	float lastTime = 0.0f;

	// --- GAME LOOP ---
	while (!glfwWindowShouldClose(window.load().get()))
	{
		glfwPollEvents();

		float now = static_cast<float>(glfwGetTime());
		float deltaTime = now - lastTime;
		lastTime = now;

		// 1. ECS LOGIC / SYSTEMS MATH
		angle += 10.0f * deltaTime;
		if (angle > 360.0f) { angle -= 360.0f; }

		glm::mat4 firstModel = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 0.0f, -1.0f));
		firstModel = glm::rotate(firstModel, glm::radians(angle), glm::vec3(0.0f, 0.0f, 1.0f));

		glm::mat4 secondModel = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, -3.0f));
		secondModel = glm::rotate(secondModel, glm::radians(-angle * 10), glm::vec3(0.0f, 0.0f, 1.0f));

		// 2. BUILD RENDER PACKET
		// An ECS render system would loop over entities with (Mesh, Transform) components and build this list.
		std::vector<Vulkanised::RenderInstance> packet;
		packet.push_back({ mesh1, tex1, firstModel });
		packet.push_back({ mesh2, tex2, secondModel });

		// Push 10,000 more instances here with zero Vulkan overhead!
		// for(int i=0; i<10000; i++) { packet.push_back({ mesh1, tex1, RandomTransform() }); }

		// 3. SUBMIT TO RENDERER
		renderer.SubmitFrame(packet);
	}

	renderer.cleanup();

	return EXIT_SUCCESS;
}