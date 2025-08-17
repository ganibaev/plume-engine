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

constexpr float DRAW_DISTANCE = 250000.0f;


namespace Plume
{

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
		UpdateCameraVectors();
	}

	Camera(glm::vec3 position, glm::vec3 up, float yaw = YAW, float pitch = PITCH)
		: _front(glm::vec3(0.0f, 0.0f, -1.0f)), _movementSpeed(SPEED), _mouseSensitivity(SENSITIVITY), _zoom(ZOOM), _position(position), _worldUp(up), _yaw(yaw), _pitch(pitch)
	{
		UpdateCameraVectors();
	}

	Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
		: _front(glm::vec3(0.0f, 0.0f, -1.0f)), _movementSpeed(SPEED), _mouseSensitivity(SENSITIVITY), _zoom(ZOOM), _position(glm::vec3(posX, posY, posZ)), _worldUp(glm::vec3(upX, upY, upZ)), _yaw(yaw), _pitch(pitch)
	{
		UpdateCameraVectors();
	}

	const glm::mat4 GetViewMatrix() const
	{
		return glm::lookAt(_position, _position + _front, _up);
	}

	void ProcessMovement(CameraMovement direction, float timeDelta);
	void ProcessCameraMotion(float xOffset, float yOffset, bool constrainPitch = true);
	void ProcessMouseScroll(float yOffset);

	bool IsMovementActive(CameraMovement movement) const { return _activeMovements[static_cast<size_t>(movement)]; }
	void SetMovementStatus(CameraMovement movement, bool isActive) { _activeMovements[static_cast<size_t>(movement)] = isActive; }

	CameraDataGPU MakeGPUCameraData(const Camera& lastFrameCamera, WindowExtent windowExtent) const;
private:
	std::array<bool, static_cast<size_t>(CameraMovement::MAX_ENUM)> _activeMovements = { false };

	void UpdateCameraVectors();
};

} // namespace Plume
