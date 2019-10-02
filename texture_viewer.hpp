#pragma once
#ifndef DEVON_TEXTURE_VIEWER_HPP
#define DEVON_TEXTURE_VIEWER_HPP

#include "render_basics/api.h"
typedef struct TextureViewer *TextureViewerHandle;
struct Image_ImageHeader;

typedef struct TextureViewer_Texture {
	Image_ImageHeader const *cpu;
	Render_TextureHandle gpu;
} TextureViewer_Texture;

TextureViewerHandle TextureViewer_Create(Render_RendererHandle renderer,
																				 Render_FrameBufferHandle frameBuffer);
void TextureViewer_Destroy(TextureViewerHandle handle);

bool TextureViewer_DrawUI(TextureViewerHandle handle, TextureViewer_Texture *texture);
// must be called before Imguibinding render. Sets up things for the callbacks from imgui
void TextureViewer_RenderSetup(TextureViewerHandle handle, Render_GraphicsEncoderHandle encoder);

void TextureViewer_SetWindowName(TextureViewerHandle handle, char const *windowName);
void TextureViewer_SetZoom(TextureViewerHandle handle, float zoom);

#endif //DEVON__TEXTURE_VIEWER_HPP
