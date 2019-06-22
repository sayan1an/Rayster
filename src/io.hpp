#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class IO {
public:
	void initWindow(int width, int height) {
		glfwInit();

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		window = glfwCreateWindow(width, height, "Vulkan", nullptr, nullptr);
		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
		glfwSetKeyCallback(window, keyboardCallback);
		glfwSetMouseButtonCallback(window, mouseButtonCallback);
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
	GLFWwindow * window;

	bool framebufferResized = false;

	int kbKey;
	int kbAction;

	int muKey;
	int muAction;
	
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
};