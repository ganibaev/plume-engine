#include "plm_inputs.h"
#include "imgui_impl_sdl3.h"


void Plume::InputManager::poll_events()
{
	float curFrameTime = static_cast<float>(SDL_GetTicks() / 1000.0f);
	_deltaTime = curFrameTime - _lastFrameTime;
	_lastFrameTime = curFrameTime;

	SDL_Event e;
	bool bQuit = false;
	bool keyDown = false;
	bool mouseMotion = false;
	bool mouseWheel = false;
	float scrollY = 0.0f;
	SDL_Keycode sym = 0;

	uint32_t queueId = 0;

	// Handle events on queue
	clear_event_queue();
	while (SDL_PollEvent(&e) != 0)
	{
		if (queueId == MAX_FRAME_EVENT_NUM)
		{
			break;
		}

		Event& keyEvent = _eventQueue[queueId];

		// SDL window callback processing
		switch (e.type)
		{
		case SDL_EVENT_QUIT:
			keyEvent.type = EventType::eQuit;
			break;
		case SDL_EVENT_MOUSE_MOTION:
			keyEvent.type = EventType::eMouseMotion;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			keyEvent.type = EventType::eZoom;
			keyEvent.scrollY = e.wheel.y;
			break;
		case SDL_EVENT_KEY_DOWN:
			sym = e.key.keysym.sym;
			keyEvent.keycode = sym;

			if (sym == SDLK_F8 || sym == SDLK_F2)
			{
				keyEvent.type = EventType::eDefocusMode;

				if (sym == SDLK_F2)
				{
					keyEvent.type = EventType::eDebugWindow;
				}

				break;
			}
			else if (sym == SDLK_ESCAPE)
			{
				keyEvent.type = EventType::eQuit;
				break;
			}

			keyEvent.type = EventType::eMovement;
			break;
		case SDL_EVENT_KEY_UP:
			keyEvent.keycode = e.key.keysym.sym;
			keyEvent.type = EventType::eMovementStop;
			break;
		default:
			break;
		}

		ImGui_ImplSDL3_ProcessEvent(&e);

		++queueId;
	}

	process_general_queue_events();
	process_movement();
}


CameraMovement Plume::InputManager::sdl_key_to_movement(SDL_Keycode sym)
{
	CameraMovement movement = CameraMovement::NONE;

	switch (sym)
	{
	case SDLK_LSHIFT:
		movement = CameraMovement::UP;
		break;
	case SDLK_LCTRL:
		movement = CameraMovement::DOWN;
		break;
	case SDLK_w:
		movement = CameraMovement::FORWARD;
		break;
	case SDLK_s:
		movement = CameraMovement::BACKWARD;
		break;
	case SDLK_a:
		movement = CameraMovement::LEFT;
		break;
	case SDLK_d:
		movement = CameraMovement::RIGHT;
		break;
	default:
		break;
	}

	return movement;
}


void Plume::InputManager::clear_event_queue()
{
	for (Event& queueEvent : _eventQueue)
	{
		queueEvent.type = EventType::eNone;
	}
}


void Plume::InputManager::process_general_queue_events()
{
	for (Event& queueEvent : _eventQueue)
	{
		switch (queueEvent.type)
		{
		case EventType::eDefocusMode:
		case EventType::eDebugWindow:
			_defocusMode = !_defocusMode;
			break;
		case EventType::eMouseMotion:
			if (_defocusMode)
			{
				break;
			}

			on_mouse_motion_callback();
			break;

		case EventType::eMovement:
			if (_defocusMode)
			{
				break;
			}

			on_keyboard_event_callback(queueEvent.keycode, true);
			break;

		case EventType::eMovementStop:
			if (_defocusMode)
			{
				break;
			}

			on_keyboard_event_callback(queueEvent.keycode, false);
			break;

		case EventType::eZoom:
			if (_defocusMode)
			{
				break;
			}

			on_mouse_scroll_callback(queueEvent.scrollY);
			break;

		case EventType::eQuit:
			_shouldQuit = true;
			break;

		default:
			break;
		}
	}
}


void Plume::InputManager::on_mouse_motion_callback()
{
	float outRelX = 0;
	float outRelY = 0;

	SDL_GetRelativeMouseState(&outRelX, &outRelY);

	float xOffset = outRelX;
	float yOffset = -outRelY;

	_camera.process_camera_motion(xOffset, yOffset);
}


void Plume::InputManager::on_mouse_scroll_callback(float yOffset)
{
	_camera.process_mouse_scroll(yOffset);
}


void Plume::InputManager::process_movement()
{
	for (size_t i = 0; i < static_cast<size_t>(CameraMovement::MAX_ENUM); ++i)
	{
		auto movementType = static_cast<CameraMovement>(i);
		bool movementActive = _camera.is_movement_active(movementType);

		if (movementActive)
		{
			_camera.process_movement(movementType, _deltaTime);
		}
	}
}


void Plume::InputManager::on_keyboard_event_callback(SDL_Keycode sym, bool keyDown)
{
	CameraMovement movement = sdl_key_to_movement(sym);

	_camera.set_movement_status(movement, keyDown);
}
