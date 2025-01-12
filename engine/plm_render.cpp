#include "plm_render.h"


void PlumeRender::init()
{
	renderSystem.init();
}

void PlumeRender::render()
{
	renderSystem.run();
}

void PlumeRender::terminate()
{
	renderSystem.cleanup();
}