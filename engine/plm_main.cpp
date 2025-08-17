#include "plm_inputs.h"
#include "plm_render.h"


void main(int argc, char* argv[])
{
	Plume::InputManager inputSystem;

	Plume::Scene scene;
	scene.DefaultInit();

	Plume::LightManager lightManager;
	lightManager.DefaultInit();

	Render::System::InitData initData;
	initData.pCam = inputSystem.get_p_camera();
	initData.pLightManager = &lightManager;
	initData.pScene = &scene;

	Plume::RenderManager renderer;
	renderer.init(initData);

	while (!inputSystem.should_quit())
	{
		inputSystem.poll_events();

		renderer.process_input_events(inputSystem.get_event_queue());
		renderer.render_frame();
	}

	renderer.terminate();
}
