#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "gfx_theforge/theforge.h"
#include "utils_gameappshell/gameappshell.h"
#include "utils_simple_logmanager/logmanager.h"
#include "gfx_shadercompiler/compiler.h"
#include "al2o3_vfile/vfile.h"
#include "al2o3_os/filesystem.h"
#include "input_basic/input.h"
#include "gfx_imgui_al2o3_theforge_bindings/bindings.h"
#include "gfx_imgui/imgui.h"

#include "devon_display.h"

const uint32_t FRAMES_AHEAD = 3;

TheForge_RendererHandle renderer;
TheForge_QueueHandle graphicsQueue;
TheForge_CmdPoolHandle cmdPool;
Display_ContextHandle display;

ShaderCompiler_ContextHandle shaderCompiler;

InputBasic_ContextHandle input;
InputBasic_KeyboardHandle keyboard;
InputBasic_MouseHandle mouse;
enum AppKey {
	AppKey_Quit
};

ImguiBindings_ContextHandle imguiBindings;

static bool Init() {

#if AL2O3_PLATFORM == AL2O3_PLATFORM_APPLE_MAC
	Os_SetCurrentDir("..");
#endif

	// window and renderer setup
	TheForge_RendererDesc desc{
			TheForge_ST_6_0,
			TheForge_GM_SINGLE,
			TheForge_D3D_FL_12_0,
	};

	renderer = TheForge_RendererCreate("Devon", &desc);
	if (!renderer) {
		return false;
	}
	shaderCompiler = ShaderCompiler_Create();
	if (!shaderCompiler) {
		return false;
	}

	// create basic graphics queues fences etc.
	TheForge_QueueDesc queueDesc{};
	queueDesc.type = TheForge_CP_DIRECT;
	TheForge_AddQueue(renderer, &queueDesc, &graphicsQueue);
	TheForge_AddCmdPool(renderer, graphicsQueue, false, &cmdPool);

	display = Display_Create(renderer, graphicsQueue, cmdPool, FRAMES_AHEAD);

	// init TheForge resourceloader
	TheForge_InitResourceLoaderInterface(renderer, nullptr);

	// setup basic input and map quit key
	input = InputBasic_Create();
	uint32_t userIdBlk = InputBasic_AllocateUserIdBlock(input); // 1st 1000 id are the apps
	ASSERT(userIdBlk == 0);

	if (InputBasic_GetKeyboardCount(input) > 0) {
		keyboard = InputBasic_KeyboardCreate(input, 0);
	}
	if (InputBasic_GetMouseCount(input) > 0) {
		mouse = InputBasic_MouseCreate(input, 0);
	}
	if (keyboard)
		InputBasic_MapToKey(input, AppKey_Quit, keyboard, InputBasic_Key_Escape);

	return true;
}

static bool Load() {

	imguiBindings = ImguiBindings_Create(renderer, shaderCompiler, input,
																			 20,
																			 FRAMES_AHEAD,
																			 Display_GetBackBufferFormat(display),
																			 Display_GetDepthBufferFormat(display),
																			 Display_IsBackBufferSrgb(display),
																			 TheForge_SC_1,
																			 0);
	if (!imguiBindings)
		return false;

	return true;
}

static void Update(double deltaMS) {
	GameAppShell_WindowDesc windowDesc;
	GameAppShell_WindowGetCurrentDesc(&windowDesc);

	InputBasic_SetWindowSize(input, windowDesc.width, windowDesc.height);
	ImguiBindings_SetWindowSize(imguiBindings, windowDesc.width, windowDesc.height);


	InputBasic_Update(input, deltaMS);
	if (InputBasic_GetAsBool(input, AppKey_Quit)) {
		GameAppShell_Quit();
	}

	// Imgui start
	ImguiBindings_UpdateInput(imguiBindings, deltaMS);
	ImGui::NewFrame();

	bool showDemo = true;
	ImGui::ShowDemoWindow(&showDemo);

	ImGui::EndFrame();
	ImGui::Render();

}

static void Draw(double deltaMS) {

	TheForge_RenderTargetHandle renderTarget;
	TheForge_RenderTargetHandle depthTarget;

	TheForge_CmdHandle cmd = Display_NewFrame(display, &renderTarget, &depthTarget);
	TheForge_RenderTargetDesc const* renderTargetDesc = TheForge_RenderTargetGetDesc(renderTarget);

	TheForge_LoadActionsDesc loadActions = {0};
	loadActions.loadActionsColor[0] = TheForge_LA_CLEAR;
	loadActions.clearColorValues[0] = { 0.45f, 0.5f, 0.6f, 0.0f };
	loadActions.loadActionDepth = TheForge_LA_CLEAR;
	loadActions.clearDepth.depth = 1.0f;
	loadActions.clearDepth.stencil = 0;
	TheForge_CmdBindRenderTargets(cmd,
																1,
																&renderTarget,
																depthTarget,
																&loadActions,
																nullptr, nullptr,
																-1, -1);
	TheForge_CmdSetViewport(cmd, 0.0f, 0.0f,
													(float) renderTargetDesc->width, (float) renderTargetDesc->height,
													0.0f, 1.0f);
	TheForge_CmdSetScissor(cmd, 0, 0,
												 renderTargetDesc->width, renderTargetDesc->height);

	ImguiBindings_Draw(imguiBindings, cmd);

	Display_Present(display);
}

static void Unload() {
	LOGINFO("Unloading");

	TheForge_WaitQueueIdle(graphicsQueue);

	ImguiBindings_Destroy(imguiBindings);

}

static void Exit() {
	LOGINFO("Exiting");

	InputBasic_MouseDestroy(mouse);
	InputBasic_KeyboardDestroy(keyboard);
	InputBasic_Destroy(input);

	Display_Destroy(display);

	TheForge_RemoveCmdPool(renderer, cmdPool);
	TheForge_RemoveQueue(graphicsQueue);
	ShaderCompiler_Destroy(shaderCompiler);
	TheForge_RendererDestroy(renderer);
}

static void Abort() {
	LOGINFO("ABORT ABORT ABORT");
	abort();
}

static void ProcessMsg(void *msg) {
	if (input) {
		InputBasic_PlatformProcessMsg(input, msg);
	}
}

int main(int argc, char const *argv[]) {
	auto logger = SimpleLogManager_Alloc();

	GameAppShell_Shell *shell = GameAppShell_Init();
	shell->onInitCallback = &Init;
	shell->onDisplayLoadCallback = &Load;
	shell->onDisplayUnloadCallback = &Unload;
	shell->onQuitCallback = &Exit;
	shell->onAbortCallback = &Abort;
	shell->perFrameUpdateCallback = &Update;
	shell->perFrameDrawCallback = &Draw;
	shell->onMsgCallback = &ProcessMsg;

	shell->initialWindowDesc.name = "Devon";
	shell->initialWindowDesc.width = -1;
	shell->initialWindowDesc.height = -1;
	shell->initialWindowDesc.windowsFlags = 0;
	shell->initialWindowDesc.visible = true;

	auto ret = GameAppShell_MainLoop(argc, argv);

	SimpleLogManager_Free(logger);
	return ret;

}
