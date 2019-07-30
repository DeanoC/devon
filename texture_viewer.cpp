#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "gfx_imgui/imgui.h"
#include "gfx_imgui/imgui_internal.h"
#include "al2o3_cadt/vector.h"
#include "texture_viewer.hpp"

struct UniformBuffer {
	float scaleOffsetMatrix[16];
	float colourMask[4];
	float alphaReplicate;
	int32_t forceMipLevel;
	uint32_t sliceToView;
	uint32_t numSlices;
};

static const uint64_t UNIFORM_BUFFER_SIZE_PER_FRAME = 256;

struct TextureViewer {
	TheForge_RendererHandle renderer;
	ShaderCompiler_ContextHandle shaderCompiler;
	ImguiBindings_ContextHandle imgui;
	uint32_t maxFrames;

	TheForge_ShaderHandle shader;
	TheForge_SamplerHandle pointSampler;
	TheForge_SamplerHandle bilinearSampler;
	TheForge_BlendStateHandle blendState;
	TheForge_RootSignatureHandle rootSignature;
	TheForge_PipelineHandle pipeline;
	TheForge_DepthStateHandle depthState;
	TheForge_RasterizerStateHandle rasterizationState;
	TheForge_DescriptorBinderHandle descriptorBinder;
	TheForge_BufferHandle uniformBuffer;

	TheForge_TextureHandle dummy2DTexture;
	TheForge_TextureHandle dummy2DArrayTexture;
	TheForge_TextureHandle dummy3DTexture;

	UniformBuffer uniforms;
	bool colourChannelEnable[4];
	float zoom;

	TheForge_CmdHandle cmd; // only valid during a render
	uint32_t currentFrame;

	char *windowName;
};

namespace {

static uint32_t DummyData[] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
																0xFF0000FF, 0xFF0000FF, 0xFF0000FF, 0xFF0000FF,
																0xFF8000FF, 0xFF8000FF, 0xFF8000FF, 0xFF8000FF,
																0xFF0080FF, 0xFF0080FF, 0xFF0080FF, 0xFF0080FF };

static void CreateDummyTextures(TextureViewer *ctx)
{
	TheForge_RawImageData const raw2DImageData{
			(unsigned char *) DummyData,
			TheForge_IF_NONE,4,4,1,1,1,
			TinyImageFormat_R8G8B8A8_UNORM
	};
	TheForge_RawImageData const raw2DArrayImageData{
			(unsigned char *) DummyData,
			TheForge_IF_NONE,4,4,1,4,1,
			TinyImageFormat_R8G8B8A8_UNORM
	};
	TheForge_RawImageData const raw3DImageData{
			(unsigned char *) DummyData,
			TheForge_IF_NONE,4,4,4,1,1,
			TinyImageFormat_R8G8B8A8_UNORM
	};

	TheForge_TextureLoadDesc loadDesc{};
	loadDesc.pRawImageData = &raw2DImageData;
	loadDesc.pTexture = &ctx->dummy2DTexture;
	loadDesc.mCreationFlag = TheForge_TCF_OWN_MEMORY_BIT;
	TheForge_LoadTexture(&loadDesc, false);
	loadDesc.pRawImageData = &raw2DArrayImageData;
	loadDesc.pTexture = &ctx->dummy2DArrayTexture;
	loadDesc.mCreationFlag = TheForge_TCF_OWN_MEMORY_BIT;
	TheForge_LoadTexture(&loadDesc, false);
	loadDesc.pRawImageData = &raw3DImageData;
	loadDesc.pTexture = &ctx->dummy3DTexture;
	loadDesc.mCreationFlag = TheForge_TCF_OWN_MEMORY_BIT;
	TheForge_LoadTexture(&loadDesc, false);

}

static bool CreateShaders(TextureViewer *ctx) {

	static char const *const vertEntryPoint = "VS_main";
	static char const *const fragEntryPoint = "FS_main";

	VFile_Handle vfile = VFile_FromFile("textureviewer_vertex.hlsl", Os_FM_Read);
	if (!vfile)
		return false;
	VFile_Handle ffile = VFile_FromFile("textureviewer_fragment.hlsl", Os_FM_Read);
	if (!ffile) {
		VFile_Close(vfile);
		return false;
	}
	ShaderCompiler_Output vout;
	bool vokay = ShaderCompiler_Compile(
			ctx->shaderCompiler, ShaderCompiler_ST_VertexShader,
			VFile_GetName(vfile), vertEntryPoint, vfile,
			&vout);
	if (vout.log != nullptr) {
		LOGWARNINGF("Shader compiler : %s %s", vokay ? "warnings" : "ERROR", vout.log);
	}
	ShaderCompiler_Output fout;
	bool fokay = ShaderCompiler_Compile(
			ctx->shaderCompiler, ShaderCompiler_ST_FragmentShader,
			VFile_GetName(ffile), fragEntryPoint, ffile,
			&fout);
	if (fout.log != nullptr) {
		LOGWARNINGF("Shader compiler : %s %s", fokay ? "warnings" : "ERROR", fout.log);
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
	TheForge_AddShader(ctx->renderer, &sdesc, &ctx->shader);
#else
	TheForge_BinaryShaderDesc bdesc;
	bdesc.stages = (TheForge_ShaderStage) (TheForge_SS_FRAG | TheForge_SS_VERT);
	bdesc.vert.byteCode = (char *) vout.shader;
	bdesc.vert.byteCodeSize = (uint32_t) vout.shaderSize;
	bdesc.vert.entryPoint = vertEntryPoint;
	bdesc.frag.byteCode = (char *) fout.shader;
	bdesc.frag.byteCodeSize = (uint32_t) fout.shaderSize;
	bdesc.frag.entryPoint = fragEntryPoint;
	TheForge_AddShaderBinary(ctx->renderer, &bdesc, &ctx->shader);
#endif
	MEMORY_FREE((void *) vout.log);
	MEMORY_FREE((void *) vout.shader);
	MEMORY_FREE((void *) fout.log);
	MEMORY_FREE((void *) fout.shader);
	return true;
}

}

TextureViewerHandle TextureViewer_Create(TheForge_RendererHandle renderer,
																				 ShaderCompiler_ContextHandle shaderCompiler,
																				 ImguiBindings_ContextHandle imgui,
																				 uint32_t maxFrames,
																				 TheForge_ImageFormat renderTargetFormat,
																				 TheForge_ImageFormat depthStencilFormat,
																				 bool sRGB,
																				 TheForge_SampleCount sampleCount,
																				 uint32_t sampleQuality) {
	auto ctx = (TextureViewer *) MEMORY_CALLOC(1, sizeof(TextureViewer));
	if (!ctx)
		return nullptr;

	ctx->renderer = renderer;
	ctx->shaderCompiler = shaderCompiler;
	ctx->imgui = imgui;
	ctx->maxFrames = maxFrames;

	if (!CreateShaders(ctx)) {
		MEMORY_FREE(ctx);
		return nullptr;
	}

	static TheForge_SamplerDesc const pointSamplerDesc{
			TheForge_FT_NEAREST,
			TheForge_FT_NEAREST,
			TheForge_MM_NEAREST,
			TheForge_AM_CLAMP_TO_EDGE,
			TheForge_AM_CLAMP_TO_EDGE,
			TheForge_AM_CLAMP_TO_EDGE,
			0.0f,
			1.0f,
			TheForge_CMP_ALWAYS
	};

	static TheForge_SamplerDesc const bilinearSamplerDesc{
			TheForge_FT_LINEAR,
			TheForge_FT_LINEAR,
			TheForge_MM_NEAREST,
			TheForge_AM_CLAMP_TO_EDGE,
			TheForge_AM_CLAMP_TO_EDGE,
			TheForge_AM_CLAMP_TO_EDGE,
			0.0f,
			1.0f,
			TheForge_CMP_ALWAYS
	};

	static TheForge_VertexLayout const vertexLayout{
			3,
			{
					{TheForge_SS_POSITION, 8, "POSITION", TheForge_IF_RG32F, 0, 0, 0},
					{TheForge_SS_TEXCOORD0, 9, "TEXCOORD", TheForge_IF_RG32F, 0, 1, sizeof(float) * 2},
					{TheForge_SS_COLOR, 5, "COLOR", TheForge_IF_RGBA8, 0, 2, sizeof(float) * 4}
			}
	};
	static TheForge_BlendStateDesc const blendDesc{
			{TheForge_BC_ONE},
			{TheForge_BC_ZERO},
			{TheForge_BC_ONE},
			{TheForge_BC_ZERO},
			{TheForge_BM_ADD},
			{TheForge_BM_ADD},
			{0xF},
			TheForge_BST_0,
			false, false
	};
	static TheForge_DepthStateDesc const depthStateDesc{
			false, false,
			TheForge_CMP_ALWAYS
	};
	static TheForge_RasterizerStateDesc const rasterizerStateDesc{
			TheForge_CM_NONE,
			0,
			0.0,
			TheForge_FM_SOLID,
			false,
			true,
	};

	static TheForge_BufferDesc const ubDesc{
			UNIFORM_BUFFER_SIZE_PER_FRAME * ctx->maxFrames,
			TheForge_RMU_CPU_TO_GPU,
			(TheForge_BufferCreationFlags) (TheForge_BCF_PERSISTENT_MAP_BIT | TheForge_BCF_OWN_MEMORY_BIT),
			TheForge_RS_UNDEFINED,
			TheForge_IT_UINT16,
			0,
			0,
			0,
			0,
			nullptr,
			TheForge_IF_NONE,
			TheForge_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	};

	TheForge_AddSampler(ctx->renderer, &pointSamplerDesc, &ctx->pointSampler);
	if (!ctx->pointSampler)
		return nullptr;
	TheForge_AddSampler(ctx->renderer, &bilinearSamplerDesc, &ctx->bilinearSampler);
	if (!ctx->bilinearSampler)
		return nullptr;

	TheForge_AddBlendState(ctx->renderer, &blendDesc, &ctx->blendState);
	if (!ctx->blendState)
		return nullptr;
	TheForge_AddDepthState(ctx->renderer, &depthStateDesc, &ctx->depthState);
	if (!ctx->depthState)
		return nullptr;
	TheForge_AddRasterizerState(ctx->renderer, &rasterizerStateDesc, &ctx->rasterizationState);
	if (!ctx->rasterizationState)
		return nullptr;

	TheForge_ShaderHandle shaders[]{ctx->shader};
	TheForge_SamplerHandle samplers[]{ctx->pointSampler, ctx->bilinearSampler};
	char const *staticSamplerNames[]{"pointSampler", "bilinearSampler"};

	TheForge_RootSignatureDesc rootSignatureDesc{};
	rootSignatureDesc.shaderCount = 1;
	rootSignatureDesc.pShaders = shaders;
	rootSignatureDesc.staticSamplerCount = 2;
	rootSignatureDesc.pStaticSamplerNames = staticSamplerNames;
	rootSignatureDesc.pStaticSamplers = samplers;
	TheForge_AddRootSignature(ctx->renderer, &rootSignatureDesc, &ctx->rootSignature);
	if (!ctx->rootSignature)
		return nullptr;

	TheForge_PipelineDesc pipelineDesc{};
	pipelineDesc.type = TheForge_PT_GRAPHICS;
	TheForge_GraphicsPipelineDesc &gfxPipeDesc = pipelineDesc.graphicsDesc;
	gfxPipeDesc.shaderProgram = ctx->shader;
	gfxPipeDesc.rootSignature = ctx->rootSignature;
	gfxPipeDesc.pVertexLayout = &vertexLayout;
	gfxPipeDesc.blendState = ctx->blendState;
	gfxPipeDesc.depthState = ctx->depthState;
	gfxPipeDesc.rasterizerState = ctx->rasterizationState;
	gfxPipeDesc.renderTargetCount = 1;
	gfxPipeDesc.pColorFormats = &renderTargetFormat;
	gfxPipeDesc.depthStencilFormat = depthStencilFormat;
	gfxPipeDesc.pSrgbValues = &sRGB;
	gfxPipeDesc.sampleCount = sampleCount;
	gfxPipeDesc.sampleQuality = sampleQuality;
	gfxPipeDesc.primitiveTopo = TheForge_PT_TRI_LIST;
	TheForge_AddPipeline(ctx->renderer, &pipelineDesc, &ctx->pipeline);
	if (!ctx->pipeline)
		return nullptr;

	TheForge_DescriptorBinderDesc descriptorBinderDesc[] = {
			{ctx->rootSignature, 20},
			{ctx->rootSignature, 20},
	};
	TheForge_AddDescriptorBinder(ctx->renderer, 0, 2, descriptorBinderDesc, &ctx->descriptorBinder);
	if (!ctx->descriptorBinder)
		return nullptr;

	TheForge_AddBuffer(ctx->renderer, &ubDesc, &ctx->uniformBuffer);
	if (!ctx->uniformBuffer)
		return nullptr;

	CreateDummyTextures(ctx);

	// defaults
	ctx->colourChannelEnable[0] = true;
	ctx->colourChannelEnable[1] = true;
	ctx->colourChannelEnable[2] = true;
	ctx->colourChannelEnable[3] = false;
	ctx->zoom = 1.0f;

	static char const DefaultName[] = "Texture Viewer";
	ctx->windowName = (char*) MEMORY_CALLOC(strlen(DefaultName)+1,1);
	memcpy(ctx->windowName, DefaultName, strlen(DefaultName));

	return ctx;
}

void TextureViewer_Destroy(TextureViewerHandle handle) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx)
		return;

	MEMORY_FREE(ctx->windowName);

	if (ctx->dummy3DTexture)
		TheForge_RemoveTexture(ctx->renderer, ctx->dummy3DTexture);
	if (ctx->dummy2DArrayTexture)
		TheForge_RemoveTexture(ctx->renderer, ctx->dummy2DArrayTexture);
	if (ctx->dummy2DTexture)
		TheForge_RemoveTexture(ctx->renderer, ctx->dummy2DTexture);

	if (ctx->uniformBuffer)
		TheForge_RemoveBuffer(ctx->renderer, ctx->uniformBuffer);
	if (ctx->descriptorBinder)
		TheForge_RemoveDescriptorBinder(ctx->renderer, ctx->descriptorBinder);
	if (ctx->pipeline)
		TheForge_RemovePipeline(ctx->renderer, ctx->pipeline);
	if (ctx->rootSignature)
		TheForge_RemoveRootSignature(ctx->renderer, ctx->rootSignature);
	if (ctx->rasterizationState)
		TheForge_RemoveRasterizerState(ctx->renderer, ctx->rasterizationState);
	if (ctx->depthState)
		TheForge_RemoveDepthState(ctx->renderer, ctx->depthState);
	if (ctx->blendState)
		TheForge_RemoveBlendState(ctx->renderer, ctx->blendState);
	if (ctx->bilinearSampler)
		TheForge_RemoveSampler(ctx->renderer, ctx->bilinearSampler);
	if (ctx->pointSampler)
		TheForge_RemoveSampler(ctx->renderer, ctx->pointSampler);
	if (ctx->shader)
		TheForge_RemoveShader(ctx->renderer, ctx->shader);

	MEMORY_FREE(ctx);
}
static void ImCallback(ImDrawList const *list, ImDrawCmd const *imcmd) {

	if (imcmd->TextureId == nullptr)
		return;

	auto ctx = (TextureViewer *) imcmd->UserCallbackData;

	ImguiBindings_Texture const *texture = (ImguiBindings_Texture *) imcmd->TextureId;

	ImDrawData *drawData = ImGui::GetDrawData();
	ImVec2 displayPos = drawData->DisplayPos;
	displayPos.x *= drawData->FramebufferScale.x;
	displayPos.y *= drawData->FramebufferScale.y;

	TheForge_CmdBindPipeline(ctx->cmd, ctx->pipeline);

	uint64_t const baseUniformOffset = ctx->currentFrame * UNIFORM_BUFFER_SIZE_PER_FRAME;

	TheForge_DescriptorData params[3]{};
	params[0].pName = "uniformBlock";
	params[0].pBuffers = &ctx->uniformBuffer;
	params[0].pOffsets = &baseUniformOffset;
	params[0].count = 1;
	if(Image_IsArray(texture->cpu)) {
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
	TheForge_CmdBindDescriptors(ctx->cmd, ctx->descriptorBinder, ctx->rootSignature, 3, params);

	float const clipX = imcmd->ClipRect.x * drawData->FramebufferScale.x;
	float const clipY = imcmd->ClipRect.y * drawData->FramebufferScale.y;
	float const clipZ = imcmd->ClipRect.z * drawData->FramebufferScale.x;
	float const clipW = imcmd->ClipRect.w * drawData->FramebufferScale.y;

	TheForge_CmdSetScissor(ctx->cmd,
												 (uint32_t) (clipX - displayPos.x),
												 (uint32_t) (clipY - displayPos.y),
												 (uint32_t) (clipZ - clipX),
												 (uint32_t) (clipW - clipY));

	TheForge_CmdDrawIndexed(ctx->cmd, 6, imcmd->IdxOffset, imcmd->VtxOffset);
}

void TextureViewer_DrawUI(TextureViewerHandle handle, ImguiBindings_Texture *texture) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx)
		return;

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
	if (rb.y < window->DC.CursorPos.y + 32.0f)
		rb.y = window->DC.CursorPos.y + 32.0f;
	ImRect const bb2(window->DC.CursorPos, rb);
	ImGui::ItemSize(bb2);

	int forceMipLevel = 0;
	int sliceToView = 0;

	if(Image_LinkedImageCountOf(texture->cpu) > 1) {
		forceMipLevel = (int) ctx->uniforms.forceMipLevel;
		ImGui::SameLine();
		ImGui::VSliderInt("Mipmap Level", ImVec2(20.0f, 100.0f),
											&forceMipLevel, 0, (int) Image_LinkedImageCountOf(texture->cpu)-1);
	}
	ctx->uniforms.forceMipLevel = (int32_t)forceMipLevel;
	if(texture->cpu->slices > 1) {
		sliceToView = (int) ctx->uniforms.sliceToView;
		ImGui::SameLine();
		ImGui::VSliderInt("Slice", ImVec2(20.0f, 100.0f),
											&sliceToView, 0, (int) texture->cpu->slices - 1);
	}
	ctx->uniforms.numSlices = texture->cpu->slices;
	ctx->uniforms.sliceToView = (uint32_t)sliceToView;

	ImGui::End();
}

void TextureViewer_RenderSetup(TextureViewerHandle handle, TheForge_CmdHandle cmd) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx)
		return;

	static_assert(sizeof(UniformBuffer) < UNIFORM_BUFFER_SIZE_PER_FRAME);

	ctx->currentFrame = (ctx->currentFrame + 1) % ctx->maxFrames;

	uint64_t const baseUniformOffset = ctx->currentFrame * UNIFORM_BUFFER_SIZE_PER_FRAME;
	memcpy(ctx->uniforms.scaleOffsetMatrix, ImguiBindings_GetScaleOffsetMatrix(ctx->imgui), sizeof(float) * 16);

	if ((ctx->colourChannelEnable[0] == false) &&
			(ctx->colourChannelEnable[1] == false) &&
			(ctx->colourChannelEnable[2] == false) &&
			(ctx->colourChannelEnable[3] == true)) {
		ctx->uniforms.alphaReplicate = 1.0f;
	} else {
		ctx->uniforms.alphaReplicate = 0.0f;
		ctx->uniforms.colourMask[0] = ctx->colourChannelEnable[0] ? 1.0f : 0.0f;
		ctx->uniforms.colourMask[1] = ctx->colourChannelEnable[1] ? 1.0f : 0.0f;
		ctx->uniforms.colourMask[2] = ctx->colourChannelEnable[2] ? 1.0f : 0.0f;
		ctx->uniforms.colourMask[3] = ctx->colourChannelEnable[3] ? 1.0f : 0.0f;
	}

	TheForge_BufferUpdateDesc const uniformsUpdate{
			ctx->uniformBuffer,
			(void const *) &ctx->uniforms,
			0,
			baseUniformOffset,
			UNIFORM_BUFFER_SIZE_PER_FRAME
	};

	TheForge_UpdateBuffer(&uniformsUpdate, true);

	ctx->cmd = cmd;
}

void TextureViewer_SetWindowName(TextureViewerHandle handle, char const* windowName ) {
	auto ctx = (TextureViewer *) handle;
	if (!ctx)
		return;

	MEMORY_FREE(ctx->windowName);
	ctx->windowName = (char*) MEMORY_CALLOC(strlen(windowName)+1,1);
	memcpy(ctx->windowName, windowName, strlen(windowName));

}
