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

	void set_camera(Plume::Camera camera) { _camera = camera; }
	const Plume::Camera* get_p_camera() const { return &_camera; }

	const std::array<Event, MAX_FRAME_EVENT_NUM>& get_event_queue() const { return _eventQueue; }

	void poll_events();

	bool should_quit() const { return _shouldQuit; }

	static CameraMovement sdl_key_to_movement(SDL_Keycode sym);

private:
	void clear_event_queue();
	void process_general_queue_events();

	void on_mouse_motion_callback();
	void on_mouse_scroll_callback(float yOffset);

	void process_movement();

	void on_keyboard_event_callback(SDL_Keycode sym, bool keyDown);

	constexpr static float _camSpeed = 0.2f;

	float _deltaTime = 0.0f;
	float _lastFrameTime = 0.0f;

	bool _defocusMode = false;
	bool _shouldQuit = false;

	Plume::Camera _camera = Plume::Camera(glm::vec3(2.8f, 6.0f, 40.0f));
	std::array<Event, MAX_FRAME_EVENT_NUM> _eventQueue;
};

} // namespace Plume
