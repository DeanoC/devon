#include <cstdio>
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_enki/TaskScheduler_c.h"
#include "gfx_theforge/theforge.h"
#include "gfx_imageio/io.h"
#include "gfx_image/utils.h"
#include "utils_gameappshell/gameappshell.h"
#include "utils_simple_logmanager/logmanager.h"
#include "gfx_shadercompiler/compiler.h"
#include "al2o3_vfile/vfile.h"
#include "al2o3_os/filesystem.h"
#include "input_basic/input.h"
#include "gfx_imgui_al2o3_theforge_bindings/bindings.h"
#include "gfx_imgui/imgui.h"
#include "utils_nativefiledialogs/dialogs.h"

#include "devon_display.h"
#include "texture_viewer.hpp"

const uint32_t FRAMES_AHEAD = 3;

TheForge_RendererHandle renderer;
TheForge_QueueHandle graphicsQueue;
TheForge_CmdPoolHandle cmdPool;
Display_ContextHandle display;

ShaderCompiler_ContextHandle shaderCompiler;

InputBasic_ContextHandle input;
InputBasic_KeyboardHandle keyboard;
InputBasic_MouseHandle mouse;

enkiTaskSchedulerHandle taskScheduler;

enum AppKey {
	AppKey_Quit
};

ImguiBindings_ContextHandle imguiBindings;

TextureViewerHandle textureViewer;
ImguiBindings_Texture textureToView;
char* lastFolder;

static void* EnkiAlloc(void* userData, size_t size) {
	return MEMORY_ALLOCATOR_MALLOC( (Memory_Allocator*)userData, size );
}
static void EnkiFree(void* userData, void* ptr) {
	MEMORY_ALLOCATOR_FREE( (Memory_Allocator*)userData, ptr );
}

static void LoadTextureToView(char const* fileName)
{
	if(textureToView.cpu != nullptr) {
		Image_Destroy(textureToView.cpu);
		textureToView.cpu = nullptr;
	}
	if(textureToView.gpu != nullptr) {
		TheForge_RemoveTexture(renderer, textureToView.gpu);
		textureToView.gpu = nullptr;
	}

	size_t startOfFileName = 0;
	size_t startOfFileNameExt = 0;

	Os_SplitPath(fileName, &startOfFileName, &startOfFileNameExt);

	MEMORY_FREE(lastFolder);
	lastFolder = (char*) MEMORY_CALLOC(startOfFileName+1,1);
	memcpy(lastFolder, lastFolder, startOfFileName);


	VFile_Handle fh = VFile_FromFile(fileName, Os_FM_ReadBinary);
	if (!fh) {
		LOGERRORF("Load From File failed for %s", fileName);
		return;
	}

	textureToView.cpu = Image_Load(fh);
	VFile_Close(fh);
	if(!textureToView.cpu) {
		LOGERRORF("Image_Load failed for %s", fileName);
		return;
	}

	TinyImageFormat originalFormat = textureToView.cpu->format;
	bool supported = TheForge_CanShaderReadFrom(renderer, textureToView.cpu->format);

	// force CPU for testing if we can
	if(!TinyImageFormat_IsCompressed(originalFormat)) supported = false;

	if(!supported) {
		// convert to R8G8B8A8 for now
		if (!TinyImageFormat_IsCompressed(textureToView.cpu->format)) {
			Image_ImageHeader const* converted = Image_FastConvert(textureToView.cpu, TinyImageFormat_R8G8B8A8_UNORM, true);
			if(converted != textureToView.cpu) {
				Image_Destroy(textureToView.cpu);
				textureToView.cpu = converted;
			}
		} else {
			LOGINFOF("%s with format %s isn't supported by this GPU/backend",
							 fileName,
							 TinyImageFormat_Name(textureToView.cpu->format));
			Image_Destroy(textureToView.cpu);
			textureToView.cpu = nullptr;
			return;
		}
	}

	if(Image_LinkedImageCountOf(textureToView.cpu) > 1) {
		Image_ImageHeader const * packed = Image_PackMipmaps(textureToView.cpu);
		if(textureToView.cpu != packed) {
			Image_Destroy(textureToView.cpu);
			textureToView.cpu = packed;
		}
		ASSERT(Image_HasPackedMipMaps(textureToView.cpu));
	}

	char tmpbuffer[2048];
	sprintf(tmpbuffer, "%s - %ix%i - %s - %s", fileName + startOfFileName,
			textureToView.cpu->width,
			textureToView.cpu->height,
			TinyImageFormat_Name(originalFormat),
			supported ? "GPU" : "CPU"
			);

	TextureViewer_SetWindowName(textureViewer, tmpbuffer);

	// use extended format
	TheForge_RawImageData rawImageData{
			(unsigned char *) Image_RawDataPtr(textureToView.cpu),
			TheForge_IF_NONE,
			textureToView.cpu->width,
			textureToView.cpu->height,
			textureToView.cpu->depth,
			textureToView.cpu->slices,
			(uint32_t) Image_LinkedImageCountOf(textureToView.cpu),
			textureToView.cpu->format,
	};

	TheForge_TextureLoadDesc loadDesc{};
	loadDesc.pRawImageData = &rawImageData;
	loadDesc.pTexture = &textureToView.gpu;
	loadDesc.mCreationFlag = TheForge_TCF_OWN_MEMORY_BIT;
	TheForge_LoadTexture(&loadDesc, false);

}

// Note that shortcuts are currently provided for display only (future version will add flags to BeginMenu to process shortcuts)
static void ShowMenuFile()
{
	if (ImGui::MenuItem("Open", "Ctrl+O")) {
		char* fileName;
		if(NativeFileDialogs_Load("ktx,dds,png,jpg,ppm", lastFolder, &fileName) ) {
			char normalisedPath[2048];
			Os_GetNormalisedPathFromPlatformPath(fileName, normalisedPath, 2048);
			MEMORY_FREE(fileName);
			LoadTextureToView(normalisedPath);
		}

	}
/*	if (ImGui::BeginMenu("Open Recent"))
	{
		ImGui::MenuItem("fish_hat.c");
		ImGui::MenuItem("fish_hat.inl");
		ImGui::MenuItem("fish_hat.h");
		if (ImGui::BeginMenu("More.."))
		{
			ImGui::MenuItem("Hello");
			ImGui::MenuItem("Sailor");
			if (ImGui::BeginMenu("Recurse.."))
			{
				ShowExampleMenuFile();
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}
 */
//	if (ImGui::MenuItem("Save", "Ctrl+S")) {}
//	if (ImGui::MenuItem("Save As..")) {}
	ImGui::Separator();
	if (ImGui::MenuItem("Quit", "Alt+F4")) {
		GameAppShell_Quit();
	}
}

static void ShowAppMainMenuBar()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			ShowMenuFile();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Edit"))
		{
/*			if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
			if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}  // Disabled item
			ImGui::Separator();
			if (ImGui::MenuItem("Cut", "CTRL+X")) {}
			if (ImGui::MenuItem("Copy", "CTRL+C")) {}
			if (ImGui::MenuItem("Paste", "CTRL+V")) {}*/
			ImGui::EndMenu();

		}
		ImGui::EndMainMenuBar();
	}
}


static bool Init() {

#if AL2O3_PLATFORM == AL2O3_PLATFORM_APPLE_MAC
//	Os_SetCurrentDir("../.."); // for xcode, no idea...
	Os_SetCurrentDir("..");
	char currentDir[2048];
	Os_GetCurrentDir(currentDir, 2048);
	LOGINFO(currentDir);
#endif

	// window and renderer setup
	TheForge_RendererDesc desc{
			TheForge_ST_6_0,
			TheForge_GM_SINGLE,
			TheForge_D3D_FL_12_0,
	};

	renderer = TheForge_RendererCreate("Devon", &desc);
	if (!renderer) {
		LOGERROR("TheForge_RendererCreate failed");

		return false;
	}
	shaderCompiler = ShaderCompiler_Create();
	if (!shaderCompiler) {
		LOGERROR("ShaderCompiler_Create failed");
		return false;
	}
#ifndef NDEBUG
	ShaderCompiler_SetOptimizationLevel(shaderCompiler, ShaderCompiler_OPT_None);
#endif
	// change from platform default to vulkan if using the vulkan backend
	if(TheForge_GetRendererApi(renderer) == TheForge_API_VULKAN) {
		ShaderCompiler_SetOutput(shaderCompiler, ShaderCompiler_OT_SPIRV, 13);
	}

	taskScheduler = enkiNewTaskScheduler(&EnkiAlloc, &EnkiFree, &Memory_GlobalAllocator);

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

	static char const DefaultFolder[] = "";
	lastFolder = (char*) MEMORY_CALLOC(strlen(DefaultFolder)+1,1);
	memcpy(lastFolder, DefaultFolder, strlen(DefaultFolder));

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
	if (!imguiBindings) {
		LOGERROR("ImguiBindings_Create failed");
		return false;
	}


	textureViewer = TextureViewer_Create(renderer,
																			 shaderCompiler,
																			 imguiBindings,
																			 FRAMES_AHEAD,
																			 Display_GetBackBufferFormat(display),
																			 Display_GetDepthBufferFormat(display),
																			 Display_IsBackBufferSrgb(display),
																			 TheForge_SC_1,
																			 0);
    if(!textureViewer) {
			LOGERROR("TextureViewer_Create failed");
    	return false;
    }


	return true;
}

static void Update(double deltaMS) {
	GameAppShell_WindowDesc windowDesc;
	GameAppShell_WindowGetCurrentDesc(&windowDesc);


	InputBasic_SetWindowSize(input, windowDesc.width, windowDesc.height);
	ImguiBindings_SetWindowSize(imguiBindings,
															windowDesc.width,
															windowDesc.height,
															windowDesc.dpiBackingScale[0],
															windowDesc.dpiBackingScale[1]);

	InputBasic_Update(input, deltaMS);
	if (InputBasic_GetAsBool(input, AppKey_Quit)) {
		GameAppShell_Quit();
	}

	// Imgui start
	ImguiBindings_UpdateInput(imguiBindings, deltaMS);
	ImGui::NewFrame();

	ShowAppMainMenuBar();
	if(textureToView.cpu != nullptr) {
		TextureViewer_DrawUI(textureViewer, &textureToView);
	}

	bool showDemo = true;
	ImGui::ShowDemoWindow(&showDemo);

	ImGui::EndFrame();
	ImGui::Render();

}

static void Draw(double deltaMS) {

	TheForge_RenderTargetHandle renderTarget;
	TheForge_RenderTargetHandle depthTarget;

	TheForge_CmdHandle cmd = Display_NewFrame(display, &renderTarget, &depthTarget);
	TheForge_RenderTargetDesc const *renderTargetDesc = TheForge_RenderTargetGetDesc(renderTarget);

	TextureViewer_RenderSetup(textureViewer, cmd);

	TheForge_LoadActionsDesc loadActions = {0};
	loadActions.loadActionsColor[0] = TheForge_LA_CLEAR;
	loadActions.clearColorValues[0] = {0.45f, 0.5f, 0.6f, 0.0f};
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

	ImguiBindings_Render(imguiBindings, cmd);


	Display_Present(display);
}

static void Unload() {
	LOGINFO("Unloading");

	TheForge_WaitQueueIdle(graphicsQueue);

	TextureViewer_Destroy(textureViewer);
	if(textureToView.cpu) {
		Image_Destroy(textureToView.cpu);
		textureToView.cpu = nullptr;
	}
	if(textureToView.gpu) {
		TheForge_RemoveTexture(renderer, textureToView.gpu);
		textureToView.gpu = nullptr;
	}

	ImguiBindings_Destroy(imguiBindings);

}

static void Exit() {
	LOGINFO("Exiting");

	MEMORY_FREE(lastFolder);

	InputBasic_MouseDestroy(mouse);
	InputBasic_KeyboardDestroy(keyboard);
	InputBasic_Destroy(input);

	Display_Destroy(display);

	TheForge_RemoveCmdPool(renderer, cmdPool);
	TheForge_RemoveQueue(graphicsQueue);

	enkiDeleteTaskScheduler(taskScheduler);
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
