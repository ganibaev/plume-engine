#include "plm_render.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"


void Plume::RenderManager::Init(Render::System::InitData& initData)
{
	// We initialize SDL and create a window with it. 
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	_pWindow = SDL_CreateWindow(
		"Plume",
		_windowExtent.width,
		_windowExtent.height,
		window_flags
	);

	SDL_SetRelativeMouseMode(SDL_TRUE);

	initData.pWindow = _pWindow;
	initData.windowExtent = _windowExtent;

	_renderSystem.Init(initData);
}


void Plume::RenderManager::ProcessInputEvents(const std::array<Plume::InputManager::Event, Plume::InputManager::MAX_FRAME_EVENT_NUM>& queue)
{
	bool relMode = false;
	for (const auto& inputEvent : queue)
	{
		switch (inputEvent.type)
		{
		case Plume::InputManager::EventType::eDebugWindow:
			_renderSystem._showDebugUi = !_renderSystem._showDebugUi;
		case Plume::InputManager::EventType::eDefocusMode:
			relMode = SDL_GetRelativeMouseMode();

			SDL_WarpMouseInWindow(_pWindow, _windowExtent.width / 2.0f, _windowExtent.height / 2.0f);
			SDL_SetRelativeMouseMode(!relMode);

			break;

		default:
			break;
		}
	}
}


void Plume::RenderManager::RenderFrame()
{
	_renderSystem.SetupDebugUIFrame();
	_renderSystem.RenderFrame();
}


void Plume::RenderManager::Terminate()
{
	_renderSystem.Cleanup();

	if (_pWindow)
	{
		SDL_DestroyWindow(_pWindow);
	}
}


void Plume::RenderManager::SetupDebugUIFrame()
{
	_renderSystem.SetupDebugUIFrame();
}
