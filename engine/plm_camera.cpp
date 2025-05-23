#include "plm_camera.h"

#include <SDL.h>
#include <SDL_vulkan.h>


void PlumeCamera::process_movement(CameraMovement direction, float timeDelta)
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


void PlumeCamera::process_camera_motion(float xOffset, float yOffset, bool constrainPitch /* = true */)
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


void PlumeCamera::process_mouse_scroll(float yOffset)
{
	_zoom -= yOffset * 2;
	_zoom = glm::clamp(_zoom, 1.0f, 70.0f);
}


CameraDataGPU PlumeCamera::make_gpu_camera_data(const PlumeCamera& lastFrameCamera, WindowExtent windowExtent) const
{
	const glm::mat4 view = get_view_matrix();

	glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(_zoom),
		windowExtent.width / static_cast<float>(windowExtent.height), 0.1f, DRAW_DISTANCE);
	projection[1][1] *= -1;

	CameraDataGPU resCameraData = {};
	resCameraData.view = view;
	resCameraData.invView = glm::inverse(view);
	resCameraData.proj = projection;
	resCameraData.viewproj = projection * view;
	resCameraData.invProj = glm::inverse(projection);
	resCameraData.invViewProj = glm::inverse(resCameraData.viewproj);

	glm::mat4 prevView = lastFrameCamera.get_view_matrix();
	glm::mat4 prevProjection = glm::perspectiveRH_ZO(glm::radians(lastFrameCamera._zoom),
		windowExtent.width / static_cast<float>(windowExtent.height), 0.1f, DRAW_DISTANCE);
	prevProjection[1][1] *= -1;

	resCameraData.prevViewProj = prevProjection * prevView;

	return resCameraData;
}


void PlumeCamera::update_camera_vectors()
{
	glm::vec3 front = {};
	front.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
	front.y = sin(glm::radians(_pitch));
	front.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));
	_front = glm::normalize(front);

	_right = glm::normalize(glm::cross(_front, _worldUp));
	_up = glm::normalize(glm::cross(_right, _front));
}
