#pragma once
#ifndef DEVON_TEXTURE_VIEWER_HPP
#define DEVON_TEXTURE_VIEWER_HPP

#include "render_basics/api.h"
#include "gfx_theforge/theforge.h"
#include "gfx_imgui_al2o3_theforge_bindings/bindings.h"

typedef struct TextureViewer *TextureViewerHandle;

TextureViewerHandle TextureViewer_Create(Render_RendererHandle renderer,
																				 Render_FrameBufferHandle frameBuffer);
void TextureViewer_Destroy(TextureViewerHandle handle);

void TextureViewer_DrawUI(TextureViewerHandle handle, ImguiBindings_Texture *texture);
// must be called before Imguibinding render. Sets up things for the callbacks from imgui
void TextureViewer_RenderSetup(TextureViewerHandle handle, Render_CmdHandle cmd);

void TextureViewer_SetWindowName(TextureViewerHandle handle, char const *windowName);
void TextureViewer_SetZoom(TextureViewerHandle handle, float zoom);

#endif //DEVON__TEXTURE_VIEWER_HPP
