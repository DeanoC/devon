#include "al2o3_platform/platform.h"
#include "gfx_theforge/theforge.h"
#include "utils_gameappshell/gameappshell.h"
#include "utils_simple_logmanager/logmanager.h"

const uint32_t gImageCount = 3;

TheForge_RendererHandle renderer;
TheForge_QueueHandle graphicsQueue;
TheForge_CmdPoolHandle cmdPool;
TheForge_CmdHandle *pCmds;

TheForge_SwapChainHandle swapChain;
TheForge_RenderTargetHandle depthBuffer;
TheForge_FenceHandle renderCompleteFences[gImageCount];
TheForge_SemaphoreHandle imageAcquiredSemaphore;
TheForge_SemaphoreHandle renderCompleteSemaphores[gImageCount];

uint32_t gFrameIndex = 0;

static void GameAppShellToTheForge_WindowsDesc(TheForge_WindowsDesc& windowDesc) {
	GameAppShell_WindowDesc gasWindowDesc;
	GameAppShell_WindowGetCurrentDesc(&gasWindowDesc);

	windowDesc.handle = GameAppShell_GetPlatformWindowPtr();
	windowDesc.windowedRect.right = gasWindowDesc.width;
	windowDesc.windowedRect.left = 0;
	windowDesc.windowedRect.bottom = gasWindowDesc.height;
	windowDesc.windowedRect.top = 0;
	windowDesc.fullscreenRect = windowDesc.windowedRect;
	windowDesc.clientRect = windowDesc.windowedRect;
	windowDesc.fullScreen = false;
	windowDesc.windowsFlags = 0;
	windowDesc.bigIcon = gasWindowDesc.bigIcon;
	windowDesc.smallIcon = gasWindowDesc.smallIcon;

	windowDesc.iconified = gasWindowDesc.iconified;
	windowDesc.maximized = gasWindowDesc.maximized;
	windowDesc.minimized = gasWindowDesc.minimized;
	windowDesc.visible = gasWindowDesc.visible;
}

static bool AddSwapChain() {
	TheForge_WindowsDesc windowDesc{};
	GameAppShellToTheForge_WindowsDesc(windowDesc);

	TheForge_SwapChainDesc swapChainDesc{};
	swapChainDesc.pWindow = &windowDesc;
	swapChainDesc.presentQueueCount = 1;
	swapChainDesc.pPresentQueues = &graphicsQueue;
	swapChainDesc.width = windowDesc.windowedRect.right - windowDesc.windowedRect.left;
	swapChainDesc.height = windowDesc.windowedRect.bottom - windowDesc.windowedRect.top;
	ASSERT(swapChainDesc.width);
	ASSERT(swapChainDesc.height);
	swapChainDesc.imageCount = gImageCount;
	swapChainDesc.sampleCount = TheForge_SC_1;
	swapChainDesc.colorFormat = TheForge_GetRecommendedSwapchainFormat(true);
	swapChainDesc.enableVsync = false;
	TheForge_AddSwapChain(renderer, &swapChainDesc, &swapChain);

	return swapChain;
}

static bool AddDepthBuffer() {
	TheForge_WindowsDesc windowDesc{};
	GameAppShellToTheForge_WindowsDesc(windowDesc);

	// Add depth buffer
	TheForge_RenderTargetDesc depthRT{};
	depthRT.arraySize = 1;
	depthRT.clearValue.depth = 1.0f;
	depthRT.clearValue.stencil = 0;
	depthRT.depth = 1;
	depthRT.format = TheForge_IF_D32F;
	depthRT.width = windowDesc.windowedRect.right - windowDesc.windowedRect.left;
	depthRT.height = windowDesc.windowedRect.bottom - windowDesc.windowedRect.top;
	ASSERT(depthRT.width);
	ASSERT(depthRT.height);
	depthRT.sampleCount = TheForge_SC_1;
	depthRT.sampleQuality = 0;
	TheForge_AddRenderTarget(renderer, &depthRT, &depthBuffer);

	return depthBuffer;
}

static bool Init() {
	LOGINFO("Initing");
	// window and renderer setup
	TheForge_RendererDesc desc{
			TheForge_ST_5_1,
			TheForge_GM_SINGLE
	};
#if AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
	desc.d3dFeatureLevel = TheForge_D3D_FL_12_0;
#endif
	renderer = TheForge_RendererCreate("theforge_triangle_c", &desc);

	//check for init success
	if (!renderer) {
		return false;
	}

	TheForge_QueueDesc queueDesc{};
	queueDesc.type = TheForge_CP_DIRECT;
	TheForge_AddQueue(renderer, &queueDesc, &graphicsQueue);
	TheForge_AddCmdPool(renderer, graphicsQueue, false, &cmdPool);
	TheForge_AddCmd_n(cmdPool, false, gImageCount, &pCmds);

	for (uint32_t i = 0; i < gImageCount; ++i) {
		TheForge_AddFence(renderer, &renderCompleteFences[i]);
		TheForge_AddSemaphore(renderer, &renderCompleteSemaphores[i]);
	}
	TheForge_AddSemaphore(renderer, &imageAcquiredSemaphore);

	return true;
}

static bool Load() {
	LOGINFO("Loading");
	if (!AddSwapChain())
		return false;

	if (!AddDepthBuffer())
		return false;

	return true;
}

static void Update(double deltaMS) {
}

static void Draw(double deltaMS) {
	TheForge_AcquireNextImage(renderer, swapChain, imageAcquiredSemaphore, NULL, &gFrameIndex);

	TheForge_RenderTargetHandle renderTarget = TheForge_SwapChainGetRenderTarget(swapChain, gFrameIndex);
	TheForge_SemaphoreHandle renderCompleteSemaphore = renderCompleteSemaphores[gFrameIndex];
	TheForge_FenceHandle renderCompleteFence = renderCompleteFences[gFrameIndex];

	// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
	TheForge_FenceStatus fenceStatus;
	TheForge_GetFenceStatus(renderer, renderCompleteFence, &fenceStatus);
	if (fenceStatus == TheForge_FS_INCOMPLETE)
		TheForge_WaitForFences(renderer, 1, &renderCompleteFence);

	// simply record the screen cleaning command
	TheForge_LoadActionsDesc loadActions = {0};
	loadActions.loadActionsColor[0] = TheForge_LA_CLEAR;
	loadActions.clearColorValues[0].r = 1.0f;
	loadActions.clearColorValues[0].g = 1.0f;
	loadActions.clearColorValues[0].b = 0.0f;
	loadActions.clearColorValues[0].a = 0.0f;
	loadActions.loadActionDepth = TheForge_LA_CLEAR;
	loadActions.clearDepth.depth = 1.0f;
	loadActions.clearDepth.stencil = 0;

	TheForge_Cmd *cmd = pCmds[gFrameIndex];
	TheForge_BeginCmd(cmd);

	TheForge_TextureBarrier barriers[] = {
			{TheForge_RenderTargetGetTexture(renderTarget), TheForge_RS_RENDER_TARGET},
			{TheForge_RenderTargetGetTexture(depthBuffer), TheForge_RS_DEPTH_WRITE},
	};
	TheForge_CmdResourceBarrier(cmd, 0, nullptr, 2, barriers, false);

	TheForge_CmdBindRenderTargets(cmd,
																1,
																&renderTarget,
																depthBuffer,
																&loadActions,
																nullptr,
																nullptr,
																-1,
																-1);
	TheForge_CmdSetViewport(cmd,
													0.0f,
													0.0f,
													(float) TheForge_RenderTargetGetDesc(renderTarget)->width,
													(float) TheForge_RenderTargetGetDesc(renderTarget)->height,
													0.0f,
													1.0f);
	TheForge_CmdSetScissor(cmd,
												 0,
												 0,
												 TheForge_RenderTargetGetDesc(renderTarget)->width,
												 TheForge_RenderTargetGetDesc(renderTarget)->height
	);

	TheForge_CmdBindRenderTargets(cmd,
																0,
																NULL,
																NULL,
																NULL,
																NULL,
																NULL,
																-1,
																-1);
	TheForge_CmdEndDebugMarker(cmd);

	barriers[0].texture = TheForge_RenderTargetGetTexture(renderTarget);
	barriers[0].newState = TheForge_RS_PRESENT;

	TheForge_CmdResourceBarrier(cmd,
															0,
															nullptr,
															1,
															barriers,
															true);
	TheForge_EndCmd(cmd);

	TheForge_QueueSubmit(graphicsQueue,
											 1,
											 &cmd,
											 renderCompleteFence,
											 1,
											 &imageAcquiredSemaphore,
											 1,
											 &renderCompleteSemaphore);

	TheForge_QueuePresent(graphicsQueue,
												swapChain,
												gFrameIndex,
												1,
												&renderCompleteSemaphore);

}

static void Unload() {
	LOGINFO("Unloading");

	TheForge_WaitQueueIdle(graphicsQueue);

	TheForge_RemoveSwapChain(renderer, swapChain);
	TheForge_RemoveRenderTarget(renderer, depthBuffer);

}

static void Exit() {
	LOGINFO("Exiting");

	TheForge_RemoveSemaphore(renderer, imageAcquiredSemaphore);

	for (uint32_t i = 0; i < gImageCount; ++i) {
		TheForge_RemoveFence(renderer, renderCompleteFences[i]);
		TheForge_RemoveSemaphore(renderer, renderCompleteSemaphores[i]);
	}

	TheForge_RemoveCmd_n(cmdPool, gImageCount, pCmds);
	TheForge_RemoveCmdPool(renderer, cmdPool);

	TheForge_RemoveQueue(graphicsQueue);
	TheForge_RendererDestroy(renderer);
}

static void Abort() {
	abort();
}

int main(int argc, char const *argv[]) {
	auto logger = SimpleLogManager_Alloc();

	GameAppShell_Shell* shell = GameAppShell_Init();
	shell->onInitCallback = &Init;
	shell->onDisplayLoadCallback = &Load;
	shell->onDisplayUnloadCallback = &Unload;
	shell->onQuitCallback = &Exit;
	shell->onAbortCallback = &Abort;
	shell->perFrameUpdateCallback = &Update;
	shell->perFrameDrawCallback = &Draw;

	shell->initialWindowDesc.name = "theforge_triangle_c";
	shell->initialWindowDesc.width = -1;
	shell->initialWindowDesc.height = -1;
	shell->initialWindowDesc.windowsFlags = 0;
	shell->initialWindowDesc.visible = true;

	auto ret = GameAppShell_MainLoop(argc, argv);

	SimpleLogManager_Free(logger);
	return ret;

}
