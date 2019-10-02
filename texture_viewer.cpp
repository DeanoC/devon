#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "gfx_image/image.h"
#include "gfx_imgui/imgui.h"
#include "gfx_imgui/imgui_internal.h"
#include "al2o3_cadt/vector.h"

#include "render_basics/api.h"
#include "render_basics/buffer.h"
#include "render_basics/descriptorset.h"
#include "render_basics/framebuffer.h"
#include "render_basics/graphicsencoder.h"
#include "render_basics/rootsignature.h"
#include "render_basics/pipeline.h"
#include "render_basics/shader.h"
#include "render_basics/texture.h"
#include "render_basics/view.h"

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

	Render_ShaderHandle shader;
	Render_RootSignatureHandle rootSignature;
	Render_GraphicsPipelineHandle pipeline;
	Render_DescriptorSetHandle descriptorSet;
	Render_BufferHandle uniformBuffer;

	Render_TextureHandle dummy2DTexture;
	Render_TextureHandle dummy2DArrayTexture;
	Render_TextureHandle dummy3DTexture;

	UniformBuffer uniforms;
	bool colourChannelEnable[4];
	float zoom;

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
	Render_TextureCreateDesc const raw2DImageData{
			TinyImageFormat_R8G8B8A8_UNORM,
			Render_TUF_SHADER_READ,
			4, 4, 1, 1, 1, 0, 0,
			DummyData
	};
	Render_TextureCreateDesc const raw2DArrayImageData{
			TinyImageFormat_R8G8B8A8_UNORM,
			Render_TUF_SHADER_READ,
			4, 4, 1, 3, 1, 0, 0,
			DummyData
	};
	Render_TextureCreateDesc const raw3DImageData{
			TinyImageFormat_R8G8B8A8_UNORM,
			Render_TUF_SHADER_READ,
			4, 4, 3, 1, 1, 0, 0,
			DummyData
	};

	ctx->dummy2DTexture = Render_TextureSyncCreate(ctx->renderer, &raw2DImageData);
	ctx->dummy2DArrayTexture = Render_TextureSyncCreate(ctx->renderer, &raw2DArrayImageData);
	ctx->dummy3DTexture = Render_TextureSyncCreate(ctx->renderer, &raw3DImageData);
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
	Render_ShaderObjectDesc vsod = {
			Render_ST_VERTEXSHADER,
			vfile,
			vertEntryPoint
	};
	Render_ShaderObjectDesc fsod = {
			Render_ST_FRAGMENTSHADER,
			ffile,
			fragEntryPoint
	};

	Render_ShaderObjectHandle shaderObjects[2]{};
	shaderObjects[0] = Render_ShaderObjectCreate(ctx->renderer, &vsod);
	shaderObjects[1] = Render_ShaderObjectCreate(ctx->renderer, &fsod);

	VFile_Close(vfile);
	VFile_Close(ffile);

	if (!shaderObjects[0] || !shaderObjects[1]) {
		Render_ShaderObjectDestroy(ctx->renderer, shaderObjects[0]);
		Render_ShaderObjectDestroy(ctx->renderer, shaderObjects[1]);
		return false;
	}

	ctx->shader = Render_ShaderCreate(ctx->renderer, 2, shaderObjects);

	Render_ShaderObjectDestroy(ctx->renderer, shaderObjects[0]);
	Render_ShaderObjectDestroy(ctx->renderer, shaderObjects[1]);

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

	Render_ShaderHandle shaders[]{ctx->shader};
	Render_SamplerHandle samplers[]{
			Render_GetStockSampler(renderer, Render_SST_POINT),
			Render_GetStockSampler(renderer, Render_SST_LINEAR),
	};

	char const *staticSamplerNames[]{"pointSampler", "bilinearSampler"};
	Render_RootSignatureDesc rootSignatureDesc{};
	rootSignatureDesc.shaderCount = 1;
	rootSignatureDesc.shaders = shaders;
	rootSignatureDesc.staticSamplerCount = 2;
	rootSignatureDesc.staticSamplerNames = staticSamplerNames;
	rootSignatureDesc.staticSamplers = samplers;
	ctx->rootSignature = Render_RootSignatureCreate(ctx->renderer, &rootSignatureDesc);
	if (!ctx->rootSignature) {
		return nullptr;
	}

	Render_GraphicsPipelineDesc gfxPipeDesc{};

	TinyImageFormat colourFormats[] = {Render_FrameBufferColourFormat(frameBuffer)};
	gfxPipeDesc.shader = ctx->shader;
	gfxPipeDesc.rootSignature = ctx->rootSignature;
	gfxPipeDesc.vertexLayout = Render_GetStockVertexLayout(renderer, Render_SVL_2D_COLOUR_UV);
	gfxPipeDesc.blendState = Render_GetStockBlendState(renderer, Render_SBS_OPAQUE);
	gfxPipeDesc.depthState = Render_GetStockDepthState(renderer, Render_SDS_IGNORE);
	gfxPipeDesc.rasteriserState = Render_GetStockRasterisationState(renderer, Render_SRS_NOCULL);
	gfxPipeDesc.colourRenderTargetCount = 1;
	gfxPipeDesc.colourFormats = colourFormats;
	gfxPipeDesc.depthStencilFormat = TinyImageFormat_UNDEFINED;
	gfxPipeDesc.sampleCount = 1;
	gfxPipeDesc.sampleQuality = 0;
	gfxPipeDesc.primitiveTopo = Render_PT_TRI_LIST;
	ctx->pipeline = Render_GraphicsPipelineCreate(ctx->renderer, &gfxPipeDesc);
	if (!ctx->pipeline) {
		return nullptr;
	}

	Render_DescriptorSetDesc const setDesc = {
			ctx->rootSignature,
			Render_DUF_PER_FRAME,
			1
	};

	ctx->descriptorSet = Render_DescriptorSetCreate(ctx->renderer, &setDesc);
	if (!ctx->descriptorSet) {
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
		Render_TextureDestroy(ctx->renderer, ctx->dummy3DTexture);
	}
	if (ctx->dummy2DArrayTexture) {
		Render_TextureDestroy(ctx->renderer, ctx->dummy2DArrayTexture);
	}
	if (ctx->dummy2DTexture) {
		Render_TextureDestroy(ctx->renderer, ctx->dummy2DTexture);
	}

	if (ctx->uniformBuffer) {
		Render_BufferDestroy(ctx->renderer, ctx->uniformBuffer);
	}
	if (ctx->descriptorSet) {
		Render_DescriptorSetDestroy(ctx->renderer, ctx->descriptorSet);
	}
	if (ctx->pipeline) {
		Render_GraphicsPipelineDestroy(ctx->renderer, ctx->pipeline);
	}
	if (ctx->rootSignature) {
		Render_RootSignatureDestroy(ctx->renderer, ctx->rootSignature);
	}
	if (ctx->shader) {
		Render_ShaderDestroy(ctx->renderer, ctx->shader);
	}

	MEMORY_FREE(ctx);
}
static void ImCallback(ImDrawList const *list, ImDrawCmd const *imcmd) {

	if (imcmd->TextureId == nullptr) {
		return;
	}

	auto ctx = (TextureViewer *) imcmd->UserCallbackData;

	TextureViewer_Texture const *texture = (TextureViewer_Texture *) imcmd->TextureId;

	ImDrawData *drawData = ImGui::GetDrawData();
	ImVec2 displayPos = drawData->DisplayPos;
	displayPos.x *= drawData->FramebufferScale.x;
	displayPos.y *= drawData->FramebufferScale.y;

	Render_GraphicsEncoderBindPipeline(ctx->currentEncoder, ctx->pipeline);

	Render_DescriptorDesc params[3];
	params[0].name = "colourTexture";
	params[0].type = Render_DT_TEXTURE;
	params[1].name = "colourTextureArray";
	params[1].type = Render_DT_TEXTURE;
	if (Image_IsArray(texture->cpu)) {
		params[0].texture = ctx->dummy2DTexture;
		params[1].texture = texture->gpu;
	} else {
		params[0].texture = texture->gpu;
		params[1].texture = ctx->dummy2DArrayTexture;
	}
	params[2].name = "uniformBlock";
	params[2].type = Render_DT_BUFFER;
	params[2].buffer = ctx->uniformBuffer;
	params[2].offset = 0;
	params[2].size = UNIFORM_BUFFER_SIZE_PER_FRAME;

	Render_DescriptorUpdate(ctx->descriptorSet, 0, 3, params);
	Render_GraphicsEncoderBindDescriptorSet(ctx->currentEncoder, ctx->descriptorSet, 0);

	float const clipX = imcmd->ClipRect.x * drawData->FramebufferScale.x;
	float const clipY = imcmd->ClipRect.y * drawData->FramebufferScale.y;
	float const clipZ = imcmd->ClipRect.z * drawData->FramebufferScale.x;
	float const clipW = imcmd->ClipRect.w * drawData->FramebufferScale.y;

	Render_GraphicsEncoderSetScissor(ctx->currentEncoder,
																	 {
																			 (uint32_t) (clipX - displayPos.x),
																			 (uint32_t) (clipY - displayPos.y),
																			 (uint32_t) (clipZ - clipX),
																			 (uint32_t) (clipW - clipY)
																	 });

	Render_GraphicsEncoderDrawIndexed(ctx->currentEncoder, 6, imcmd->IdxOffset, imcmd->VtxOffset);
}

bool TextureViewer_DrawUI(TextureViewerHandle handle, TextureViewer_Texture *texture) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx) {
		return false;
	}

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize;
	bool open = true;
	ImGui::Begin(ctx->windowName, &open, window_flags);
	if(open == false) {
		ImGui::End();
		return false;
	}

	ImGuiWindow *window = ImGui::GetCurrentWindow();
	ImDrawList *drawList = ImGui::GetWindowDrawList();
	if (window->SkipItems || !texture) {
		ImGui::End();
		return false;
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
											&forceMipLevel, 0, (int) Image_MipMapCountOf(texture->cpu) - 1);
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
	return true;
}

void TextureViewer_RenderSetup(TextureViewerHandle handle, Render_GraphicsEncoderHandle encoder) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx) {
		return;
	}

	static_assert(sizeof(UniformBuffer) < UNIFORM_BUFFER_SIZE_PER_FRAME);

	memcpy(ctx->uniforms.scaleOffsetMatrix,
				 Render_FrameBufferImguiScaleOffsetMatrix(ctx->frameBuffer),
				 sizeof(float) * 16);

	ctx->uniforms.colourMask[0] = ctx->colourChannelEnable[0] ? 1.0f : 0.0f;
	ctx->uniforms.colourMask[1] = ctx->colourChannelEnable[1] ? 1.0f : 0.0f;
	ctx->uniforms.colourMask[2] = ctx->colourChannelEnable[2] ? 1.0f : 0.0f;
	ctx->uniforms.colourMask[3] = ctx->colourChannelEnable[3] ? 1.0f : 0.0f;

	if ((!ctx->colourChannelEnable[0]) &&
			(!ctx->colourChannelEnable[1]) &&
			(!ctx->colourChannelEnable[2]) &&
			ctx->colourChannelEnable[3]) {
		ctx->uniforms.alphaReplicate = 1.0f;
	} else {
		ctx->uniforms.alphaReplicate = 0.0f;
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
