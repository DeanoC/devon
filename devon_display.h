#pragma once
#ifndef DEVON_DEVON_DISPLAY_H
#define DEVON_DEVON_DISPLAY_H

#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "gfx_theforge/theforge.h"

typedef struct Display_Context *Display_ContextHandle;
extern Display_ContextHandle Display_Create(TheForge_RendererHandle renderer,
																						TheForge_QueueHandle presentQueue,
																						TheForge_CmdPoolHandle cmdPool,
																						uint32_t swapImageCount);
extern void Display_Destroy(Display_ContextHandle handle);

extern TheForge_CmdHandle Display_NewFrame(Display_ContextHandle handle,
																					 TheForge_RenderTargetHandle *renderTarget,
																					 TheForge_RenderTargetHandle *depthTarget);
extern void Display_Present(Display_ContextHandle handle);

extern TinyImageFormat Display_GetBackBufferFormat(Display_ContextHandle handle);
extern TinyImageFormat Display_GetDepthBufferFormat(Display_ContextHandle handle);
extern bool Display_IsBackBufferSrgb(Display_ContextHandle handle);

#endif //DEVON_DEVON_DISPLAY_H
