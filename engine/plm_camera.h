#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include "shaders/host_device_common.h"


enum class CameraMovement
{
	NONE,
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT,
	UP,
	DOWN,
	MAX_ENUM
};

constexpr float YAW = -90.0f;
constexpr float PITCH = 0.0f;
constexpr float SPEED = 20.0f;
constexpr float SENSITIVITY = 0.1f;
constexpr float ZOOM = 70.0f;

constexpr float DRAW_DISTANCE = 6000.0f;


class PlumeCamera
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

	PlumeCamera(glm::vec3 position)
		: _front(glm::vec3(0.0f, 0.0f, -1.0f)), _movementSpeed(SPEED), _mouseSensitivity(SENSITIVITY), _zoom(ZOOM), _position(position)
	{
		update_camera_vectors();
	}

	PlumeCamera(glm::vec3 position, glm::vec3 up, float yaw = YAW, float pitch = PITCH)
		: _front(glm::vec3(0.0f, 0.0f, -1.0f)), _movementSpeed(SPEED), _mouseSensitivity(SENSITIVITY), _zoom(ZOOM), _position(position), _worldUp(up), _yaw(yaw), _pitch(pitch)
	{
		update_camera_vectors();
	}

	PlumeCamera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
		: _front(glm::vec3(0.0f, 0.0f, -1.0f)), _movementSpeed(SPEED), _mouseSensitivity(SENSITIVITY), _zoom(ZOOM), _position(glm::vec3(posX, posY, posZ)), _worldUp(glm::vec3(upX, upY, upZ)), _yaw(yaw), _pitch(pitch)
	{
		update_camera_vectors();
	}

	const glm::mat4 get_view_matrix() const
	{
		return glm::lookAt(_position, _position + _front, _up);
	}

	void process_movement(CameraMovement direction, float timeDelta);
	void process_camera_motion(float xOffset, float yOffset, bool constrainPitch = true);
	void process_mouse_scroll(float yOffset);

	bool is_movement_active(CameraMovement movement) const { return _activeMovements[static_cast<size_t>(movement)]; }
	void set_movement_status(CameraMovement movement, bool isActive) { _activeMovements[static_cast<size_t>(movement)] = isActive; }

	CameraDataGPU make_gpu_camera_data(const PlumeCamera& lastFrameCamera, WindowExtent windowExtent) const;
private:
	std::array<bool, static_cast<size_t>(CameraMovement::MAX_ENUM)> _activeMovements = { false };

	void update_camera_vectors();
};
