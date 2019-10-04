#include "al2o3_platform/platform.h"
#include "al2o3_platform/visualdebug.h"
#include "al2o3_memory/memory.h"
#include "al2o3_enki/TaskScheduler_c.h"
#include "al2o3_cadt/freelist.h"
#include "al2o3_cadt/vector.h"

#include "gfx_imageio/io.h"
#include "gfx_image/utils.h"
#include "gfx_imagedecompress/imagedecompress.h"
#include "utils_gameappshell/gameappshell.h"
#include "utils_simple_logmanager/logmanager.h"
#include "al2o3_vfile/vfile.h"
#include "al2o3_os/filesystem.h"
#include "input_basic/input.h"

#include "render_basics/api.h"
#include "render_basics/framebuffer.h"
#include "render_basics/texture.h"

#include "gfx_imgui/imgui.h"
#include "utils_nativefiledialogs/dialogs.h"
#include <cstdio> // for sprintf

#include "texture_viewer.hpp"
#include "about.h"

static SimpleLogManager_Handle g_logger;
static int g_returnCode;

Render_RendererHandle renderer;
Render_QueueHandle graphicsQueue;
Render_FrameBufferHandle frameBuffer;

InputBasic_ContextHandle input;
InputBasic_KeyboardHandle keyboard;
InputBasic_MouseHandle mouse;

enkiTaskSchedulerHandle taskScheduler;
char *lastFolder;

enum AppKey {
	AppKey_Quit
};

static const int MAX_TEXTURE_WINDOWS = 100;
static const int MAX_INPUT_PATH_LENGTH = 1024;

struct TextureWindow {
	TextureViewerHandle textureViewer;
	TextureViewer_Texture textureToView;
};

void LoadTexture(char const *fileName);
CADT_FreeListHandle textureWindowFreeList;
CADT_VectorHandle textureWindows;
CADT_VectorHandle fileToOpenQueue;

static void *EnkiAlloc(void *userData, size_t size) {
	return MEMORY_ALLOCATOR_MALLOC((Memory_Allocator *) userData, size);
}
static void EnkiFree(void *userData, void *ptr) {
	MEMORY_ALLOCATOR_FREE((Memory_Allocator *) userData, ptr);
}

static void LoadTextureToView(char const *fileName, TextureWindow *tw) {
	if (tw->textureToView.cpu != nullptr) {
		Image_Destroy(tw->textureToView.cpu);
		tw->textureToView.cpu = nullptr;
	}
	if (tw->textureToView.gpu != nullptr) {
		Render_TextureDestroy(renderer, tw->textureToView.gpu);
		tw->textureToView.gpu = nullptr;
	}

	size_t startOfFileName = 0;
	size_t startOfFileNameExt = 0;

	Os_SplitPath(fileName, &startOfFileName, &startOfFileNameExt);

	MEMORY_FREE(lastFolder);
	lastFolder = (char *) MEMORY_CALLOC(startOfFileName + 1, 1);
	memcpy(lastFolder, fileName, startOfFileName);

	VFile_Handle fh = VFile_FromFile(fileName, Os_FM_ReadBinary);
	if (!fh) {
		LOGINFO("Load From File failed for %s", fileName);
		return;
	}

	tw->textureToView.cpu = Image_Load(fh);
	VFile_Close(fh);
	if (!tw->textureToView.cpu) {
		LOGINFO("Image_Load failed for %s", fileName);
		return;
	}

	TinyImageFormat originalFormat = tw->textureToView.cpu->format;
	bool supported = Render_RendererCanShaderReadFrom(renderer, tw->textureToView.cpu->format);

	// force CPU for testing
	// supported = false;

	if (!supported) {
		// convert to R8G8B8A8 for now
		if (!TinyImageFormat_IsCompressed(tw->textureToView.cpu->format)) {
			Image_ImageHeader const *converted = tw->textureToView.cpu;
			if (TinyImageFormat_IsSigned(tw->textureToView.cpu->format)) {
				converted = Image_FastConvert(tw->textureToView.cpu, TinyImageFormat_R8G8B8A8_SNORM, true);
			} else {
				converted = Image_FastConvert(tw->textureToView.cpu, TinyImageFormat_R8G8B8A8_UNORM, true);
			}
			if (converted != tw->textureToView.cpu) {
				Image_Destroy(tw->textureToView.cpu);
				tw->textureToView.cpu = converted;
			}
		} else {
			Image_ImageHeader const *converted = tw->textureToView.cpu;
			converted = Image_Decompress(tw->textureToView.cpu);
			if (converted == nullptr || converted == tw->textureToView.cpu) {
				LOGINFO("%s with format %s isn't supported by this GPU/backend and can't be converted",
								fileName,
								TinyImageFormat_Name(tw->textureToView.cpu->format));
				Image_Destroy(tw->textureToView.cpu);
				tw->textureToView.cpu = nullptr;
				return;
			} else {
				Image_Destroy(tw->textureToView.cpu);
				tw->textureToView.cpu = converted;
			}
		}
	}

	if (Image_MipMapCountOf(tw->textureToView.cpu) > 1) {
		Image_ImageHeader const *packed = Image_PackMipmaps(tw->textureToView.cpu);
		if (tw->textureToView.cpu != packed) {
			Image_Destroy(tw->textureToView.cpu);
			tw->textureToView.cpu = packed;
		}
		ASSERT(Image_HasPackedMipMaps(tw->textureToView.cpu));
	}

	static int uniqueHiddenNumber = 0;

	char tmpbuffer[2048];
	sprintf(tmpbuffer, "%s - %ix%i - %s - %s ##%i", fileName + startOfFileName,
					tw->textureToView.cpu->width,
					tw->textureToView.cpu->height,
					TinyImageFormat_Name(originalFormat),
					supported ? "GPU" : "CPU",
					uniqueHiddenNumber++
	);

	TextureViewer_SetWindowName(tw->textureViewer, tmpbuffer);
	TextureViewer_SetZoom(tw->textureViewer, 768.0f / tw->textureToView.cpu->width);

	Render_TextureCreateDesc createGPUDesc{
			tw->textureToView.cpu->format,
			Render_TUF_SHADER_READ,
			tw->textureToView.cpu->width,
			tw->textureToView.cpu->height,
			tw->textureToView.cpu->depth,
			tw->textureToView.cpu->slices,
			(uint32_t) Image_MipMapCountOf(tw->textureToView.cpu),
			0,
			0,
			(unsigned char *) Image_RawDataPtr(tw->textureToView.cpu),
			fileName + startOfFileName,
	};

	tw->textureToView.gpu = Render_TextureSyncCreate(renderer, &createGPUDesc);
}

// Note that shortcuts are currently provided for display only (future version will add flags to BeginMenu to process shortcuts)
static void ShowMenuFile() {
	if (ImGui::MenuItem("Open", "Ctrl+O")) {
		char *fileName;
		if (NativeFileDialogs_Load("ktx,dds,exr,hdr,jpg,jpeg,png,tga,bmp,psd,gif,pic,pnm,ppm,basis",
															 lastFolder, &fileName)) {
			LoadTexture(fileName);
			MEMORY_FREE(fileName);
		}

	}
	ImGui::Separator();
	if (ImGui::MenuItem("Quit", "Alt+F4")) {
		GameAppShell_Quit();
	}
}

void LoadTexture(char const *fileName) {
	if (fileName == nullptr) {
		return;
	}
	char normalisedPath[2048];
	Os_GetNormalisedPathFromPlatformPath(fileName, normalisedPath, 2048);
	auto textureWindow = (TextureWindow *) CADT_FreeListAlloc(textureWindowFreeList);
	if (textureWindow) {
		textureWindow->textureViewer = TextureViewer_Create(renderer, frameBuffer);
		if (!textureWindow->textureViewer) {
			CADT_FreeListFree(textureWindowFreeList, textureWindow);
			LOGERROR("TextureViewer_Create failed");
			return;
		}
		memset(&textureWindow->textureToView, 0, sizeof(TextureViewer_Texture));
		LoadTextureToView(normalisedPath, textureWindow);
		CADT_VectorPushElement(textureWindows, &textureWindow);
	}
}

static void ShowAppMainMenuBar() {
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			ShowMenuFile();
			ImGui::EndMenu();
		}
		if (ImGui::Button("About")) {
			About_Open();
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
	fbDesc.queue = graphicsQueue = Render_RendererGetPrimaryQueue(renderer, Render_QT_GRAPHICS);
	fbDesc.commandPool = Render_RendererGetPrimaryCommandPool(renderer, Render_QT_GRAPHICS);
	fbDesc.frameBufferWidth = windowDesc.width;
	fbDesc.frameBufferHeight = windowDesc.height;
	fbDesc.colourFormat = TinyImageFormat_UNDEFINED;
	fbDesc.depthFormat = TinyImageFormat_UNDEFINED;
	fbDesc.embeddedImgui = true;
	fbDesc.visualDebugTarget = true;
	frameBuffer = Render_FrameBufferCreate(renderer, &fbDesc);

	static char const DefaultFolder[] = "";
	lastFolder = (char *) MEMORY_CALLOC(strlen(DefaultFolder) + 1, 1);
	memcpy(lastFolder, DefaultFolder, strlen(DefaultFolder));

	textureWindowFreeList = CADT_FreeListCreate(sizeof(TextureWindow), MAX_TEXTURE_WINDOWS);
	textureWindows = CADT_VectorCreate(sizeof(TextureWindow *));

	return true;
}

static bool Load() {
	while (!CADT_VectorIsEmpty(fileToOpenQueue)) {
		char path[MAX_INPUT_PATH_LENGTH];
		CADT_VectorPopElement(fileToOpenQueue, path);
		LoadTexture(path);
	}

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

	About_Display();

	ShowAppMainMenuBar();

	TextureWindow *toClose[MAX_TEXTURE_WINDOWS];
	uint32_t closeCount = 0;
	for (auto i = 0u; i < CADT_VectorSize(textureWindows); ++i) {
		auto textureWindow = *(TextureWindow **) CADT_VectorAt(textureWindows, i);
		ASSERT(textureWindow);

		if (textureWindow->textureToView.cpu != nullptr) {
			bool keepOpen = TextureViewer_DrawUI(textureWindow->textureViewer, &textureWindow->textureToView);
			if (!keepOpen) {
				toClose[closeCount++] = textureWindow;
			}
		} else {
			toClose[closeCount++] = textureWindow;
		}
	}
	for (auto i = 0u; i < closeCount; ++i) {
		auto textureWindow = (TextureWindow *) toClose[i];
		ASSERT(textureWindow);

		TextureViewer_Destroy(textureWindow->textureViewer);
		textureWindow->textureViewer = nullptr;
		if (textureWindow->textureToView.cpu) {
			Image_Destroy(textureWindow->textureToView.cpu);
			textureWindow->textureToView.cpu = nullptr;
		}

		if (textureWindow->textureToView.gpu) {
			Render_TextureDestroy(renderer, textureWindow->textureToView.gpu);
			textureWindow->textureToView.gpu = nullptr;
		}

		TextureViewer_Destroy(textureWindow->textureViewer);
		textureWindow->textureViewer = nullptr;
		CADT_VectorRemove(textureWindows, CADT_VectorFind(textureWindows, &textureWindow));
		CADT_FreeListFree(textureWindowFreeList, textureWindow);
	}


	//	static bool demoWindow = false;
	//	ImGui::ShowDemoWindow(&demoWindow);

	ImGui::EndFrame();
	ImGui::Render();

}

static void Draw(double deltaMS) {

	Render_RenderTargetHandle renderTargets[2] = {nullptr, nullptr};

	Render_FrameBufferNewFrame(frameBuffer);

	for (auto i = 0u; i < CADT_VectorSize(textureWindows); ++i) {
		auto textureWindow = *(TextureWindow **) CADT_VectorAt(textureWindows, i);
		ASSERT(textureWindow);
		TextureViewer_RenderSetup(textureWindow->textureViewer, Render_FrameBufferGraphicsEncoder(frameBuffer));
	}

	Render_FrameBufferPresent(frameBuffer);
}

static void Unload() {
	LOGINFO("Unloading");

	Render_QueueWaitIdle(graphicsQueue);

	About_Close();
}

static void Exit() {
	LOGINFO("Exiting");

	MEMORY_FREE(lastFolder);

	for (auto i = 0u; i < CADT_VectorSize(textureWindows); ++i) {
		auto textureWindow = *(TextureWindow **) CADT_VectorAt(textureWindows, i);
		ASSERT(textureWindow);
		TextureViewer_Destroy(textureWindow->textureViewer);
		textureWindow->textureViewer = nullptr;
		if (textureWindow->textureToView.cpu) {
			Image_Destroy(textureWindow->textureToView.cpu);
			textureWindow->textureToView.cpu = nullptr;
		}
		if (textureWindow->textureToView.gpu) {
			Render_TextureDestroy(renderer, textureWindow->textureToView.gpu);
			textureWindow->textureToView.gpu = nullptr;
		}

		TextureViewer_Destroy(textureWindow->textureViewer);
		textureWindow->textureViewer = nullptr;
	}
	// no need to pop each element as destroying
	CADT_VectorDestroy(textureWindows);
	CADT_FreeListDestroy(textureWindowFreeList);

	InputBasic_MouseDestroy(mouse);
	InputBasic_KeyboardDestroy(keyboard);
	InputBasic_Destroy(input);

	Render_FrameBufferDestroy(renderer, frameBuffer);

	enkiDeleteTaskScheduler(taskScheduler);
	Render_RendererDestroy(renderer);


	CADT_VectorDestroy(fileToOpenQueue);

	SimpleLogManager_Free(g_logger);

	Memory_TrackerDestroyAndLogLeaks();

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
	g_logger = SimpleLogManager_Alloc();

	fileToOpenQueue = CADT_VectorCreate(MAX_INPUT_PATH_LENGTH);
	if (!fileToOpenQueue) {
		SimpleLogManager_Free(g_logger);
		return 11;
	}

	CADT_VectorReserve(fileToOpenQueue, argc - 1);
	for (auto i = 0u; i < argc - 1; ++i) {
		CADT_VectorPushElement(fileToOpenQueue, (void *) argv[1 + i]);
	}

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

	GameAppShell_MainLoop(argc, argv);

	return 	g_returnCode;
}
