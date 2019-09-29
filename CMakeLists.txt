cmake_minimum_required(VERSION 3.12.4)
get_directory_property(hasParent PARENT_DIRECTORY)
if(NOT hasParent)
	option(unittests "unittests" OFF)
	get_filename_component(_PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
	set_property(GLOBAL PROPERTY GLOBAL_FETCHDEPS_BASE ${_PARENT_DIR}/al2o3 )
	include(FetchContent)
	FetchContent_Declare( al2o3 GIT_REPOSITORY https://github.com/DeanoC/al2o3 GIT_TAG master )
	FetchContent_GetProperties(al2o3)
	if(NOT al2o3_POPULATED)
		FetchContent_Populate(al2o3)
		add_subdirectory(${al2o3_SOURCE_DIR} ${al2o3_BINARY_DIR})
	endif()
	INIT_AL2O3(${CMAKE_CURRENT_SOURCE_DIR})
endif ()
SET_MIN_VERSIONS()
set(ProjectName devon)
project(${ProjectName})

set(Src
		main.cpp
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
		input_basic_interface
		input_basic_impl_gainput
		render_basics_interface
		render_basics_impl_theforge
		utils_simple_logmanager
		utils_misccpp
		utils_gameappshell_interface
		utils_gameappshell_impl_basic
		utils_nativefiledialogs
		)
		
set(LIB_BASE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/libs/)

ADD_GUI_APP(${ProjectName} "${Src}" "${Deps}" utils_gameappshell_interface)
