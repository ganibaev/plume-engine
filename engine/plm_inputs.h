#pragma once

#include <array>

#include "plm_camera.h"
#include <SDL.h>
#include <SDL_vulkan.h>


namespace Plume
{

class InputManager
{
public:
	static constexpr uint32_t MAX_FRAME_EVENT_NUM = 32;

	enum class EventType
	{
		eNone,
		eQuit,
		eMouseMotion,
		eMovement,
		eMovementStop,
		eZoom,
		eDebugWindow,
		eDefocusMode
	};

	struct Event
	{
		EventType type = EventType::eNone;

		union {
			SDL_Keycode keycode;
			float scrollY;
		};
	};

	void SetCamera(Plume::Camera camera) { _camera = camera; }
	const Plume::Camera* GetPCamera() const { return &_camera; }

	const std::array<Event, MAX_FRAME_EVENT_NUM>& GetEventQueue() const { return _eventQueue; }

	void PollEvents();

	bool ShouldQuit() const { return _shouldQuit; }

	static CameraMovement SDLKeyToMovement(SDL_Keycode sym);

private:
	void ClearEventQueue();
	void ProcessGeneralQueueEvents();

	void OnMouseMotionCallback();
	void OnMouseScrollCallback(float yOffset);

	void ProcessMovement();

	void OnKeyboardEventCallback(SDL_Keycode sym, bool keyDown);

	constexpr static float _camSpeed = 0.2f;

	float _deltaTime = 0.0f;
	float _lastFrameTime = 0.0f;

	bool _defocusMode = false;
	bool _shouldQuit = false;

	Plume::Camera _camera = Plume::Camera(glm::vec3(2.8f, 6.0f, 40.0f));
	std::array<Event, MAX_FRAME_EVENT_NUM> _eventQueue;
};

} // namespace Plume
