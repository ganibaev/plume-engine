#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vector>

enum class CameraMovement
{
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT,
	UP,
	DOWN
};

constexpr float YAW = -90.0f;
constexpr float PITCH = 0.0f;
constexpr float SPEED = 20.0f;
constexpr float SENSITIVITY = 0.1f;
constexpr float ZOOM = 70.0f;

class Camera
{
public:
	glm::vec3 _position{ 0.0f };
	glm::vec3 _front;
	glm::vec3 _up;
	glm::vec3 _right;
	glm::vec3 _worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

	float _yaw = YAW;
	float _pitch = PITCH;

	float _movementSpeed;
	float _mouseSensitivity;
	float _zoom;

	Camera(glm::vec3 position)
		: _front(glm::vec3(0.0f, 0.0f, -1.0f)), _movementSpeed(SPEED), _mouseSensitivity(SENSITIVITY), _zoom(ZOOM), _position(position)
	{
		update_camera_vectors();
	}

	Camera(glm::vec3 position, glm::vec3 up, float yaw = YAW, float pitch = PITCH)
		: _front(glm::vec3(0.0f, 0.0f, -1.0f)), _movementSpeed(SPEED), _mouseSensitivity(SENSITIVITY), _zoom(ZOOM), _position(position), _worldUp(up), _yaw(yaw), _pitch(pitch)
	{
		update_camera_vectors();
	}

	Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
		: _front(glm::vec3(0.0f, 0.0f, -1.0f)), _movementSpeed(SPEED), _mouseSensitivity(SENSITIVITY), _zoom(ZOOM), _position(glm::vec3(posX, posY, posZ)), _worldUp(glm::vec3(upX, upY, upZ)), _yaw(yaw), _pitch(pitch)
	{
		update_camera_vectors();
	}

	glm::mat4 get_view_matrix()
	{
		return glm::lookAt(_position, _position + _front, _up);
	}

	void process_keyboard(CameraMovement direction, float timeDelta);
	void process_camera_movement(float xOffset, float yOffset, bool constrainPitch = true);
	void process_mouse_scroll(float yOffset);
private:
	void update_camera_vectors();
};