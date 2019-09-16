#include <cstdio> // for sprintf
#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_enki/TaskScheduler_c.h"
#include "gfx_theforge/theforge.h"
#include "gfx_imageio/io.h"
#include "gfx_image/utils.h"
#include "utils_gameappshell/gameappshell.h"
#include "utils_simple_logmanager/logmanager.h"
#include "al2o3_vfile/vfile.h"
#include "al2o3_os/filesystem.h"
#include "input_basic/input.h"

#include "render_basics/theforge/api.h"
#include "render_basics/api.h"
#include "render_basics/cmd.h"
#include "render_basics/view.h"
#include "render_basics/framebuffer.h"

#include "gfx_imgui_al2o3_theforge_bindings/bindings.h"
#include "gfx_imgui/imgui.h"
#include "utils_nativefiledialogs/dialogs.h"

#include "texture_viewer.hpp"
#include "gfx_imagedecompress/imagedecompress.h"

Render_RendererHandle renderer;
Render_QueueHandle graphicsQueue;
Render_CmdPoolHandle cmdPool;
Render_FrameBufferHandle frameBuffer;

InputBasic_ContextHandle input;
InputBasic_KeyboardHandle keyboard;
InputBasic_MouseHandle mouse;

enkiTaskSchedulerHandle taskScheduler;

enum AppKey {
	AppKey_Quit
};

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
		TheForge_RemoveTexture(renderer->renderer, textureToView.gpu);
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
		LOGINFO("Load From File failed for %s", fileName);
		return;
	}

	textureToView.cpu = Image_Load(fh);
	VFile_Close(fh);
	if(!textureToView.cpu) {
		LOGINFO("Image_Load failed for %s", fileName);
		return;
	}

	TinyImageFormat originalFormat = textureToView.cpu->format;
	bool supported = TheForge_CanShaderReadFrom(renderer->renderer, textureToView.cpu->format);

	// force CPU for testing
	// supported = false;

	if(!supported) {
		// convert to R8G8B8A8 for now
		if (!TinyImageFormat_IsCompressed(textureToView.cpu->format)) {
			Image_ImageHeader const* converted = textureToView.cpu;
			if(TinyImageFormat_IsSigned(textureToView.cpu->format)) {
				converted = Image_FastConvert(textureToView.cpu, TinyImageFormat_R8G8B8A8_SNORM, true);
			} else {
				converted = Image_FastConvert(textureToView.cpu, TinyImageFormat_R8G8B8A8_UNORM, true);
			}
			if(converted != textureToView.cpu) {
				Image_Destroy(textureToView.cpu);
				textureToView.cpu = converted;
			}
		} else {
			Image_ImageHeader const* converted = textureToView.cpu;
			converted = Image_Decompress(textureToView.cpu);
			if(converted == nullptr || converted == textureToView.cpu ) {
				LOGINFO("%s with format %s isn't supported by this GPU/backend and can't be converted",
								 fileName,
								 TinyImageFormat_Name(textureToView.cpu->format));
				Image_Destroy(textureToView.cpu);
				textureToView.cpu = nullptr;
				return;
			} else {
				Image_Destroy(textureToView.cpu);
				textureToView.cpu = converted;
			}
		}
	}

	if(Image_MipMapCountOf(textureToView.cpu) > 1) {
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
	TextureViewer_SetZoom(textureViewer, 768.0f / textureToView.cpu->width);

	// use extended format
	TheForge_RawImageData rawImageData{
			(unsigned char *) Image_RawDataPtr(textureToView.cpu),
			textureToView.cpu->format,
			textureToView.cpu->width,
			textureToView.cpu->height,
			textureToView.cpu->depth,
			textureToView.cpu->slices,
			(uint32_t) Image_LinkedImageCountOf(textureToView.cpu),
			true
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
		if(NativeFileDialogs_Load("ktx,dds,exr,hdr,jpg,jpeg,png,tga,bmp,psd,gif,pic,pnm,ppm", lastFolder, &fileName) ) {
			if(fileName != nullptr) {
				char normalisedPath[2048];
				Os_GetNormalisedPathFromPlatformPath(fileName, normalisedPath, 2048);
				MEMORY_FREE(fileName);
				LoadTextureToView(normalisedPath);
			}
		}

	}
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
	// setup basic input and map quit key
	input = InputBasic_Create();
	uint32_t userIdBlk = InputBasic_AllocateUserIdBlock(input); // 1st 1000 id are the apps
	ASSERT(userIdBlk == 0);

	renderer = Render_RendererCreate(input);
	if (!renderer) {
		LOGERROR("Render_RendererCreate failed");

		return false;
	}

	taskScheduler = enkiNewTaskScheduler(&EnkiAlloc, &EnkiFree, &Memory_GlobalAllocator);

	GameAppShell_WindowDesc windowDesc;
	GameAppShell_WindowGetCurrentDesc(&windowDesc);

	if (InputBasic_GetKeyboardCount(input) > 0) {
		keyboard = InputBasic_KeyboardCreate(input, 0);
	}
	if (InputBasic_GetMouseCount(input) > 0) {
		mouse = InputBasic_MouseCreate(input, 0);
	}
	if (keyboard) {
		InputBasic_MapToKey(input, AppKey_Quit, keyboard, InputBasic_Key_Escape);
	}

	Render_FrameBufferDesc fbDesc{};
	fbDesc.platformHandle = GameAppShell_GetPlatformWindowPtr();
	fbDesc.queue = graphicsQueue = Render_RendererGetPrimaryQueue(renderer, Render_GQT_GRAPHICS);
	fbDesc.commandPool = Render_RendererGetPrimaryCommandPool(renderer, Render_GQT_GRAPHICS);
	fbDesc.frameBufferWidth = windowDesc.width;
	fbDesc.frameBufferHeight = windowDesc.height;
	fbDesc.frameBufferCount = 3;
	fbDesc.colourFormat = TinyImageFormat_UNDEFINED;
	fbDesc.depthFormat = TinyImageFormat_UNDEFINED;
	fbDesc.embeddedImgui = true;
	frameBuffer = Render_FrameBufferCreate(renderer, &fbDesc);

	static char const DefaultFolder[] = "";
	lastFolder = (char*) MEMORY_CALLOC(strlen(DefaultFolder)+1,1);
	memcpy(lastFolder, DefaultFolder, strlen(DefaultFolder));

	textureViewer = TextureViewer_Create(renderer, frameBuffer);
	if(!textureViewer) {
		LOGERROR("TextureViewer_Create failed");
		return false;
	}

	return true;
}

static bool Load() {

	return true;
}

static void Update(double deltaMS) {
	GameAppShell_WindowDesc windowDesc;
	GameAppShell_WindowGetCurrentDesc(&windowDesc);


	InputBasic_SetWindowSize(input, windowDesc.width, windowDesc.height);
	InputBasic_Update(input, deltaMS);
	if (InputBasic_GetAsBool(input, AppKey_Quit)) {
		GameAppShell_Quit();
	}

	Render_FrameBufferUpdate(frameBuffer,
													 windowDesc.width, windowDesc.height,
													 windowDesc.dpiBackingScale[0],
													 windowDesc.dpiBackingScale[1],
													 deltaMS);

	ImGui::NewFrame();

	ShowAppMainMenuBar();
	if(textureToView.cpu != nullptr) {
		TextureViewer_DrawUI(textureViewer, &textureToView);
	}

	ImGui::EndFrame();
	ImGui::Render();

}

static void Draw(double deltaMS) {

	Render_RenderTargetHandle renderTargets[2] = {nullptr, nullptr};

	Render_CmdHandle cmd = Render_FrameBufferNewFrame(frameBuffer, renderTargets + 0, renderTargets + 1);

	TextureViewer_RenderSetup(textureViewer, cmd);

	Render_FrameBufferPresent(frameBuffer);
}

static void Unload() {
	LOGINFO("Unloading");

	TheForge_WaitQueueIdle(graphicsQueue);
}

static void Exit() {
	LOGINFO("Exiting");

	MEMORY_FREE(lastFolder);


	TextureViewer_Destroy(textureViewer); textureViewer = nullptr;
	if(textureToView.cpu) {
		Image_Destroy(textureToView.cpu);
		textureToView.cpu = nullptr;
	}
	if(textureToView.gpu) {
		TheForge_RemoveTexture(renderer->renderer, textureToView.gpu);
		textureToView.gpu = nullptr;
	}

	TextureViewer_Destroy(textureViewer); textureViewer = nullptr;

	InputBasic_MouseDestroy(mouse);
	InputBasic_KeyboardDestroy(keyboard);
	InputBasic_Destroy(input);

	Render_FrameBufferDestroy(renderer, frameBuffer);

	enkiDeleteTaskScheduler(taskScheduler);
	Render_RendererDestroy(renderer);
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
