#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "io.hpp"

struct ProjectionViewMat {
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
	alignas(16) glm::mat4 viewInv;
	alignas(16) glm::mat4 projInv;
};

class Camera {
public:
	void createBuffers(const VmaAllocator &allocator) 
	{
		VkDeviceSize bufferSize = sizeof(ProjectionViewMat);
		
		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = bufferSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(allocator, &bufferCreateInfo, &allocCreateInfo, &uniformBuffer, &uniformBuffersAllocation, &uniformBuffersAllocationInfo) != VK_SUCCESS)
			throw std::runtime_error("Failed to create uniform buffers!");

		if (uniformBuffersAllocationInfo.pMappedData == nullptr)
			throw std::runtime_error("Failed to map meory for uniform buffer!");
		
	}

	VkDescriptorBufferInfo getDescriptorBufferInfo() const 
	{
		if (uniformBuffer == VK_NULL_HANDLE)
			throw std::runtime_error("Uniform buffer for projection and view matrix un-initialized");

		VkDescriptorBufferInfo info = { uniformBuffer, 0, sizeof(ProjectionViewMat) };
		return info;
	}

	void cleanUp(const VmaAllocator &allocator) 
	{
		vmaDestroyBuffer(allocator, uniformBuffer, uniformBuffersAllocation);
	}

	void updateProjViewMat(IO &io, uint32_t screenWidth, uint32_t screenHeight) 
	{

		projViewMat.view = getViewMatrix(io);
		projViewMat.proj = glm::perspective(glm::radians(fovy), screenWidth / (float)screenHeight, 0.1f, 10.0f);
		projViewMat.proj[1][1] *= -1;

		projViewMat.viewInv = glm::inverse(projViewMat.view);
		projViewMat.projInv = glm::inverse(projViewMat.proj);

		memcpy(uniformBuffersAllocationInfo.pMappedData, &projViewMat, sizeof(projViewMat));
	}

	Camera() 
	{
		cameraPosition = glm::vec3(0.0f, 5.0f, 0.0f);
		cameraFocus = glm::vec3(0.0f, 0.0f, 0.0f);
		cameraUp = glm::vec3(0.0f, 0.0f, 1.0f);

		cPlane = chooseCameraPlane(cameraUp);

		setCoordinateSystem();
	}

	void setCamera(const glm::vec3& cameraPosition, const glm::vec3& cameraFocus, const glm::vec3& cameraUp, float fovy = 45.0f)
	{
		this->cameraPosition = cameraPosition;
		this->cameraFocus = cameraFocus;
		this->cameraUp = cameraUp;

		this->fovy = fovy;

		cPlane = chooseCameraPlane(cameraUp);

		setCoordinateSystem();
	}

	void setCamera(const std::array<float, 16> &viewMat, float focusDistance = 1.0f, float fovy = 45.0f)
	{
		this->cameraPosition = glm::vec3(viewMat[3], viewMat[7], viewMat[11]);
		this->cameraFocus = this->cameraPosition + glm::vec3(focusDistance*viewMat[8], focusDistance*viewMat[9], focusDistance*viewMat[10]);
		this->cameraUp = glm::vec3(viewMat[4], viewMat[5], viewMat[6]);
		
		this->fovy = fovy;

		cPlane = chooseCameraPlane(cameraUp);

		setCoordinateSystem();
	}

	void setAngleIncrement(float value) 
	{
		angleIncrement = value;
	}

	void setDistanceIncrement(float value)
	{
		distanceIncrement = value;
	}

private:
	enum MOUSE_DRAG { NO_DRAG = 0, DRAG_LEFT = 1, DRAG_DOWN = 2, DRAG_UP = 4, DRAG_RIGHT = 8 };
	enum SCROLL_ZOOM { NO_ZOOM = 0, ZOOM_IN = 1, ZOOM_OUT = 2 };
	enum MOVEMENT { NO_MOVEMENT = 0, MOVE_FORWARD = 1, MOVE_BACK = 2, MOVE_RIGHT = 4, MOVE_LEFT = 8 };

	enum CAMERA_PLANE { XY = 0, XZ, ZY };

	ProjectionViewMat projViewMat;

	VkBuffer uniformBuffer = VK_NULL_HANDLE;
	VmaAllocation uniformBuffersAllocation;
	VmaAllocationInfo uniformBuffersAllocationInfo;

	glm::vec3 cameraPosition;
	glm::vec3 cameraFocus;

	glm::vec3 cameraFront;
	glm::vec3 cameraUp;
	glm::vec3 cameraRight;

	uint32_t selectCamera = 0;

	float fovy = 45.0f;

	float angleIncrement = 0.001f;
	float distanceIncrement = 0.001f;

	CAMERA_PLANE cPlane = XY;
	
	glm::mat4 getViewMatrix(IO& io) {
		glm::mat4 view(1.0f);

		switch (switchCamera(io)) {
		case 0:
			trackBallCamera(io);
			break;
		case 1:
			firstPersonCamera(io);
			break;
		}

		setView(view);

		return view;
	}

	void setView(glm::mat4 &view) {
		view[0][0] = cameraRight.x;
		view[1][0] = cameraRight.y;
		view[2][0] = cameraRight.z;
		view[0][1] = cameraUp.x;
		view[1][1] = cameraUp.y;
		view[2][1] = cameraUp.z;
		view[0][2] = -cameraFront.x;
		view[1][2] = -cameraFront.y;
		view[2][2] = -cameraFront.z;
		view[3][0] = -dot(cameraRight, cameraPosition);
		view[3][1] = -dot(cameraUp, cameraPosition);
		view[3][2] = dot(cameraFront, cameraPosition);
	}

	void trackBallCamera(IO &io) {
		// rotate camera position around the point camera focus.
		
		uint32_t drag = mouseDrag(io);
		uint32_t zoom = mouseZoom(io);

		if (drag != NO_DRAG) {
			float thetaVertical = drag & DRAG_UP ? angleIncrement : drag & DRAG_DOWN ? -angleIncrement : 0.0f;
			float thetaHorizontal = drag & DRAG_RIGHT ? angleIncrement : drag & DRAG_LEFT ? -angleIncrement : 0.0f;

			float length = glm::length(cameraPosition - cameraFocus);

			// move camera position in a circle (centered at camera focus ) embedded on the vertical plane defined by axis cameraUp and cameraFront
			cameraPosition = cameraPosition + cameraUp * length * std::sin(thetaVertical)
				+ cameraFront * length * (1.0f - std::cos(thetaVertical));

			// move camera position in a circle (centered at camera focus ) embedded on the horizontal plane defined by axis cameraRight and cameraFront
			cameraPosition = cameraPosition + cameraRight * length * std::sin(thetaHorizontal)
				+ cameraFront * length * (1.0f - std::cos(thetaHorizontal));

			setCoordinateSystem();
		}

		if (zoom != NO_ZOOM) {
			float zoomVal = zoom & ZOOM_IN ? distanceIncrement * 10 : zoom & ZOOM_OUT ? -distanceIncrement * 10 : 0.0f;
			cameraPosition = cameraPosition + cameraFront * zoomVal;
		}
	}

	void firstPersonCamera(const IO &io) {
		// roate camera foucs point and translate (camera position and camera Focus)
		uint32_t drag = mouseDrag(io);
		uint32_t movement = keyboardMovement(io);

		if (drag != NO_DRAG) {
			float thetaVertical = drag & DRAG_UP ? angleIncrement : drag & DRAG_DOWN ? -angleIncrement : 0.0f;
			float thetaHorizontal = drag & DRAG_RIGHT ? angleIncrement : drag & DRAG_LEFT ? -angleIncrement : 0.0f;

			float length = glm::length(cameraFocus - cameraPosition);
			
			// move camera focus in a circle (centered at camera position ) embedded on the vertical plane defined by axis cameraUp and cameraFront
			cameraFocus = cameraFocus + cameraUp * length * std::sin(thetaVertical)
				+ cameraFront * length * (std::cos(thetaVertical) - 1.0f);

			// move camera focus in a circle (centered at camera position ) embedded on the horzontal plane defined by axis cameraRight and cameraFront
			cameraFocus = cameraFocus + cameraRight * length * std::sin(thetaHorizontal)
				+ cameraFront * length * (std::cos(thetaHorizontal) - 1.0f);

			setCoordinateSystem();
		}

		if (movement != NO_MOVEMENT) {	// translate camera position and camera focus
			float forward = movement & MOVE_FORWARD ? distanceIncrement : movement & MOVE_BACK ? -distanceIncrement : 0.0f;
			float strafe = movement & MOVE_RIGHT ? distanceIncrement : movement & MOVE_LEFT ? -distanceIncrement : 0.0f;

			glm::vec3 delta = forward * cameraFront + strafe * cameraRight;
			cameraFocus = cameraFocus + delta;
			cameraPosition = cameraPosition + delta;
		}
	}

	static CAMERA_PLANE chooseCameraPlane(const glm::vec3 up)
	{	
		float x = abs(up.x);
		float y = abs(up.y);
		float z = abs(up.z);

		if (x > y && x > z)
			return ZY;
		else if (y > z)
			return XZ;
			
		return XY;
	}

	void setCoordinateSystem() 
	{
		cameraFront = glm::normalize(cameraFocus - cameraPosition);
		
		if (cPlane == XY) {
			// restrict cameraRight to xy plane
			cameraRight.x = -cameraFront.y;
			cameraRight.y = cameraFront.x;
			cameraRight.z = 0.0f;
		}
		else if (cPlane == XZ) {
			// restrict cameraRight to xz plane
			cameraRight.x = cameraFront.z;
			cameraRight.y = 0.0f;
			cameraRight.z = -cameraFront.x;
		}
		else {
			// restrict cameraRight to yz plane
			cameraRight.x = 0.0f;
			cameraRight.y = cameraFront.z;
			cameraRight.z = -cameraFront.y;
		}

		cameraRight = glm::normalize(cameraRight);

		//cameraRight = glm::normalize(glm::cross(cameraFront, cameraUp));
		cameraRight = glm::dot(glm::cross(cameraFront, cameraUp), cameraRight) > 0 ? cameraRight : -cameraRight;
		cameraUp = glm::normalize(glm::cross(cameraRight, cameraFront));
	}

	uint32_t switchCamera(const IO &io) 
	{
		int key, action;
		io.getKeyboardInput(key, action);
		static int lastAction = 0;
		if (action == GLFW_RELEASE && key == GLFW_KEY_C && lastAction == GLFW_PRESS)
			selectCamera++;

		lastAction = action;
		return selectCamera % 2;
	}

	uint32_t mouseDrag(const IO &io) 
	{
		int key, action;
		io.getMouseInput(key, action);
		static double lastPosY, lastPosX;
		
		if (key == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
			uint32_t drag = NO_DRAG;
			
			double posX, posY;
			io.getMouseCursorPos(posX, posY);
			double diffX = posX - lastPosX;
			double diffY = lastPosY - posY;
			drag |= diffX > 1e-4 ? DRAG_RIGHT : diffX < -1e-4 ? DRAG_LEFT : NO_DRAG;
			drag |= diffY > 1e-4 ? DRAG_UP : diffY < -1e-4 ? DRAG_DOWN : NO_DRAG;
			
			lastPosX = posX;
			lastPosY = posY;

			return drag;
		}
		else {
			lastPosX = 0.0;
			lastPosY = 0.0;
		}

		return NO_DRAG;
	}

	uint32_t mouseZoom(IO &io) 
	{
		double scrollOffset;
		io.getMouseScrollOffset(scrollOffset);

		return scrollOffset > 0 ? ZOOM_OUT : scrollOffset < 0 ? ZOOM_IN : NO_ZOOM;
	}

	uint32_t keyboardMovement(const IO &io) 
	{
		uint32_t movement = NO_MOVEMENT;
		int key, action;
		io.getKeyboardInput(key, action);
		bool pressed = action == GLFW_PRESS || action == GLFW_REPEAT;
		if (pressed) {
			movement |= key == GLFW_KEY_W ? MOVE_FORWARD : key == GLFW_KEY_S ? MOVE_BACK : NO_MOVEMENT;
			movement |= key == GLFW_KEY_D ? MOVE_RIGHT : key == GLFW_KEY_A ? MOVE_LEFT : NO_MOVEMENT;
		}

		return movement;
	}
};