cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

include(FetchContent)
FetchContent_Declare( al2o3 GIT_REPOSITORY https://github.com/DeanoC/al2o3 GIT_TAG master )
FetchContent_GetProperties(al2o3)
if(NOT al2o3_POPULATED)
	FetchContent_Populate(al2o3)
	add_subdirectory(${al2o3_SOURCE_DIR} ${al2o3_BINARY_DIR})
endif()

SET_MIN_VERSIONS()
set(ProjectName devon)
project(${ProjectName})

set(Src
		main.cpp
		)
set(Deps
		al2o3_platform
		al2o3_memory
		al2o3_os
		al2o3_cmath
		al2o3_tinystl
		al2o3_vfile
		gfx_image_interface
		gfx_image_impl_basic
		gfx_imageio
		gfx_theforge
		gfx_shadercompiler
		gfx_imgui_al2o3_theforge_bindings
		lua_base5.3
		lua_al2o3_os
		lua_image
		input_basic_interface
		input_basic_impl_gainput
		utils_simple_logmanager
		utils_misccpp
		utils_gameappshell_interface
		utils_gameappshell_impl_basic
		)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out_libs)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out_bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out_bin)

set(LIB_BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/)

ADD_GUI_APP(${ProjectName} "${Src}" "${Deps}" utils_gameappshell_interface)