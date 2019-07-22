cmake_minimum_required(VERSION 3.10.0)

project(stochastic-screen-space-reflections)

find_package(PythonInterp 3.6 REQUIRED)

option(SSSR_NO_D3D12 "Stochastic Screen Space Reflections - Skip D3D12 backend" OFF)
option(SSSR_NO_VK "Stochastic Screen Space Reflections - Skip VK backend" OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (MSVC)
    add_compile_options(/W3 /WX)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall -Werror -std=c++17")
endif()

file(GLOB SSSR_HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/ffx_sssr.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
file(GLOB SSSR_INLINE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.inl)
file(GLOB SSSR_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB SSSR_SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsl)

if(SSSR_NO_D3D12)
else()
    file(GLOB SSSR_HEADER_FILES_D3D12
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/ffx_sssr_d3d12.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.h)
    file(GLOB SSSR_INLINE_FILES_D3D12
        ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.inl)
    file(GLOB SSSR_SOURCE_FILES_D3D12
        ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.cpp)
endif()

if(SSSR_NO_VK)
else()
    file(GLOB SSSR_HEADER_FILES_VK
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/ffx_sssr_vk.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vk/*.h)
    file(GLOB SSSR_INLINE_FILES_VK
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vk/*.inl)
    file(GLOB SSSR_SOURCE_FILES_VK
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vk/*.cpp)
endif()

set(SSSR_HEADER_FILES_SHADERS)

foreach(shaderfile classify_tiles
				   common
				   intersect
				   prepare_indirect_args
				   resolve_eaw
				   resolve_eaw_stride
				   resolve_spatial
				   resolve_temporal)
					
	add_custom_command(
		OUTPUT 
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_${shaderfile}.h
        DEPENDS 
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shaderfile}.hlsl
		COMMAND 
            ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/sourceToHeader.py ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shaderfile}.hlsl ${shaderfile} > ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_${shaderfile}.h
		COMMENT
            "Generate shader header shader_${shaderfile}.h for ${shaderfile}.hlsl"
        USES_TERMINAL)
	
	list(APPEND SSSR_HEADER_FILES_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_${shaderfile}.h")

endforeach()

add_library(FFX_SSSR
    ${SSSR_SOURCE_FILES}
    ${SSSR_SOURCE_FILES_D3D12}
    ${SSSR_SOURCE_FILES_VK}
    ${SSSR_HEADER_FILES_SHADERS})

target_include_directories(FFX_SSSR PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/externals)
target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/externals/dxc)
target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/externals/samplerCPP)

if(SSSR_NO_D3D12)
    target_compile_definitions(FFX_SSSR PRIVATE SSSR_NO_D3D12)
endif()

if(SSSR_NO_VK)
    target_compile_definitions(FFX_SSSR PRIVATE SSSR_NO_VK)
endif()

target_sources(FFX_SSSR PRIVATE
    ${SSSR_HEADER_FILES}
    ${SSSR_INLINE_FILES}
    ${SSSR_SHADER_FILES}
    ${SSSR_HEADER_FILES_D3D12}
    ${SSSR_INLINE_FILES_D3D12}
    ${SSSR_HEADER_FILES_VK}
    ${SSSR_INLINE_FILES_VK})

source_group("Header Files\\Shaders"    FILES ${SSSR_HEADER_FILES_SHADERS})

source_group("Inline Files"             FILES ${SSSR_INLINE_FILES})
source_group("Media Files\\Shaders"     FILES ${SSSR_SHADER_FILES})


source_group("Header Files\\D3D12"      FILES ${SSSR_HEADER_FILES_D3D12})
source_group("Inline Files\\D3D12"      FILES ${SSSR_INLINE_FILES_D3D12})
source_group("Source Files\\D3D12"      FILES ${SSSR_SOURCE_FILES_D3D12})

source_group("Header Files\\VK"         FILES ${SSSR_HEADER_FILES_VK})
source_group("Inline Files\\VK"         FILES ${SSSR_INLINE_FILES_VK})
source_group("Source Files\\VK"         FILES ${SSSR_SOURCE_FILES_VK})

if(MSVC)
    set_source_files_properties(${SSSR_SHADER_FILES}
        PROPERTIES
        VS_TOOL_OVERRIDE
        "None")
endif()
