#include "Vulkanised/VulkanRenderer.h"

namespace Vulkanised
{
	// Little trick I stole from Rust. Ideally this should be immutable and thread-safe, but at least it is supposed to be somehow thread-safe now. This might come in handy down the line.
	VAtomic<GLFWwindow> window;
	VulkanRenderer renderer;

	static void initWindow(std::string_view wName = "Test Window", const int width = 800, const int height = 600)
	{
		if (glfwInit() == GLFW_FALSE)
		{
			constexpr const char* message { "Failed to initialize GLFW" };
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
			constexpr const char* message { "Failed to create GLFW window" };
			LOGE(message);
			throw std::runtime_error(message);
		}

		// Wrap it in a shared_ptr with a custom deleter
		// We pass glfwDestroyWindow as the function to call when the ref count hits 0
		window = std::shared_ptr<GLFWwindow>(rawWindow, glfwDestroyWindow);
	}
}

int main()
{
	using namespace Vulkanised;

	// Set the initial timestamp before anything
	Utilities::Time::getStartTime();
	//Utilities::Time::timeIt("getStartTime", Utilities::Time::getStartTime);

	// Initialises the logger (necessary, since it uses lazy initialisation), while also "announcing" the initialisation of the program.
	LOGH("Vulkanised hath started!");
	//Utilities::Time::timeIt("Initialising logger and logging", [&]() { Logger::getLoggerInstance()->logText(Logger::LogType::HIGHLIGHT, std::source_location::current(), "Vulkanised hath started!"); });

	// Create window
	initWindow("Test Window", 1366, 768);

	// Create Vulkan renderer instance and initialize it with the window
	if (renderer.init(window.load().get()) == EXIT_FAILURE)
	{
		printf("Failed to initialize Vulkan renderer\n");
		return EXIT_FAILURE;
	}

	float angle{ 0.0f };
	float deltaTime{ 0.0f };
	float lastTime{ 0.0f };

	// Loop until closed
	while (!glfwWindowShouldClose(window.load().get()))
	{
		glfwPollEvents();

		float now{ static_cast<float>(glfwGetTime()) };
		deltaTime = now - lastTime;
		lastTime = now;

		angle += 10.0f * deltaTime;
		if (angle > 360.0f) { angle -= 360.0f; }
		
		glm::mat4 firstModel(1.0f);
		glm::mat4 secondModel(1.0f);

		firstModel = glm::translate(firstModel, glm::vec3(-1.0f, 0.0f, -1.0f));
		firstModel = glm::rotate(firstModel, glm::radians(angle), glm::vec3(0.0f, 0.0f, 1.0f));

		secondModel = glm::translate(secondModel, glm::vec3(1.0f, 0.0f, -3.0f));
		secondModel = glm::rotate(secondModel, glm::radians(-angle * 10), glm::vec3(0.0f, 0.0f, 1.0f));

		renderer.updateModel(0, firstModel);
		renderer.updateModel(1, secondModel);

		//Utilities::Time::timeIt("Drawing", [&](){renderer.draw();});
		renderer.draw();
	}

	renderer.cleanup();

	return EXIT_SUCCESS;
}