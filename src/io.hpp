#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <array>
#include <chrono>
#include <thread>
#include "imgui.h"
#include <string>

class IO {
public:
	static std::vector<const char*> getRequiredExtensions() 
	{
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

		for (size_t i = 0; i < frameTimes.size(); i++)
			frameTimes[i] = 0;

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
			//glfwWaitEvents();
		}
	}

	inline bool isFramebufferResized(bool reset) 
	{
		bool retVal = framebufferResized;
		framebufferResized = reset ? false : framebufferResized;
		return retVal;
	}
    
	inline void getKeyboardInput(int &key, int &action) const
	{
		key = kbKey;
		action = kbAction;
	}

	inline void getMouseInput(int &key, int &action) const 
	{
		key = muKey;
		action = muAction;
	}

	inline void getMouseCursorPos(double &xpos, double &ypos) const 
	{
		glfwGetCursorPos(window, &xpos, &ypos);
	}

	inline void getMouseScrollOffset(double &scrollOffset) 
	{
		scrollOffset = muScrollOffset;
	}

	inline int getLastKeyState(int key) const 
	{
		return glfwGetKey(window, key);
	}

	inline int getLastMouseBtnState(int key) const
	{
		return glfwGetMouseButton(window, key);
	}

	inline int windowShouldClose() 
	{
		return glfwWindowShouldClose(window);
	}

	inline void sleep(uint64_t milliseconds)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
	}

	inline void pollEvents() 
	{
		muScrollOffset = 0.0;
		glfwPollEvents();
		ioCaptured = false;
		
		avgFrameTime -= frameTimes[0];
		std::rotate(frameTimes.begin(), frameTimes.begin() + 1, frameTimes.end());
		using namespace std::chrono;
		microseconds ms = duration_cast<microseconds>(system_clock::now().time_since_epoch());
		uint64_t t = ms.count();
		frameTimes.back() = static_cast<float>(t - time) / 1000.0f;
		time = t;
		avgFrameTime += frameTimes[frameTimes.size() - 1];
	}

	void frameRateWidget(const float max = 20) const
	{	
		if (ImGui::CollapsingHeader("FPS monitor")) {
			float fpms = static_cast<float>(frameTimes.size()) / static_cast<float>(avgFrameTime);
			ImGui::Text(("FPS: " + std::to_string(static_cast<uint32_t>(std::floor(fpms * 1000)))).c_str());
			ImGui::PlotLines(std::to_string(static_cast<uint32_t>(max)).c_str(), &frameTimes[0], static_cast<int>(frameTimes.size()), 0, "Frame Times (ms)", 0, max, ImVec2(0, 50));
			ImGui::Spacing();
			ImGui::Spacing();
		}
	}

	const float getAvgFrameTime() const
	{
		return static_cast<float>(avgFrameTime) / static_cast<float>(frameTimes.size());
	}

	const std::array<float, 50> & getFrameTimes() const
	{
		return frameTimes;
	}

	inline void setIoCaptured()
	{
		ioCaptured = true;
	}

	inline bool isIoCaptured() const
	{
		return ioCaptured;
	}

	void terminate() 
	{
		glfwDestroyWindow(window);
		glfwTerminate();
	}
private:
	static bool glfwInitialized;
	GLFWwindow * window = nullptr;

	bool framebufferResized = false;

	int kbKey = 0;
	int kbAction = 0;

	int muKey = 0;
	int muAction = 0;

	double muScrollOffset = 0;

	bool ioCaptured = false;
	
	std::array<float, 50> frameTimes;
	uint64_t time = 0;
	double avgFrameTime = 10;

	static void framebufferResizeCallback(GLFWwindow* window, int width, int height) 
	{
		auto app = reinterpret_cast<IO *>(glfwGetWindowUserPointer(window));
		app->framebufferResized = true;
	}

	static void keyboardCallback(GLFWwindow* window, int key, int scancode, int action, int mods) 
	{
		auto app = reinterpret_cast<IO *>(glfwGetWindowUserPointer(window));
		app->kbKey = key;
		app->kbAction = action;
	}

	static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) 
	{
		auto app = reinterpret_cast<IO *>(glfwGetWindowUserPointer(window));
		app->muKey = button;
		app->muAction = action;
	}

	static void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset) 
	{
		auto app = reinterpret_cast<IO *>(glfwGetWindowUserPointer(window));
		app->muScrollOffset = yoffset;
	}
};
