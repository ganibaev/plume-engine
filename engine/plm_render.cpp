#include "plm_render.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"


void PlumeRender::init(RenderSystem::InitData& initData)
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

	_renderSystem.init(initData);
}


void PlumeRender::process_input_events(const std::array<PlumeInputs::Event, PlumeInputs::MAX_FRAME_EVENT_NUM>& queue)
{
	bool relMode = false;
	for (const auto& inputEvent : queue)
	{
		switch (inputEvent.type)
		{
		case PlumeInputs::EventType::eDebugWindow:
			_renderSystem._showDebugUi = !_renderSystem._showDebugUi;
		case PlumeInputs::EventType::eDefocusMode:
			relMode = SDL_GetRelativeMouseMode();

			SDL_WarpMouseInWindow(_pWindow, _windowExtent.width / 2.0f, _windowExtent.height / 2.0f);
			SDL_SetRelativeMouseMode(!relMode);

			break;

		default:
			break;
		}
	}
}


void PlumeRender::render_frame()
{
	_renderSystem.setup_debug_ui_frame();
	_renderSystem.render_frame();
}


void PlumeRender::terminate()
{
	_renderSystem.cleanup();

	if (_pWindow)
	{
		SDL_DestroyWindow(_pWindow);
	}
}


void PlumeRender::setup_debug_ui_frame()
{
	_renderSystem.setup_debug_ui_frame();
}