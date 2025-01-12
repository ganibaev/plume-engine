#include "render_camera.h"

void Camera::process_keyboard(CameraMovement direction, float timeDelta)
{
	float velocity = _movementSpeed * timeDelta;
	switch (direction)
	{
	case CameraMovement::FORWARD:
		_position += _front * velocity;
		break;
	case CameraMovement::BACKWARD:
		_position -= _front * velocity;
		break;
	case CameraMovement::LEFT:
		_position -= _right * velocity;
		break;
	case CameraMovement::RIGHT:
		_position += _right * velocity;
		break;
	case CameraMovement::UP:
		_position += _up * velocity;
		break;
	case CameraMovement::DOWN:
		_position -= _up * velocity;
		break;
	default:
		break;
	}
}

void Camera::process_camera_movement(float xOffset, float yOffset, bool constrainPitch /* = true */)
{
	xOffset *= _mouseSensitivity;
	yOffset *= _mouseSensitivity;

	_yaw += xOffset;
	_pitch += yOffset;

	if (constrainPitch)
	{
		_pitch = glm::clamp(_pitch, -89.0f, 89.0f);
	}

	update_camera_vectors();
}

void Camera::process_mouse_scroll(float yOffset)
{
	_zoom -= yOffset * 2;
	_zoom = glm::clamp(_zoom, 1.0f, 70.0f);
}

void Camera::update_camera_vectors()
{
	glm::vec3 front = {};
	front.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
	front.y = sin(glm::radians(_pitch));
	front.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));
	_front = glm::normalize(front);

	_right = glm::normalize(glm::cross(_front, _worldUp));
	_up = glm::normalize(glm::cross(_right, _front));
}