#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class IO {
public:
	static std::vector<const char*> getRequiredExtensions() {
		if (!glfwInitialized)
			throw std::runtime_error("failed to initialize glfw!");

		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

		return extensions;
	}

	void init(int width, int height) {
		if (glfwInit() != GLFW_TRUE)
			throw std::runtime_error("failed to initialize glfw!");

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);
		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
		glfwSetKeyCallback(window, keyboardCallback);
		glfwSetMouseButtonCallback(window, mouseButtonCallback);
		glfwSetScrollCallback(window, mouseScrollCallback);

		glfwInitialized = true;
	}

	void createSurface(const VkInstance &instance, VkSurfaceKHR &surface) {
		if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
			throw std::runtime_error("failed to create window surface!");
		}
	}

	inline void getFramebufferSize(int &width, int &height) {
		width = 0, height = 0;
		while (width == 0 || height == 0) {
			glfwGetFramebufferSize(window, &width, &height);
			glfwWaitEvents();
		}
	}

	inline bool isFramebufferResized(bool reset) {
		bool retVal = framebufferResized;
		framebufferResized = reset ? false : framebufferResized;
		return retVal;
	}
    
	inline void getKeyboardInput(int &key, int &action) const {
		key = kbKey;
		action = kbAction;
	}

	inline void getMouseInput(int &key, int &action) const {
		key = muKey;
		action = muAction;
	}

	inline void getMouseCursorPos(double &xpos, double &ypos) const {
		glfwGetCursorPos(window, &xpos, &ypos);
	}

	inline void getMouseScrollOffset(double &scrollOffset) {
		scrollOffset = muScrollOffset;
		muScrollOffset = 0.0;
	}

	inline int getLastKeyState(int key) const {
		return glfwGetKey(window, key);
	}

	inline int windowShouldClose() {
		return glfwWindowShouldClose(window);
	}

	inline void pollEvents() {
		glfwPollEvents();
	}

	void terminate() {
		glfwDestroyWindow(window);
		glfwTerminate();
	}
private:
	static bool glfwInitialized;
	GLFWwindow * window;

	bool framebufferResized = false;

	int kbKey;
	int kbAction;

	int muKey;
	int muAction;

	double muScrollOffset = 0;
	
	static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
		auto app = reinterpret_cast<IO *>(glfwGetWindowUserPointer(window));
		app->framebufferResized = true;
	}

	static void keyboardCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
		auto app = reinterpret_cast<IO *>(glfwGetWindowUserPointer(window));
		app->kbKey = key;
		app->kbAction = action;
	}

	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
		auto app = reinterpret_cast<IO *>(glfwGetWindowUserPointer(window));
		app->muKey = button;
		app->muAction = action;
	}

	static void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
		auto app = reinterpret_cast<IO *>(glfwGetWindowUserPointer(window));
		app->muScrollOffset = yoffset;
	}
};
