#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "spline.h"
#include "cereal/archives/binary.hpp"
#include "cereal/types/vector.hpp"

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
		float timeDelta = io.getFrameTimes().back();

		if (keyFrames.isPlaying) {
			if (keyFrames.play(cameraPosition, cameraFocus, cameraUp, timeDelta)) {
				cPlane = chooseCameraPlane(cameraUp);
				setCoordinateSystem();
				setView(projViewMat.view);
			}
		}
		else if (io.isIoCaptured())
			setView(projViewMat.view); // for gui controls
		else {
			projViewMat.view = getViewMatrix(io); // for kbd-mouse controls
			uint32_t keyFrameContol = keyboardKeyframe(io);
			if (keyFrameContol == RECORD)
				keyFrames.addKeyFrame(cameraPosition, cameraFocus, cameraUp);
			else if (keyFrameContol == DEL)
				keyFrames.reset();
			else if (keyFrameContol == SAVE)
				keyFrames.saveKeyFrames(keyFrameFileName);
			else if (keyFrameContol == LOAD)
				keyFrames.loadKeyFrames(keyFrameFileName);
		}

		projViewMat.proj = glm::perspective(glm::radians(fovy), screenWidth / (float)screenHeight, 0.1f, 10.0f);
		projViewMat.proj[1][1] *= -1;

		projViewMat.viewInv = glm::inverse(projViewMat.view);
		projViewMat.projInv = glm::inverse(projViewMat.proj);
		
		memcpy(uniformBuffersAllocationInfo.pMappedData, &projViewMat, sizeof(projViewMat));

		keyFrames.tick(timeDelta);
	}

	void cameraWidget()
	{
		if (ImGui::CollapsingHeader("Camera controls"))
		{	
			if (ImGui::CollapsingHeader("Camera movement readme")) {
				ImGui::Text("Press C to toggle between camera modes");
				ImGui::Text("Trackball camera control- drag mouse to change viewpoint and scroll to zoom");
				ImGui::Text("First person camera control- drag mouse to change viewpoint and WSAD to move");
				ImGui::Text("Slected camera mode - "); ImGui::SameLine();
				ImGui::Text(selectCamera % 2 == 0 ? "Trackball" : "First person");
				ImGui::Spacing();
				ImGui::Spacing();
			}
			if (ImGui::CollapsingHeader("Camera keyframing")) {
				ImGui::Text("Press R to add a new keyframe");
				ImGui::Text("Press L to load keyframes from file - "); ImGui::SameLine(); ImGui::Text(keyFrameFileName.c_str());
				ImGui::Text("Press S to save keyframes to file - "); ImGui::SameLine(); ImGui::Text(keyFrameFileName.c_str());
				ImGui::Text("Press del to remove all keyframes");
				ImGui::Text("WallClock time - "); ImGui::SameLine(); ImGui::Text(std::to_string(keyFrames.getWallClock()).c_str());
				ImGui::Text("Keyframe added - "); ImGui::SameLine(); ImGui::Text(std::to_string(keyFrames.keyFrameCount()).c_str());
				ImGui::Text("Play keyframes"); ImGui::SameLine();
				ImGui::RadioButton("Yes", &keyFrames.isPlaying, 1); ImGui::SameLine();
				ImGui::RadioButton("No", &keyFrames.isPlaying, 0);
				ImGui::Text("Play time - "); ImGui::SameLine(); ImGui::Text(std::to_string(keyFrames.getPlayClock()).c_str());
				ImGui::Spacing();
				ImGui::Spacing();
			}
			{
				ImGui::RadioButton("Camera position", &guiData.option0, 0); ImGui::SameLine();
				ImGui::RadioButton("Camera focus", &guiData.option0, 1);
				float* data = guiData.option0 == 0 ? &cameraPosition[0] : guiData.option0 == 1 ? &cameraFocus[0] : &cameraUp[0];
				ImGui::SliderFloat("Slider scale##1", &guiData.sliderScale0, 0.1f, 100);
				ImGui::SliderFloat3(guiData.option0 == 0 ? "Camera position" : "Camera focus", data, -guiData.sliderScale0, guiData.sliderScale0);
				ImGui::Spacing();
				ImGui::Spacing();
			}
			{
				int option = cPlane == ZY ? 0 : cPlane == XZ ? 1 : 2;
				ImGui::Text("Camera up vector"); ImGui::SameLine();
				ImGui::RadioButton("X", &option, 0); ImGui::SameLine();
				ImGui::RadioButton("Y", &option, 1); ImGui::SameLine();
				ImGui::RadioButton("Z", &option, 2);
				cameraUp = option == 0 ? glm::vec3(1, 0, 0) : option == 1 ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
				ImGui::Spacing();
				ImGui::Spacing();
			}
			{
				ImGui::SliderFloat("Fov", &fovy, 10, 80);
				ImGui::Spacing();
				ImGui::Spacing();
			}
			{	
				ImGui::SliderFloat("Slider scale##2", &guiData.sliderScale1, 0.01f, 1); // ## is used to de-couple this slider from the last slider with same name
				ImGui::SliderFloat("Linear movement speed", &distanceIncrement, 0, guiData.sliderScale1);
				ImGui::SliderFloat("Angular movement speed", &angleIncrement, 0, guiData.sliderScale1);
				ImGui::Spacing();
				ImGui::Spacing();
			}
		
			cPlane = chooseCameraPlane(cameraUp);
			setCoordinateSystem();
		}
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
	enum KEYFRAME_CTRL {RECORD = 0, DEL, SAVE, LOAD, NO_ACTION};

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

	std::string keyFrameFileName = "default.bin";

	class KeyFrame
	{
	public:
		int isPlaying = 0;
						
		uint32_t addKeyFrame(const glm::vec3 &cameraPosition, const glm::vec3 &cameraFocus, const glm::vec3 &cameraUp) 
		{	
			for (const auto & t : time) {
				if (t > wallClock) {
					WARN(false, "Camera: Could not add keyframe key-frame time must be in ascending order");
					return static_cast<uint32_t>(time.size());
				}
			}

			time.push_back(wallClock);
			for (int i = 0; i < 3; i++) {
				camParams[i].push_back(static_cast<double>(cameraPosition[i]));
				camParams[3 + i].push_back(static_cast<double>(cameraFocus[i]));
				camParams[6 + i].push_back(static_cast<double>(cameraUp[i]));
			}

			setSpline();
			return static_cast<uint32_t>(time.size());
		}

		void tick(const float timeDelta)
		{	
			if (isPlaying == 0)
				wallClock += timeDelta;
		}
		
		uint64_t getWallClock() const 
		{
			return static_cast<uint64_t>(wallClock);
		}

		uint64_t getPlayClock() const
		{
			return static_cast<uint64_t>(playTime);
		}

		void saveKeyFrames(const std::string &filename) 
		{	
			if (time.size() < 2) {
				WARN_DBG_ONLY(false, "Camera: Could not save keframes - number of keyframes must be more than or equal to 2.");
				return;
			}

			std::ofstream os(filename, std::ios::binary);
			if (os.is_open()) {
				cereal::BinaryOutputArchive archive(os);

				archive(time, camParams[0], camParams[1], camParams[2],
					camParams[3], camParams[4], camParams[5],
					camParams[6], camParams[7], camParams[8], uniqueId);

				os.close();
			}
			else {
				WARN_DBG_ONLY(false, "Camera: Could not save keyframes to file - " + filename);
			}
		}

		void loadKeyFrames(const std::string &filename)
		{
			std::ifstream is(filename, std::ios::binary);
			if (is.is_open()) {
				cereal::BinaryInputArchive archive(is);
				uint64_t uId = 0;
				archive(time, camParams[0], camParams[1], camParams[2],
					camParams[3], camParams[4], camParams[5],
					camParams[6], camParams[7], camParams[8], uId);

				if (uId != uniqueId) {
					reset();
					WARN_DBG_ONLY(false, "Camera: Could not load keyframes from file - " + filename + ". Unique Id mismatch!");
				}

				is.close();
			}
			else {
				WARN_DBG_ONLY(false, "Camera: Could not load keyframes from file - " + filename);
			}
		}
		
		bool play(glm::vec3 &camPosition, glm::vec3& camFocus, glm::vec3 &camUp, const float timeDelta)
		{	
			if (time.size() >= 2) {
				for (int i = 0; i < 3; i++) {
					camPosition[i] = static_cast<float>(splines[i](playTime));
					camFocus[i] = static_cast<float>(splines[3 + i](playTime));
					camUp[i] = static_cast<float>(splines[6 + i](playTime));
				}

				if (delta > 0 && playTime > time.back())
					delta = -1.0;
				if (delta < 0 && playTime < 0)
					delta = 1.0f;

				playTime += timeDelta * delta;

				return true;
			}
					
			return false;
		}

		uint32_t keyFrameCount() const 
		{
			return static_cast<uint32_t>(time.size());
		}

		void reset() 
		{
			time.clear();
			for (uint32_t i = 0; i < 9; i++)
				camParams[i].clear();

			playTime = 0;
			wallClock = 0;
		}

	private:
		std::vector<double> time;
		std::array<std::vector<double>, 9> camParams;
		std::array<tk::spline, 9> splines;
		
		double playTime = 0;
		double delta = 1.0;
		double wallClock = 0;
		
		const uint64_t uniqueId = 0xf1e7ce;
		void setSpline()
		{
			if (time.size() >= 2) {
				for (int i = static_cast<int>(time.size()) - 1; i >= 0; i--)
					time[i] -= time[0];

				for (int i = 0; i < 9; i++)
					splines[i].set_points(time, camParams[i]);
			}
		}
		
	} keyFrames;

	struct GuiData 
	{
		int option0;
		int option1;
		float sliderScale0;
		float sliderScale1;
					
		GuiData() {
			option0 = 0;
			option1 = 0;
			sliderScale0 = 1.0f;
			sliderScale1 = 0.01f;
		}

	} guiData;
	
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

	uint32_t keyboardKeyframe(const IO& io)
	{
		uint32_t control = NO_ACTION;
		int key, action;
		io.getLastKeyState(key, action);
		if (key == GLFW_KEY_R && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
			io.getKeyboardInput(key, action);
			if (action == GLFW_RELEASE && key == GLFW_KEY_R)
				control = RECORD;
		}
		else if (key == GLFW_KEY_DELETE && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
			io.getKeyboardInput(key, action);
			if (action == GLFW_RELEASE && key == GLFW_KEY_DELETE)
				control = DEL;
		}
		else if (key == GLFW_KEY_S && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
			io.getKeyboardInput(key, action);
			if (action == GLFW_RELEASE && key == GLFW_KEY_S)
				control = SAVE;
		}
		else if (key == GLFW_KEY_L && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
			io.getKeyboardInput(key, action);
			if (action == GLFW_RELEASE && key == GLFW_KEY_L)
				control = LOAD;
		}
		
		return control;
	}
};