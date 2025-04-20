#pragma once

#include <render_system.h>
#include "plm_inputs.h"


struct SDL_Window;

class PlumeRender
{
public:
	void init(RenderSystem::InitData& initData);

	void process_input_events(const std::array<PlumeInputManager::Event, PlumeInputManager::MAX_FRAME_EVENT_NUM>& queue);

	void set_p_window(SDL_Window* pWindow) { _pWindow = pWindow; }
	const SDL_Window* get_p_window() const { return _pWindow; }

	void set_window_extent(vk::Extent2D windowExtent) { _windowExtent = windowExtent; }
	vk::Extent2D get_window_extent() const { return _windowExtent; }

	void render_frame();
	void terminate();

private:
	RenderSystem _renderSystem;

	bool _defocusMode = false;

	vk::Extent2D _windowExtent{ 1920, 1080 };
	SDL_Window* _pWindow = nullptr;

	void setup_debug_ui_frame();
};
