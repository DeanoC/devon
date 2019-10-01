#include "al2o3_platform/platform.h"
#include "al2o3_memory/memory.h"
#include "al2o3_vfile/vfile.h"
#include "gfx_imgui/imgui.h"

namespace {
bool aboutOpen = false;

char * licenseText;
char * licenseTextEnd;
char * liblicensesText;
char * liblicensesTextEnd;

}

// Demonstrate creating a simple log window with basic filtering.
static void TextWindow(char const* name, char const * begin, char const * end, size_t vsize)
{

	ImGui::BeginChild(name, ImVec2(0,vsize), false, ImGuiWindowFlags_HorizontalScrollbar);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::TextUnformatted(begin, end);
	ImGui::PopStyleVar();

	ImGui::EndChild();
}

void About_Open() {
	aboutOpen = true;
}

void About_Close() {
	MEMORY_FREE(liblicensesText); liblicensesText = nullptr;
	MEMORY_FREE(licenseText); licenseText = nullptr;
	aboutOpen = false;
}

void About_Display() {

	if(aboutOpen == true)
	{
		ImGui::SetNextWindowSize(ImVec2(600, 950), 0);
		if (!ImGui::Begin("About", &aboutOpen, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("Devon - a texture viewer");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::Text("By Deano Calver using lots of open source software!");

		if(licenseText == nullptr) {
			VFile_Handle fh = VFile_FromFile("LICENSE", Os_FM_Read);
			uint64_t const size = VFile_Size(fh);
			licenseText = (char*) MEMORY_MALLOC(size);
			VFile_Read(fh, licenseText, size);
			VFile_Close(fh);
			licenseTextEnd = licenseText + size;
		}

		if(liblicensesText == nullptr) {
			VFile_Handle fh = VFile_FromFile("LIBRARY_LICENSES", Os_FM_Read);
			uint64_t const size = VFile_Size(fh);
			liblicensesText = (char*) MEMORY_MALLOC(size);
			VFile_Read(fh, liblicensesText, size);
			VFile_Close(fh);
			liblicensesTextEnd = liblicensesText + size;
		}

		if(licenseText != nullptr) {
			ImGui::Spacing();
			ImGui::Separator();
			TextWindow("license", licenseText, licenseTextEnd, 300);
		}

		if(liblicensesText != nullptr) {
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Devon uses libraries with the following licenses");
			TextWindow("liblicenses", liblicensesText, liblicensesTextEnd, 500);
		}

		if(ImGui::Button("Okay")) {
			About_Close();
		}

		ImGui::End();
	}
}