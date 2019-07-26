#pragma once
#ifndef DEVON__TEXTURE_VIEWER_HPP
#define DEVON__TEXTURE_VIEWER_HPP

#include "gfx_theforge/theforge.h"
#include "gfx_imgui_al2o3_theforge_bindings/bindings.h"

typedef struct TextureViewer *TextureViewerHandle;

TextureViewerHandle TextureViewer_Create(TheForge_RendererHandle renderer,
																				 ShaderCompiler_ContextHandle shaderCompiler,
																				 ImguiBindings_ContextHandle imgui,
																				 uint32_t maxFrames,
																				 TheForge_ImageFormat renderTargetFormat,
																				 TheForge_ImageFormat depthStencilFormat,
																				 bool sRGB,
																				 TheForge_SampleCount sampleCount,
																				 uint32_t sampleQuality);
void TextureViewer_Destroy(TextureViewerHandle handle);

void TextureViewer_DrawUI(TextureViewerHandle handle, ImguiBindings_Texture* texture);
// must be called before Imguibinding render. Sets up things for the callbacks from imgui
void TextureViewer_RenderSetup(TextureViewerHandle handle, TheForge_CmdHandle cmd);

#endif //DEVON__TEXTURE_VIEWER_HPP
