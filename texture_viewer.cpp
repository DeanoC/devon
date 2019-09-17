#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "gfx_imgui/imgui.h"
#include "gfx_imgui/imgui_internal.h"
#include "al2o3_cadt/vector.h"

#include "render_basics/theforge/api.h"
#include "render_basics/api.h"
#include "render_basics/buffer.h"
#include "render_basics/view.h"
#include "render_basics/framebuffer.h"

#include "texture_viewer.hpp"

struct UniformBuffer {
	float scaleOffsetMatrix[16];
	float colourMask[4];
	float alphaReplicate;
	int32_t forceMipLevel;
	int32_t signedRGB;
	uint32_t sliceToView;
	uint32_t numSlices;
};

static const uint64_t UNIFORM_BUFFER_SIZE_PER_FRAME = 256;

struct TextureViewer {
	Render_RendererHandle renderer;
	Render_FrameBufferHandle frameBuffer;

	TheForge_ShaderHandle shader;
	TheForge_RootSignatureHandle rootSignature;
	TheForge_PipelineHandle pipeline;
	TheForge_DescriptorBinderHandle descriptorBinder;
	Render_BufferHandle uniformBuffer;

	Render_TextureHandle dummy2DTexture;
	Render_TextureHandle dummy2DArrayTexture;
	Render_TextureHandle dummy3DTexture;

	UniformBuffer uniforms;
	bool colourChannelEnable[4];
	float zoom;

	uint32_t currentFrame;
	Render_GraphicsEncoderHandle currentEncoder;

	char *windowName;
};

namespace {

static uint32_t const DummyData[] = {
		0x00FFFF00, 0x00FFFF00, 0x00FFFF00, 0x00FFFF00,
		0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x00800000, 0x00800000, 0x00800000, 0x00800000,
		0x00008000, 0x00008000, 0x00008000, 0x00008000,

		0x80FFFF80, 0x80FFFF80, 0x80FFFF80, 0x80FFFF80,
		0x80000080, 0x80000080, 0x80000080, 0x80000080,
		0x80800080, 0x80800080, 0x80800080, 0x80800080,
		0x80008080, 0x80008080, 0x80008080, 0x80008080,

		0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
		0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF,
		0xFF8000FF, 0xFF8000FF, 0xFF8000FF, 0xFF8000FF,
		0xFF0080FF, 0xFF0080FF, 0xFF0080FF, 0xFF0080FF};

static void CreateDummyTextures(TextureViewer *ctx) {
	TheForge_RawImageData const raw2DImageData{
			(unsigned char *) DummyData,
			TinyImageFormat_R8G8B8A8_UNORM, 4, 4, 1, 1, 1,
			true
	};
	TheForge_RawImageData const raw2DArrayImageData{
			(unsigned char *) DummyData,
			TinyImageFormat_R8G8B8A8_UNORM, 4, 4, 1, 3, 1,
			true
	};
	TheForge_RawImageData const raw3DImageData{
			(unsigned char *) DummyData,
			TinyImageFormat_R8G8B8A8_UNORM, 4, 4, 3, 1, 1,
			true
	};

	TheForge_TextureLoadDesc loadDesc{};
	loadDesc.pRawImageData = &raw2DImageData;
	loadDesc.pTexture = &ctx->dummy2DTexture;
	loadDesc.mCreationFlag = TheForge_TCF_NONE;
	TheForge_LoadTexture(&loadDesc, false);
	loadDesc.pRawImageData = &raw2DArrayImageData;
	loadDesc.pTexture = &ctx->dummy2DArrayTexture;
	loadDesc.mCreationFlag = TheForge_TCF_NONE;
	TheForge_LoadTexture(&loadDesc, false);
	loadDesc.pRawImageData = &raw3DImageData;
	loadDesc.pTexture = &ctx->dummy3DTexture;
	loadDesc.mCreationFlag = TheForge_TCF_NONE;
	TheForge_LoadTexture(&loadDesc, false);

}

bool CreateShaders(TextureViewer *ctx) {

	static char const *const vertEntryPoint = "VS_main";
	static char const *const fragEntryPoint = "FS_main";

	VFile_Handle vfile = VFile_FromFile("textureviewer_vertex.hlsl", Os_FM_Read);
	if (!vfile) {
		return false;
	}
	VFile_Handle ffile = VFile_FromFile("textureviewer_fragment.hlsl", Os_FM_Read);
	if (!ffile) {
		VFile_Close(vfile);
		return false;
	}
	ShaderCompiler_Output vout;
	bool vokay = ShaderCompiler_Compile(
			ctx->renderer->shaderCompiler, ShaderCompiler_ST_VertexShader,
			VFile_GetName(vfile), vertEntryPoint, vfile,
			&vout);
	if (vout.log != nullptr) {
		LOGWARNING("Shader compiler : %s %s", vokay ? "warnings" : "ERROR", vout.log);
	}
	ShaderCompiler_Output fout;
	bool fokay = ShaderCompiler_Compile(
			ctx->renderer->shaderCompiler, ShaderCompiler_ST_FragmentShader,
			VFile_GetName(ffile), fragEntryPoint, ffile,
			&fout);
	if (fout.log != nullptr) {
		LOGWARNING("Shader compiler : %s %s", fokay ? "warnings" : "ERROR", fout.log);
	}
	VFile_Close(vfile);
	VFile_Close(ffile);

	if (!vokay || !fokay) {
		MEMORY_FREE((void *) vout.log);
		MEMORY_FREE((void *) vout.shader);
		MEMORY_FREE((void *) fout.log);
		MEMORY_FREE((void *) fout.shader);
		return false;
	}

#if AL2O3_PLATFORM == AL2O3_PLATFORM_APPLE_MAC
	TheForge_ShaderDesc sdesc;
	sdesc.stages = (TheForge_ShaderStage) (TheForge_SS_FRAG | TheForge_SS_VERT);
	sdesc.vert.name = "ImguiBindings_VertexShader";
	sdesc.vert.code = (char *) vout.shader;
	sdesc.vert.entryPoint = vertEntryPoint;
	sdesc.vert.macroCount = 0;
	sdesc.frag.name = "ImguiBindings_FragmentShader";
	sdesc.frag.code = (char *) fout.shader;
	sdesc.frag.entryPoint = fragEntryPoint;
	sdesc.frag.macroCount = 0;
	TheForge_AddShader(ctx->renderer->renderer, &sdesc, &ctx->shader);
#else
	TheForge_BinaryShaderDesc bdesc;
	bdesc.stages = (TheForge_ShaderStage) (TheForge_SS_FRAG | TheForge_SS_VERT);
	bdesc.vert.byteCode = (char *) vout.shader;
	bdesc.vert.byteCodeSize = (uint32_t) vout.shaderSize;
	bdesc.vert.entryPoint = vertEntryPoint;
	bdesc.frag.byteCode = (char *) fout.shader;
	bdesc.frag.byteCodeSize = (uint32_t) fout.shaderSize;
	bdesc.frag.entryPoint = fragEntryPoint;
	TheForge_AddShaderBinary(ctx->renderer->renderer, &bdesc, &ctx->shader);
#endif
	MEMORY_FREE((void *) vout.log);
	MEMORY_FREE((void *) vout.shader);
	MEMORY_FREE((void *) fout.log);
	MEMORY_FREE((void *) fout.shader);
	return true;
}

} // end anon namespace

TextureViewerHandle TextureViewer_Create(Render_RendererHandle renderer,
																				 Render_FrameBufferHandle frameBuffer) {

	auto ctx = (TextureViewer *) MEMORY_CALLOC(1, sizeof(TextureViewer));
	if (!ctx) {
		return nullptr;
	}

	ctx->renderer = renderer;
	ctx->frameBuffer = frameBuffer;

	if (!CreateShaders(ctx)) {
		MEMORY_FREE(ctx);
		return nullptr;
	}


	TheForge_ShaderHandle shaders[]{ctx->shader};
	TheForge_SamplerHandle samplers[]{
			Render_GetStockSampler(renderer, Render_SST_POINT),
			Render_GetStockSampler(renderer, Render_SST_LINEAR),
	};

	char const *staticSamplerNames[]{"pointSampler", "bilinearSampler"};
	TheForge_RootSignatureDesc rootSignatureDesc{};
	rootSignatureDesc.shaderCount = 1;
	rootSignatureDesc.pShaders = shaders;
	rootSignatureDesc.staticSamplerCount = 2;
	rootSignatureDesc.pStaticSamplerNames = staticSamplerNames;
	rootSignatureDesc.pStaticSamplers = samplers;
	TheForge_AddRootSignature(ctx->renderer->renderer, &rootSignatureDesc, &ctx->rootSignature);
	if (!ctx->rootSignature) {
		return nullptr;
	}

	TheForge_PipelineDesc pipelineDesc{};
	TinyImageFormat colourFormats[] = {Render_FrameBufferColourFormat(frameBuffer)};
	pipelineDesc.type = TheForge_PT_GRAPHICS;
	TheForge_GraphicsPipelineDesc &gfxPipeDesc = pipelineDesc.graphicsDesc;
	gfxPipeDesc.shaderProgram = ctx->shader;
	gfxPipeDesc.rootSignature = ctx->rootSignature;
	gfxPipeDesc.pVertexLayout = Render_GetStockVertexLayout(renderer, Render_SVL_2D_COLOUR_UV);
	gfxPipeDesc.blendState = Render_GetStockBlendState(renderer, Render_SBS_PORTER_DUFF);
	gfxPipeDesc.depthState = Render_GetStockDepthState(renderer, Render_SDS_IGNORE);
	gfxPipeDesc.rasterizerState = Render_GetStockRasterisationState(renderer, Render_SRS_NOCULL);
	gfxPipeDesc.renderTargetCount = 1;
	gfxPipeDesc.pColorFormats = colourFormats;
	gfxPipeDesc.depthStencilFormat = TinyImageFormat_UNDEFINED;
	gfxPipeDesc.sampleCount = TheForge_SC_1;
	gfxPipeDesc.sampleQuality = 0;
	gfxPipeDesc.primitiveTopo = TheForge_PT_TRI_LIST;
	TheForge_AddPipeline(ctx->renderer->renderer, &pipelineDesc, &ctx->pipeline);
	if (!ctx->pipeline) {
		return nullptr;
	}

	TheForge_DescriptorBinderDesc descriptorBinderDesc[] = {
			{ctx->rootSignature, 20},
			{ctx->rootSignature, 20},
	};
	TheForge_AddDescriptorBinder(ctx->renderer->renderer, 0, 2, descriptorBinderDesc, &ctx->descriptorBinder);
	if (!ctx->descriptorBinder) {
		return nullptr;
	}

	static Render_BufferUniformDesc const ubDesc{
			UNIFORM_BUFFER_SIZE_PER_FRAME,
			true
	};

	ctx->uniformBuffer = Render_BufferCreateUniform(ctx->renderer, &ubDesc);
	if (!ctx->uniformBuffer) {
		return nullptr;
	}

	CreateDummyTextures(ctx);

	// defaults
	ctx->colourChannelEnable[0] = true;
	ctx->colourChannelEnable[1] = true;
	ctx->colourChannelEnable[2] = true;
	ctx->colourChannelEnable[3] = false;
	ctx->zoom = 1.0f;

	static char const DefaultName[] = "Texture Viewer";
	ctx->windowName = (char *) MEMORY_CALLOC(strlen(DefaultName) + 1, 1);
	memcpy(ctx->windowName, DefaultName, strlen(DefaultName));

	return ctx;
}

void TextureViewer_Destroy(TextureViewerHandle handle) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx) {
		return;
	}

	MEMORY_FREE(ctx->windowName);

	if (ctx->dummy3DTexture) {
		TheForge_RemoveTexture(ctx->renderer->renderer, ctx->dummy3DTexture);
	}
	if (ctx->dummy2DArrayTexture) {
		TheForge_RemoveTexture(ctx->renderer->renderer, ctx->dummy2DArrayTexture);
	}
	if (ctx->dummy2DTexture) {
		TheForge_RemoveTexture(ctx->renderer->renderer, ctx->dummy2DTexture);
	}

	if (ctx->uniformBuffer) {
		Render_BufferDestroy(ctx->renderer, ctx->uniformBuffer);
	}
	if (ctx->descriptorBinder) {
		TheForge_RemoveDescriptorBinder(ctx->renderer->renderer, ctx->descriptorBinder);
	}
	if (ctx->pipeline) {
		TheForge_RemovePipeline(ctx->renderer->renderer, ctx->pipeline);
	}
	if (ctx->rootSignature) {
		TheForge_RemoveRootSignature(ctx->renderer->renderer, ctx->rootSignature);
	}
	if (ctx->shader) {
		TheForge_RemoveShader(ctx->renderer->renderer, ctx->shader);
	}

	MEMORY_FREE(ctx);
}
static void ImCallback(ImDrawList const *list, ImDrawCmd const *imcmd) {

	if (imcmd->TextureId == nullptr) {
		return;
	}

	auto ctx = (TextureViewer *) imcmd->UserCallbackData;

	ImguiBindings_Texture const *texture = (ImguiBindings_Texture *) imcmd->TextureId;

	ImDrawData *drawData = ImGui::GetDrawData();
	ImVec2 displayPos = drawData->DisplayPos;
	displayPos.x *= drawData->FramebufferScale.x;
	displayPos.y *= drawData->FramebufferScale.y;

	TheForge_CmdBindPipeline(ctx->currentEncoder->cmd, ctx->pipeline);

	uint64_t const baseUniformOffset = ctx->currentFrame * UNIFORM_BUFFER_SIZE_PER_FRAME;

	TheForge_DescriptorData params[3]{};
	params[0].pName = "uniformBlock";
	params[0].pBuffers = &ctx->uniformBuffer->buffer;
	params[0].pOffsets = &baseUniformOffset;
	params[0].count = 1;
	if (Image_IsArray(texture->cpu)) {
		params[1].pName = "colourTexture";
		params[1].pTextures = &ctx->dummy2DTexture;
		params[1].count = 1;
		params[2].pName = "colourTextureArray";
		params[2].pTextures = &(texture->gpu);
		params[2].count = 1;
	} else {
		params[1].pName = "colourTexture";
		params[1].pTextures = &(texture->gpu);
		params[1].count = 1;
		params[2].pName = "colourTextureArray";
		params[2].pTextures = &ctx->dummy2DArrayTexture;
		params[2].count = 1;
	}
	TheForge_CmdBindDescriptors(ctx->currentEncoder->cmd, ctx->descriptorBinder, ctx->rootSignature, 3, params);

	float const clipX = imcmd->ClipRect.x * drawData->FramebufferScale.x;
	float const clipY = imcmd->ClipRect.y * drawData->FramebufferScale.y;
	float const clipZ = imcmd->ClipRect.z * drawData->FramebufferScale.x;
	float const clipW = imcmd->ClipRect.w * drawData->FramebufferScale.y;

	TheForge_CmdSetScissor(ctx->currentEncoder->cmd,
												 (uint32_t) (clipX - displayPos.x),
												 (uint32_t) (clipY - displayPos.y),
												 (uint32_t) (clipZ - clipX),
												 (uint32_t) (clipW - clipY));

	TheForge_CmdDrawIndexed(ctx->currentEncoder->cmd, 6, imcmd->IdxOffset, imcmd->VtxOffset);
}

void TextureViewer_DrawUI(TextureViewerHandle handle, ImguiBindings_Texture *texture) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx) {
		return;
	}

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize;

	ImGui::Begin(ctx->windowName, nullptr, window_flags);

	ImGuiWindow *window = ImGui::GetCurrentWindow();
	ImDrawList *drawList = ImGui::GetWindowDrawList();
	if (window->SkipItems || !texture) {
		ImGui::End();
		return;
	}

	ImGui::Checkbox("R", ctx->colourChannelEnable + 0);
	ImGui::SameLine();
	ImGui::Checkbox("G", ctx->colourChannelEnable + 1);
	ImGui::SameLine();
	ImGui::Checkbox("B", ctx->colourChannelEnable + 2);
	ImGui::SameLine();
	ImGui::Checkbox("A", ctx->colourChannelEnable + 3);
	ImGui::SameLine();

	ImGui::SliderFloat("Zoom", &ctx->zoom, 1.0f / texture->cpu->width, 256.0f, "%.3f", 2);
	ImVec2 rb{window->DC.CursorPos.x + (texture->cpu->width * ctx->zoom),
						window->DC.CursorPos.y + (texture->cpu->height * ctx->zoom)};
	ImRect const bb(window->DC.CursorPos, rb);

	drawList->PushTextureID(texture);
	drawList->PrimReserve(6, 4);
	drawList->PrimRectUV(bb.Min, bb.Max, {0, 0}, {1, 1}, 0xFFFFFFFF);
	drawList->CmdBuffer.back().ElemCount = 0; // stop the rect rendering instead do a callback
	drawList->AddCallback(&ImCallback, handle);
	drawList->PopTextureID();

	// size of the auto size window takes
	if (rb.y < window->DC.CursorPos.y + 32.0f) {
		rb.y = window->DC.CursorPos.y + 32.0f;
	}
	ImRect const bb2(window->DC.CursorPos, rb);
	ImGui::ItemSize(bb2);

	int forceMipLevel = 0;
	int sliceToView = 0;
	bool signedRGB = false;
	if (Image_MipMapCountOf(texture->cpu) > 1) {
		forceMipLevel = (int) ctx->uniforms.forceMipLevel;
		ImGui::SameLine();
		ImGui::VSliderInt("Mipmap Level", ImVec2(20.0f, 100.0f),
											&forceMipLevel, 0, (int) Image_LinkedImageCountOf(texture->cpu) - 1);
	}
	ctx->uniforms.forceMipLevel = (int32_t) forceMipLevel;
	if (texture->cpu->slices > 1) {
		sliceToView = (int) ctx->uniforms.sliceToView;
		ImGui::SameLine();
		ImGui::VSliderInt("Slice", ImVec2(20.0f, 100.0f),
											&sliceToView, 0, (int) texture->cpu->slices - 1);
	}
	if (TinyImageFormat_IsSigned(texture->cpu->format)) {
		signedRGB = (bool) ctx->uniforms.signedRGB;
		ImGui::SameLine();
		ImGui::Checkbox("Signed decode", &signedRGB);
	}

	ctx->uniforms.numSlices = texture->cpu->slices;
	ctx->uniforms.sliceToView = (uint32_t) sliceToView;
	ctx->uniforms.signedRGB = signedRGB;

	ImGui::End();
}

void TextureViewer_RenderSetup(TextureViewerHandle handle, Render_GraphicsEncoderHandle encoder) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx) {
		return;
	}

	static_assert(sizeof(UniformBuffer) < UNIFORM_BUFFER_SIZE_PER_FRAME);

	ctx->currentFrame = (ctx->currentFrame + 1) % ctx->frameBuffer->frameBufferCount;

	uint64_t const baseUniformOffset = ctx->currentFrame * UNIFORM_BUFFER_SIZE_PER_FRAME;
	memcpy(ctx->uniforms.scaleOffsetMatrix,
				 ImguiBindings_GetScaleOffsetMatrix(ctx->frameBuffer->imguiBindings),
				 sizeof(float) * 16);

	if (!ctx->colourChannelEnable[0] &&
			!ctx->colourChannelEnable[1] &&
			!ctx->colourChannelEnable[2] &&
			ctx->colourChannelEnable[3]) {
		ctx->uniforms.alphaReplicate = 1.0f;
	} else {
		ctx->uniforms.alphaReplicate = 0.0f;
		ctx->uniforms.colourMask[0] = ctx->colourChannelEnable[0] ? 1.0f : 0.0f;
		ctx->uniforms.colourMask[1] = ctx->colourChannelEnable[1] ? 1.0f : 0.0f;
		ctx->uniforms.colourMask[2] = ctx->colourChannelEnable[2] ? 1.0f : 0.0f;
		ctx->uniforms.colourMask[3] = ctx->colourChannelEnable[3] ? 1.0f : 0.0f;
	}

	Render_BufferUpdateDesc uniformUpdate = {
			&ctx->uniforms,
			0,
			UNIFORM_BUFFER_SIZE_PER_FRAME
	};
	Render_BufferUpload(ctx->uniformBuffer, &uniformUpdate);

	ctx->currentEncoder = encoder;
}

void TextureViewer_SetWindowName(TextureViewerHandle handle, char const *windowName) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx) {
		return;
	}

	MEMORY_FREE(ctx->windowName);
	ctx->windowName = (char *) MEMORY_CALLOC(strlen(windowName) + 1, 1);
	memcpy(ctx->windowName, windowName, strlen(windowName));

}

void TextureViewer_SetZoom(TextureViewerHandle handle, float zoom) {

	auto ctx = (TextureViewer *) handle;
	if (!ctx) {
		return;
	}

	ctx->zoom = zoom;
}
