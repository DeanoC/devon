#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "gfx_theforge/theforge.h"
#include "utils_gameappshell/gameappshell.h"

typedef struct Display_Context {
	TheForge_RendererHandle renderer;
	TheForge_QueueHandle presentQueue;
	TheForge_CmdPoolHandle cmdPool;
	uint32_t swapImageCount;

	TheForge_SwapChainHandle swapChain;
	TheForge_RenderTargetHandle depthBuffer;
	TheForge_FenceHandle *renderCompleteFences;
	TheForge_SemaphoreHandle imageAcquiredSemaphore;
	TheForge_SemaphoreHandle *renderCompleteSemaphores;
	TheForge_CmdHandle *frameCmds;

	uint32_t frameIndex;
} *Display_ContextHandle;

Display_ContextHandle Display_Create(TheForge_RendererHandle renderer,
																		 TheForge_QueueHandle presentQueue,
																		 TheForge_CmdPoolHandle cmdPool,
																		 uint32_t swapImageCount) {
	GameAppShell_WindowDesc gasWindowDesc;
	GameAppShell_WindowGetCurrentDesc(&gasWindowDesc);

	Display_Context *ctx = (Display_Context *) MEMORY_CALLOC(1, sizeof(Display_Context));

	ctx->renderer = renderer;
	ctx->presentQueue = presentQueue;
	ctx->swapImageCount = swapImageCount;
	ctx->cmdPool = cmdPool;
	ctx->renderCompleteFences = (TheForge_FenceHandle *) MEMORY_CALLOC(swapImageCount, sizeof(TheForge_FenceHandle));
	ctx->renderCompleteSemaphores =
			(TheForge_SemaphoreHandle *) MEMORY_CALLOC(swapImageCount, sizeof(TheForge_SemaphoreHandle));

	for (uint32_t i = 0; i < swapImageCount; ++i) {
		TheForge_AddFence(renderer, &ctx->renderCompleteFences[i]);
		TheForge_AddSemaphore(renderer, &ctx->renderCompleteSemaphores[i]);
	}
	TheForge_AddSemaphore(renderer, &ctx->imageAcquiredSemaphore);

	TheForge_AddCmd_n(ctx->cmdPool, false, ctx->swapImageCount, &ctx->frameCmds);

	TheForge_WindowsDesc windowDesc;
	memset(&windowDesc, 0, sizeof(TheForge_WindowsDesc));
	windowDesc.handle = GameAppShell_GetPlatformWindowPtr();

	TheForge_SwapChainDesc swapChainDesc;
	swapChainDesc.pWindow = &windowDesc;
	swapChainDesc.presentQueueCount = 1;
	swapChainDesc.pPresentQueues = &presentQueue;
	swapChainDesc.width = gasWindowDesc.width;
	swapChainDesc.height = gasWindowDesc.height;
	ASSERT(swapChainDesc.width);
	ASSERT(swapChainDesc.height);
	swapChainDesc.imageCount = swapImageCount;
	swapChainDesc.sampleCount = TheForge_SC_1;
	swapChainDesc.sampleQuality = 0;
	swapChainDesc.colorFormat = TheForge_GetRecommendedSwapchainFormat(true);
	swapChainDesc.srgb = true;
	swapChainDesc.enableVsync = false;
	TheForge_AddSwapChain(renderer, &swapChainDesc, &ctx->swapChain);

	// Add depth buffer
	TheForge_RenderTargetDesc depthRTDesc;
	depthRTDesc.arraySize = 1;
	depthRTDesc.clearValue.depth = 1.0f;
	depthRTDesc.clearValue.stencil = 0;
	depthRTDesc.depth = 1;
	depthRTDesc.format = TheForge_IF_D32F;
	depthRTDesc.width = gasWindowDesc.width;
	depthRTDesc.height = gasWindowDesc.height;
	depthRTDesc.mipLevels = 1;
	ASSERT(depthRTDesc.width);
	ASSERT(depthRTDesc.height);
	depthRTDesc.sampleCount = TheForge_SC_1;
	depthRTDesc.sampleQuality = 0;
	depthRTDesc.descriptors = TheForge_DESCRIPTOR_TYPE_UNDEFINED;
	depthRTDesc.debugName = "Backing DepthBuffer";
	depthRTDesc.flags = TheForge_TCF_NONE;
	TheForge_AddRenderTarget(renderer, &depthRTDesc, &ctx->depthBuffer);

	return ctx;
}

void Display_Destroy(Display_ContextHandle handle) {
	if (!handle)
		return;
	Display_Context *ctx = (Display_Context *) handle;

	if (ctx->swapChain) {
		TheForge_RemoveSwapChain(ctx->renderer, ctx->swapChain);
	}
	if (ctx->depthBuffer) {
		TheForge_RemoveRenderTarget(ctx->renderer, ctx->depthBuffer);
	}

	TheForge_RemoveCmd_n(ctx->cmdPool, ctx->swapImageCount, ctx->frameCmds);

	TheForge_RemoveSemaphore(ctx->renderer, ctx->imageAcquiredSemaphore);

	for (uint32_t i = 0; i < ctx->swapImageCount; ++i) {
		TheForge_RemoveFence(ctx->renderer, ctx->renderCompleteFences[i]);
		TheForge_RemoveSemaphore(ctx->renderer, ctx->renderCompleteSemaphores[i]);
	}

	MEMORY_FREE(ctx->renderCompleteFences);
	MEMORY_FREE(ctx->renderCompleteSemaphores);
	MEMORY_FREE(ctx);

}

TheForge_CmdHandle Display_NewFrame(Display_ContextHandle handle,
																		TheForge_RenderTargetHandle *renderTarget,
																		TheForge_RenderTargetHandle *depthTarget) {
	Display_Context *ctx = (Display_Context *) handle;
	ASSERT(renderTarget != nullptr);
	ASSERT(depthTarget != nullptr);

	TheForge_AcquireNextImage(ctx->renderer, ctx->swapChain, ctx->imageAcquiredSemaphore, nullptr, &ctx->frameIndex);

	*renderTarget = TheForge_SwapChainGetRenderTarget(ctx->swapChain, ctx->frameIndex);
	*depthTarget = ctx->depthBuffer;

	TheForge_SemaphoreHandle renderCompleteSemaphore = ctx->renderCompleteSemaphores[ctx->frameIndex];
	TheForge_FenceHandle renderCompleteFence = ctx->renderCompleteFences[ctx->frameIndex];

	// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
	TheForge_FenceStatus fenceStatus;
	TheForge_GetFenceStatus(ctx->renderer, renderCompleteFence, &fenceStatus);
	if (fenceStatus == TheForge_FS_INCOMPLETE)
		TheForge_WaitForFences(ctx->renderer, 1, &renderCompleteFence);

	TheForge_CmdHandle cmd = ctx->frameCmds[ctx->frameIndex];
	TheForge_BeginCmd(cmd);

	// insert write barrier for render target if we are more the N frames ahead
	TheForge_TextureBarrier barriers[] = {
			{TheForge_RenderTargetGetTexture(*renderTarget), TheForge_RS_RENDER_TARGET},
			{TheForge_RenderTargetGetTexture(ctx->depthBuffer), TheForge_RS_DEPTH_WRITE},
	};
	TheForge_CmdResourceBarrier(cmd, 0, nullptr, 2, barriers, false);

	return cmd;
}

void Display_Present(Display_ContextHandle handle) {
	Display_Context *ctx = (Display_Context *) handle;
	if(!ctx) return;

	TheForge_CmdHandle cmd = ctx->frameCmds[ctx->frameIndex];

	TheForge_RenderTargetHandle renderTarget = TheForge_SwapChainGetRenderTarget(ctx->swapChain, ctx->frameIndex);

	TheForge_CmdBindRenderTargets(cmd,
																0,
																nullptr, nullptr,
																nullptr,
																nullptr, nullptr,
																-1,
																-1);

	TheForge_TextureBarrier barriers[] = {
			{TheForge_RenderTargetGetTexture(renderTarget), TheForge_RS_PRESENT},
	};

	TheForge_CmdResourceBarrier(cmd,
															0,
															nullptr,
															1,
															barriers,
															true);
	TheForge_EndCmd(cmd);

	TheForge_QueueSubmit(ctx->presentQueue,
											 1,
											 &cmd,
											 ctx->renderCompleteFences[ctx->frameIndex],
											 1,
											 &ctx->imageAcquiredSemaphore,
											 1,
											 &ctx->renderCompleteSemaphores[ctx->frameIndex]);

	TheForge_QueuePresent(ctx->presentQueue,
												ctx->swapChain,
												ctx->frameIndex,
												1,
												&ctx->renderCompleteSemaphores[ctx->frameIndex]);

}

TheForge_ImageFormat Display_GetBackBufferFormat(Display_ContextHandle handle) {
	Display_Context *ctx = (Display_Context *) handle;
	if(!ctx) return TheForge_IF_NONE;

	TheForge_RenderTargetHandle renderTarget = TheForge_SwapChainGetRenderTarget(ctx->swapChain, ctx->frameIndex);
	TheForge_RenderTargetDesc const* desc = TheForge_RenderTargetGetDesc(renderTarget);
	return desc->format;
}
bool Display_IsBackBufferSrgb(Display_ContextHandle handle) {
	Display_Context *ctx = (Display_Context *) handle;
	if(!ctx) return false;

	TheForge_RenderTargetHandle renderTarget = TheForge_SwapChainGetRenderTarget(ctx->swapChain, ctx->frameIndex);
	TheForge_RenderTargetDesc const* desc = TheForge_RenderTargetGetDesc(renderTarget);
	return desc->sRGB;
}

TheForge_ImageFormat Display_GetDepthBufferFormat(Display_ContextHandle handle) {
	Display_Context *ctx = (Display_Context *) handle;
	if(!ctx) return TheForge_IF_NONE;

	TheForge_RenderTargetDesc const* desc = TheForge_RenderTargetGetDesc(ctx->depthBuffer);
	return desc->format;
}
