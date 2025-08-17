#pragma once

#include <render_system.h>
#include "plm_inputs.h"


struct SDL_Window;

namespace Plume
{

class RenderManager
{
public:
	void Init(Render::System::InitData& initData);

	void ProcessInputEvents(const std::array<Plume::InputManager::Event, Plume::InputManager::MAX_FRAME_EVENT_NUM>& queue);

	void SetPWindow(SDL_Window* pWindow) { _pWindow = pWindow; }
	const SDL_Window* GetPWindow() const { return _pWindow; }

	void SetWindowExtent(vk::Extent2D windowExtent) { _windowExtent = windowExtent; }
	vk::Extent2D GetWindowExtent() const { return _windowExtent; }

	void RenderFrame();
	void Terminate();

private:
	Render::System _renderSystem;

	bool _defocusMode = false;

	vk::Extent2D _windowExtent{ 1920, 1080 };
	SDL_Window* _pWindow = nullptr;

	void SetupDebugUIFrame();
};

} // namespace Plume
