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
	initData.pCam = inputSystem.GetPCamera();
	initData.pLightManager = &lightManager;
	initData.pScene = &scene;

	Plume::RenderManager renderer;
	renderer.Init(initData);

	while (!inputSystem.ShouldQuit())
	{
		inputSystem.PollEvents();

		renderer.ProcessInputEvents(inputSystem.GetEventQueue());
		renderer.RenderFrame();
	}

	renderer.Terminate();
}
