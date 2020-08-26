cmake_minimum_required(VERSION 3.4)
set(CMAKE_GENERATOR_PLATFORM x64)

project (SssrSample_${GFX_API})

# set options for FidelityFX SSSR
if(GFX_API STREQUAL DX12)        
    set(FFX_SSSR_D3D12 ON)   
elseif(GFX_API STREQUAL VK)
    set(FFX_SSSR_VK ON)   
endif()

# ouput exe to bin directory
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_HOME_DIRECTORY}/bin)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_HOME_DIRECTORY}/bin )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

# reference libs used by both backends
add_subdirectory(libs/cauldron)
add_subdirectory(../ffx-sssr libs/ffx-sssr)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

if(GFX_API STREQUAL DX12)        
    add_subdirectory(src/DX12)   

elseif(GFX_API STREQUAL VK)
    find_package(Vulkan REQUIRED)
    add_subdirectory(src/VK)

    message(STATUS ${VULKAN_LIBRARIES})

    # the Vulkan sample requires glslc.exe for GLSL shader compilation. copy it over from the Vulkan SDK 
    file(COPY  $ENV{VULKAN_SDK}/Bin/glslc.exe DESTINATION ${CMAKE_HOME_DIRECTORY}/bin)

else()
    message(STATUS "----------------------------------------------------------------------------------------")
    message(STATUS "")
    message(STATUS "** Almost there!!")
    message(STATUS "")
    message(STATUS " This framework supports DX12 or VULKAN, you need to invoke cmake in one of these ways:")
    message(STATUS "")
    message(STATUS " Examples:")
    message(STATUS "    cmake <project_root_dir> -DGFX_API=DX12")
    message(STATUS "    cmake <project_root_dir> -DGFX_API=VK")
    message(STATUS "")
    message(STATUS "----------------------------------------------------------------------------------------")
    message(FATAL_ERROR "")
endif()

