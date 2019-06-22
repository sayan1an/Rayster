#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "io.hpp"

class Camera {
public:
	glm::mat4 getViewMatrix(const IO &io) {
		glm::mat4 view;

		trackBallCamera();
		setView(view);

		return view;
	}

	Camera() {
		cameraPosition = glm::vec3(2.0f, 2.0f, 2.0f);
		cameraFocus = glm::vec3(0.0f, 0.0f, 0.0f);
		cameraUp = glm::vec3(0.0f, 0.0f, 1.0f);

		setCoordinateSystem();
	}
private:
	uint32_t selectCamera = 0;
	glm::vec3 cameraPosition;
	glm::vec3 cameraFocus;

	glm::vec3 cameraFront;
	glm::vec3 cameraUp;
	glm::vec3 cameraRight;

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

	void trackBallCamera() {
		// rotate camera position around the point camera focus.
		float thetaVertical = 0.000f;
		float thetaHorizontal = 0.001f;

		float length = glm::length(cameraPosition - cameraFocus);

		{
			// move camera position in a circle embedded on the vertical plane defined by axis cameraUp and cameraFront
			cameraPosition = cameraPosition + cameraUp * length * std::sin(thetaVertical)
				+ cameraFront * length * (1.0f - std::cos(thetaVertical));
		}
		{	
			// move camera position in a circle embedded on the horizontal plane defined by axis cameraRight and cameraFront
			cameraPosition = cameraPosition + cameraRight * length * std::sin(thetaHorizontal)
				+ cameraFront * length * (1.0f - std::cos(thetaHorizontal));
		}

		setCoordinateSystem();
	}

	void firstPersonCamera() {
		// roate camera foucs point and translate (camera position and camera Focus)
		float forward = 0;
		float strafe = 0;

		float thetaVertical = 0;
		float thetaHorizontal = 0;

		{	// translate camera position and camera focus
			glm::vec3 delta = forward * cameraFront + strafe * cameraRight;
			cameraFocus = cameraFocus + delta;
			cameraPosition = cameraPosition + delta;
		}
	}

	void setCoordinateSystem() {
		cameraFront = glm::normalize(cameraFocus - cameraPosition);
		{ // restrict cameraRight to xy plane
			cameraRight.x = -cameraFront.y;
			cameraRight.y = cameraFront.x;
			cameraRight.z = 0.0f;
			cameraRight = glm::normalize(cameraRight);

			cameraRight = glm::dot(glm::cross(cameraFront, cameraUp), cameraRight) > 0 ? cameraRight : -cameraRight;
		}
		cameraUp = glm::normalize(glm::cross(cameraRight, cameraFront));
	}

	void switchCamera(const IO &io) {
		int key, action;
		static int lastAction;
		io.getKeyboardInput(key, action);
		if (key == GLFW_KEY_C && action == GLFW_RELEASE && lastAction == GLFW_PRESS)
			selectCamera++;
	}
};