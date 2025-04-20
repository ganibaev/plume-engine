#include "plm_inputs.h"
#include "plm_render.h"


void main(int argc, char* argv[])
{
	PlumeInputManager inputSystem;
	PlumeLightManager lightManager;
	PlumeRender renderer;

	lightManager.Init();

	RenderSystem::InitData initData;
	initData.pCam = inputSystem.get_p_camera();
	initData.pLightManager = &lightManager;

	renderer.init(initData);

	while (!inputSystem.should_quit())
	{
		inputSystem.poll_events();

		renderer.process_input_events(inputSystem.get_event_queue());
		renderer.render_frame();
	}

	renderer.terminate();
}
