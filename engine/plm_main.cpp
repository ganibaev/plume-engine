#include "plm_render.h"


void main(int argc, char* argv[])
{
	PlumeRender renderer;

	renderer.init();

	renderer.render();

	renderer.terminate();
}
