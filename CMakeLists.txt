cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

get_directory_property(hasParent PARENT_DIRECTORY)
if(NOT hasParent)
	get_filename_component(PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
	set_property(GLOBAL PROPERTY GLOBAL_FETCHDEPS_BASE ${PARENT_DIR}/al2o3 )
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out_libs)
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out_bin)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/out_bin)
	set(LIB_BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/)
	include(FetchContent)
	FetchContent_Declare( al2o3 GIT_REPOSITORY https://github.com/DeanoC/al2o3 GIT_TAG master )
	FetchContent_GetProperties(al2o3)
	if(NOT al2o3_POPULATED)
		FetchContent_Populate(al2o3)
		add_subdirectory(${al2o3_SOURCE_DIR} ${al2o3_BINARY_DIR})
	endif()
endif ()

SET_MIN_VERSIONS()
set(ProjectName devon)
project(${ProjectName})

set(Src
		main.cpp
		devon_display.cpp
		devon_display.h
		texture_viewer.cpp
		texture_viewer.hpp
		)
set(Deps
		al2o3_platform
		al2o3_memory
		al2o3_os
		al2o3_cmath
		al2o3_cadt
		al2o3_tinystl
		al2o3_vfile
		al2o3_enki
		gfx_image_interface
		gfx_image_impl_basic
		gfx_imageio
		gfx_imagecompress
		gfx_imagedecompress
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
		utils_nativefiledialogs
		)
		
set(LIB_BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/)

ADD_GUI_APP(${ProjectName} "${Src}" "${Deps}" utils_gameappshell_interface)
