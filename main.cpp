#include "al2o3_platform/platform.h"
#include "gfx_theforge/theforge.h"
#include "utils_gameappshell/gameappshell.h"
#include "utils_simple_logmanager/logmanager.h"
#include "gfx_shadercompiler/compiler.h"
#include "al2o3_vfile/vfile.hpp"

#define DO_TRIANGLE 1

const uint32_t gImageCount = 3;
uint32_t gFrameIndex = 0;

TheForge_RendererHandle renderer;
TheForge_QueueHandle graphicsQueue;
TheForge_CmdPoolHandle cmdPool;
TheForge_CmdHandle *pCmds;

TheForge_SwapChainHandle swapChain;
TheForge_RenderTargetHandle depthBuffer;
TheForge_FenceHandle renderCompleteFences[gImageCount];
TheForge_SemaphoreHandle imageAcquiredSemaphore;
TheForge_SemaphoreHandle renderCompleteSemaphores[gImageCount];

TheForge_ShaderHandle shader;
TheForge_RootSignatureHandle rootSignature;
TheForge_DescriptorBinderHandle descriptorBinder;
TheForge_RasterizerStateHandle rasterizerState;
TheForge_DepthStateHandle depthState;
TheForge_BufferHandle vertexBuffer;
TheForge_BufferHandle indexBuffer;
TheForge_PipelineHandle pipeline;

TheForge_SwapChainDesc swapChainDesc;
TheForge_RenderTargetDesc renderTargetDesc;
TheForge_RenderTargetDesc depthRTDesc;

static void GameAppShellToTheForge_WindowsDesc(TheForge_WindowsDesc &windowDesc) {
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

	TheForge_RenderTargetHandle renderTarget = TheForge_SwapChainGetRenderTarget(swapChain, 0);
	memcpy(&renderTargetDesc, TheForge_RenderTargetGetDesc(renderTarget), sizeof(TheForge_RenderTargetDesc));

	return swapChain;
}

static bool AddDepthBuffer() {
	TheForge_WindowsDesc windowDesc{};
	GameAppShellToTheForge_WindowsDesc(windowDesc);

	// Add depth buffer
	depthRTDesc.arraySize = 1;
	depthRTDesc.clearValue.depth = 1.0f;
	depthRTDesc.clearValue.stencil = 0;
	depthRTDesc.depth = 1;
	depthRTDesc.format = TheForge_IF_D32F;
	depthRTDesc.width = windowDesc.windowedRect.right - windowDesc.windowedRect.left;
	depthRTDesc.height = windowDesc.windowedRect.bottom - windowDesc.windowedRect.top;
	ASSERT(depthRTDesc.width);
	ASSERT(depthRTDesc.height);
	depthRTDesc.sampleCount = TheForge_SC_1;
	depthRTDesc.sampleQuality = 0;
	TheForge_AddRenderTarget(renderer, &depthRTDesc, &depthBuffer);

	return depthBuffer;
}

static bool AddShader() {

	/*
	TheForge_ShaderLoadDesc basicShader = {};
	basicShader.target = TheForge_ST_5_1;
	basicShader.stages[0] = { "passthrough.vert", NULL, 0, TheForge_FSR_SrcShaders };
	basicShader.stages[1] = { "colour.frag", NULL, 0, TheForge_FSR_SrcShaders };

	TheForge_LoadShader(renderer, &basicShader, &shader);
*/

	char *vtxt = nullptr;
	char *ftxt = nullptr;

	char const* const vertName = "passthrough.hlsl";
	char const* const fragName = "colour.hlsl";
	{
		VFile::ScopedFile file = VFile::File::FromFile(vertName, Os_FM_Read);
		if(!file) return false;
		size_t const fileSize = file->Size();
		if(fileSize == 0) return false;
		vtxt = (char *) MEMORY_MALLOC(fileSize + 1);
		file->Read(vtxt, fileSize);
		vtxt[fileSize] = 0;
	}
	{
		VFile::ScopedFile file = VFile::File::FromFile(fragName, Os_FM_Read);
		if(!file) return false;
		size_t const fileSize = file->Size();
		if(fileSize == 0) return false;
		ftxt = (char *) MEMORY_MALLOC(fileSize + 1);
		file->Read(ftxt, fileSize);
		ftxt[fileSize] = 0;
	}
	ShaderCompiler_Output vout;
	memset(&vout, 0, sizeof(ShaderCompiler_Output));
	ShaderCompiler_Output fout;
	memset(&fout, 0, sizeof(ShaderCompiler_Output));

	{
		bool okay = ShaderCompiler_CompileShader(
				vertName,
				ShaderCompiler_LANG_HLSL,
				ShaderCompiler_ST_VertexShader,
				vtxt,
				"main",
				ShaderCompiler_OPT_None,
#if AL2O3_PLATFORM == AL2O3_PLATFORM_APPLE_MAC
				ShaderCompiler_OT_MSL_OSX,
#elif AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
				ShaderCompiler_OT_DXIL,
#endif
				&vout);
		if (okay) {
			if (vout.log != nullptr) {
				LOGWARNINGF("Shader compiler : Warnings %s", vout.log);
			}
		} else {
			if (vout.log != nullptr) {
				LOGERRORF("Shader compiler : Errors %s", vout.log);
			}
		}
	}

	{
		bool okay = ShaderCompiler_CompileShader(
				fragName,
				ShaderCompiler_LANG_HLSL,
				ShaderCompiler_ST_FragmentShader,
				ftxt,
				"main",
				ShaderCompiler_OPT_None,
#if AL2O3_PLATFORM == AL2O3_PLATFORM_APPLE_MAC
				ShaderCompiler_OT_MSL_OSX,
#elif AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
				ShaderCompiler_OT_DXIL,
#endif
				&fout);
		if (okay) {
			if (fout.log != nullptr) {
				LOGWARNINGF("Shader compiler : Warnings %s", fout.log);
			}
		} else {
			if (fout.log != nullptr) {
				LOGERRORF("Shader compiler : Errors %s", fout.log);
			}
		}
	}

	TheForge_BinaryShaderDesc bdesc;
	bdesc.stages = (TheForge_ShaderStage) (TheForge_SS_FRAG | TheForge_SS_VERT);
	bdesc.vert.byteCode = (char*) vout.shader;
	bdesc.vert.byteCodeSize = vout.shaderSize;
	bdesc.vert.entryPoint = "main";
	bdesc.vert.source = vtxt;
	bdesc.frag.byteCode = (char*) fout.shader;
	bdesc.frag.byteCodeSize = fout.shaderSize;
	bdesc.frag.entryPoint = "main";
	bdesc.frag.source = ftxt;
	TheForge_AddShaderBinary(renderer, &bdesc, &shader);

	MEMORY_FREE((void *) vout.log);
	MEMORY_FREE((void *) vout.shader);
	MEMORY_FREE((void *) fout.log);
	MEMORY_FREE((void *) fout.shader);
	MEMORY_FREE(vtxt);
	MEMORY_FREE(ftxt);

	return shader;
}

static bool AddRootSignature() {
	TheForge_ShaderHandle shaders[]{shader};
	TheForge_RootSignatureDesc rootDesc{};
	rootDesc.staticSamplerCount = 0;
	rootDesc.shaderCount = 1;
	rootDesc.pShaders = shaders;
	TheForge_AddRootSignature(renderer, &rootDesc, &rootSignature);
	return rootSignature;
}

static bool AddDescriptorBinder() {
	TheForge_DescriptorBinderDesc descriptorBinderDesc[1] = {
			{rootSignature}
	};

	TheForge_AddDescriptorBinder(renderer, 0, 1, descriptorBinderDesc, &descriptorBinder);

	return descriptorBinder;
}
static bool AddRasterizerState() {
	TheForge_RasterizerStateDesc rasterizerStateDesc{};

	rasterizerStateDesc.cullMode = TheForge_CM_NONE;
	TheForge_AddRasterizerState(renderer, &rasterizerStateDesc, &rasterizerState);

	return rasterizerState;
}

static bool AddDepthState() {
	TheForge_DepthStateDesc depthStateDesc{};
	depthStateDesc.depthTest = true;
	depthStateDesc.depthWrite = true;
	depthStateDesc.depthFunc = TheForge_CMP_LEQUAL;
	TheForge_AddDepthState(renderer, &depthStateDesc, &depthState);

	return depthState;
}
static bool AddTriangle() {
	float const triVerts[] = {
			//x     y     z     r     g     b
			0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.5f, 0.0f, 1.0f, 0.0f,
			1.0f, 1.0f, 0.5f, 0.0f, 0.0f, 1.0f,
	};

	uint16_t const triIndices[] = {
			0, 1, 2
	};

	uint64_t triVertsDataSize = 3 * 6 * sizeof(float);
	TheForge_BufferLoadDesc triVbDesc{};
	triVbDesc.mDesc.mDescriptors = TheForge_DESCRIPTOR_TYPE_VERTEX_BUFFER;
	triVbDesc.mDesc.mMemoryUsage = TheForge_RMU_GPU_ONLY;
	triVbDesc.mDesc.mSize = triVertsDataSize;
	triVbDesc.mDesc.mVertexStride = sizeof(float) * 6;
	triVbDesc.pData = triVerts;
	triVbDesc.pBuffer = &vertexBuffer;
	TheForge_AddBuffer(&triVbDesc, true);

	uint64_t triIndexDataSize = 3 * sizeof(uint16_t);
	TheForge_BufferLoadDesc triIbDesc{};
	triIbDesc.mDesc.mDescriptors = TheForge_DESCRIPTOR_TYPE_INDEX_BUFFER;
	triIbDesc.mDesc.mMemoryUsage = TheForge_RMU_GPU_ONLY;
	triIbDesc.mDesc.mSize = triIndexDataSize;
	triIbDesc.mDesc.mIndexType = TheForge_IT_UINT16;
	triIbDesc.pData = triIndices;
	triIbDesc.pBuffer = &indexBuffer;
	TheForge_AddBuffer(&triIbDesc, true);

	TheForge_FinishResourceLoading();

	return indexBuffer && vertexBuffer;
}

static bool AddPipeline() {

	TheForge_VertexLayout vertexLayout{};
	vertexLayout.attribCount = 2;
	vertexLayout.attribs[0].semantic = TheForge_SS_POSITION;
	vertexLayout.attribs[0].format = TheForge_IF_RGB32F;
	vertexLayout.attribs[0].binding = 0;
	vertexLayout.attribs[0].location = 0;
	vertexLayout.attribs[0].offset = 0;
	vertexLayout.attribs[1].semantic = TheForge_SS_COLOR;
	vertexLayout.attribs[1].format = TheForge_IF_RGB32F;
	vertexLayout.attribs[1].binding = 0;
	vertexLayout.attribs[1].location = 1;
	vertexLayout.attribs[1].offset = 3 * sizeof(float);

	TheForge_PipelineDesc desc{};
	desc.type = TheForge_PT_GRAPHICS;

	TheForge_GraphicsPipelineDesc &pipelineSettings = desc.graphicsDesc;
	pipelineSettings.primitiveTopo = TheForge_PT_TRI_LIST;
	pipelineSettings.renderTargetCount = 1;
	pipelineSettings.depthState = depthState;
	pipelineSettings.pColorFormats = &renderTargetDesc.format;
	pipelineSettings.pSrgbValues = &renderTargetDesc.sRGB;
	pipelineSettings.sampleCount = renderTargetDesc.sampleCount;
	pipelineSettings.sampleQuality = renderTargetDesc.sampleQuality;
	pipelineSettings.depthStencilFormat = depthRTDesc.format;
	pipelineSettings.rootSignature = rootSignature;
	pipelineSettings.shaderProgram = shader;
	pipelineSettings.pVertexLayout = &vertexLayout;
	pipelineSettings.rasterizerState = rasterizerState;
	TheForge_AddPipeline(renderer, &desc, &pipeline);

	return pipeline;
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
	renderer = TheForge_RendererCreate("Devon", &desc);

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

	TheForge_InitResourceLoaderInterface(renderer, nullptr);

	return true;
}

static bool Load() {
	LOGINFO("Loading");
	if (!AddSwapChain())
		return false;

	if (!AddDepthBuffer())
		return false;
#if DO_TRIANGLE == 1

	if (!AddShader())
		return false;

	if (!AddRootSignature())
		return false;

	if (!AddRasterizerState())
		return false;

	if (!AddDepthState())
		return false;

	if (!AddDescriptorBinder())
		return false;

	if (!AddPipeline())
		return false;

	if (!AddTriangle())
		return false;
#endif
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
#if DO_TRIANGLE == 1
	TheForge_CmdBindPipeline(cmd, pipeline);
	TheForge_CmdBindDescriptors(cmd, descriptorBinder, rootSignature, 0, nullptr);
	TheForge_CmdBindVertexBuffer(cmd, 1, &vertexBuffer, nullptr);
	TheForge_CmdBindIndexBuffer(cmd, indexBuffer, 0);
	TheForge_CmdDrawIndexed(cmd, 3, 0, 0);
#endif

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

	GameAppShell_Shell *shell = GameAppShell_Init();
	shell->onInitCallback = &Init;
	shell->onDisplayLoadCallback = &Load;
	shell->onDisplayUnloadCallback = &Unload;
	shell->onQuitCallback = &Exit;
	shell->onAbortCallback = &Abort;
	shell->perFrameUpdateCallback = &Update;
	shell->perFrameDrawCallback = &Draw;

	shell->initialWindowDesc.name = "Devon";
	shell->initialWindowDesc.width = -1;
	shell->initialWindowDesc.height = -1;
	shell->initialWindowDesc.windowsFlags = 0;
	shell->initialWindowDesc.visible = true;

	auto ret = GameAppShell_MainLoop(argc, argv);

	SimpleLogManager_Free(logger);
	return ret;

}
