cmake_minimum_required(VERSION 3.10.0)

project(stochastic-screen-space-reflections)

find_package(PythonInterp 3.6 REQUIRED)

# ensure that only one option is enabled
if(FFX_SSSR_VK AND FFX_SSSR_D3D12)
    message(FATAL_ERROR "FFX_SSSR_VK and FFX_SSSR_D3D12 are enabled. Please make sure to enable only one at a time.")
endif()

if(FFX_SSSR_VK)
    find_package(Vulkan REQUIRED)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (MSVC)
    add_compile_options(/W3 /WX)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall -Werror -std=c++17")
endif()

file(GLOB FFX_SSSR_HEADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/ffx_sssr.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
file(GLOB FFX_SSSR_INLINE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.inl)
file(GLOB FFX_SSSR_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB FFX_SSSR_SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsl)

if(FFX_SSSR_D3D12)
    file(GLOB FFX_SSSR_HEADER_FILES_D3D12
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/ffx_sssr_d3d12.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.h)
    file(GLOB FFX_SSSR_INLINE_FILES_D3D12
        ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.inl)
    file(GLOB FFX_SSSR_SOURCE_FILES_D3D12
        ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12/*.cpp)
endif()

if(FFX_SSSR_VK)
    file(GLOB FFX_SSSR_HEADER_FILES_VK
        ${CMAKE_CURRENT_SOURCE_DIR}/inc/ffx_sssr_vk.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vk/*.h)
    file(GLOB FFX_SSSR_INLINE_FILES_VK
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vk/*.inl)
    file(GLOB FFX_SSSR_SOURCE_FILES_VK
        ${CMAKE_CURRENT_SOURCE_DIR}/src/vk/*.cpp)
endif()

set(FFX_SSSR_HEADER_FILES_SHADERS)

foreach(shaderfile classify_tiles
				   common
				   intersect
				   prepare_indirect_args
				   resolve_eaw
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
	
	list(APPEND FFX_SSSR_HEADER_FILES_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_${shaderfile}.h")

endforeach()

add_library(FFX_SSSR
    ${FFX_SSSR_SOURCE_FILES}
    ${FFX_SSSR_SOURCE_FILES_D3D12}
    ${FFX_SSSR_SOURCE_FILES_VK}
    ${FFX_SSSR_HEADER_FILES_SHADERS})

target_include_directories(FFX_SSSR PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/inc)
target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/externals)
target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/externals/dxc)
target_include_directories(FFX_SSSR PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/externals/samplerCPP)

if(FFX_SSSR_D3D12)
    target_compile_definitions(FFX_SSSR PRIVATE FFX_SSSR_D3D12)
endif()

if(FFX_SSSR_VK)
    target_compile_definitions(FFX_SSSR PRIVATE FFX_SSSR_VK)
endif()

target_sources(FFX_SSSR PRIVATE
    ${FFX_SSSR_HEADER_FILES}
    ${FFX_SSSR_INLINE_FILES}
    ${FFX_SSSR_SHADER_FILES}
    ${FFX_SSSR_HEADER_FILES_D3D12}
    ${FFX_SSSR_INLINE_FILES_D3D12}
    ${FFX_SSSR_HEADER_FILES_VK}
    ${FFX_SSSR_INLINE_FILES_VK})

source_group("Header Files\\Shaders"    FILES ${FFX_SSSR_HEADER_FILES_SHADERS})

source_group("Inline Files"             FILES ${FFX_SSSR_INLINE_FILES})
source_group("Media Files\\Shaders"     FILES ${FFX_SSSR_SHADER_FILES})


source_group("Header Files\\D3D12"      FILES ${FFX_SSSR_HEADER_FILES_D3D12})
source_group("Inline Files\\D3D12"      FILES ${FFX_SSSR_INLINE_FILES_D3D12})
source_group("Source Files\\D3D12"      FILES ${FFX_SSSR_SOURCE_FILES_D3D12})

source_group("Header Files\\VK"         FILES ${FFX_SSSR_HEADER_FILES_VK})
source_group("Inline Files\\VK"         FILES ${FFX_SSSR_INLINE_FILES_VK})
source_group("Source Files\\VK"         FILES ${FFX_SSSR_SOURCE_FILES_VK})

if(MSVC)
    set_source_files_properties(${FFX_SSSR_SHADER_FILES}
        PROPERTIES
        VS_TOOL_OVERRIDE
        "None")
endif()


if(FFX_SSSR_VK)
    target_link_libraries (FFX_SSSR Vulkan::Vulkan)
endif()
