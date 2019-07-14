#include "al2o3_platform/platform.h"
#include "gfx_theforge/theforge.h"


int main(int argc, char const* args[]) {
	TheForge_RendererDesc desc {
			TheForge_ST_5_1,
			TheForge_GM_SINGLE
	};
#if AL2O3_PLATFORM == AL2O3_PLATFORM_WINDOWS
	desc.d3dFeatureLevel = TheForge_D3D_FL_12_0;
#endif

	auto renderer = TheForge_RendererCreate("Devon", &desc);

	TheForge_RendererDestroy(renderer);
}