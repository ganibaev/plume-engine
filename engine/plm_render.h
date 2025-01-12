#pragma once

#include <render_system.h>


class PlumeRender
{
public:
	void init();
	void render();
	void terminate();

private:
	RenderSystem renderSystem;
};