#pragma once
#ifndef DEVON_TEXTURE_VIEWER_HPP
#define DEVON_TEXTURE_VIEWER_HPP

#include "gfx_theforge/theforge.h"
#include "gfx_imgui_al2o3_theforge_bindings/bindings.h"

typedef struct TextureViewer *TextureViewerHandle;

TextureViewerHandle TextureViewer_Create(TheForge_RendererHandle renderer,
																				 ShaderCompiler_ContextHandle shaderCompiler,
																				 ImguiBindings_ContextHandle imgui,
																				 uint32_t maxFrames,
																				 TinyImageFormat renderTargetFormat,
																				 TinyImageFormat depthStencilFormat,
																				 TheForge_SampleCount sampleCount,
																				 uint32_t sampleQuality);
void TextureViewer_Destroy(TextureViewerHandle handle);

void TextureViewer_DrawUI(TextureViewerHandle handle, ImguiBindings_Texture* texture);
// must be called before Imguibinding render. Sets up things for the callbacks from imgui
void TextureViewer_RenderSetup(TextureViewerHandle handle, TheForge_CmdHandle cmd);
void TextureViewer_SetWindowName(TextureViewerHandle handle, char const* windowName );

#endif //DEVON__TEXTURE_VIEWER_HPP
