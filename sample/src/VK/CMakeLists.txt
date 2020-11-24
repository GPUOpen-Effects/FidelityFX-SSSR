project (SssrSample_VK)

add_compile_options(/MP)

file(GLOB Sources_src 
	Sources/*.h
	Sources/*.cpp
	)

file(GLOB Shaders_src
    ../Shaders/*.hlsl
    ../Shaders/*.h
    ../../../ffx-dnsr/ffx-reflection-dnsr/*.h
    ../../../ffx-dnsr/ffx-reflection-dnsr/*.hlsl
	../../../ffx-sssr/*.h
    ../../../ffx-sssr/*.hlsl
	../../../ffx-spd/*.h
    ../../../ffx-spd/*.hlsl
	)

file(GLOB Common_src
	../Common/config.json
)
    
source_group("Sources"            FILES ${Sources_src})    
source_group("Shaders"            FILES ${Shaders_src})    
source_group("Common"             FILES ${Common_src})    

set_source_files_properties(${Shaders_src} PROPERTIES VS_TOOL_OVERRIDE "Text")
set_source_files_properties(${Common_src} PROPERTIES VS_TOOL_OVERRIDE "Text")
    
function(copyCommand list dest)
	foreach(fullFileName ${list})    
		get_filename_component(file ${fullFileName} NAME)
		message("Generating custom command for ${fullFileName}")
		add_custom_command(
			OUTPUT   ${dest}/${file}
			PRE_BUILD
			COMMAND cmake -E make_directory ${dest}
			COMMAND cmake -E copy ${fullFileName}  ${dest}
			MAIN_DEPENDENCY  ${fullFileName}
			COMMENT "Updating ${file} into ${dest}" 
		)
	endforeach()    
endfunction()

copyCommand("${Shaders_src}" ${CMAKE_HOME_DIRECTORY}/bin/ShaderLibVK)
copyCommand("${Common_src}" ${CMAKE_HOME_DIRECTORY}/bin)

add_executable(${PROJECT_NAME} WIN32 ${Sources_src} ${Shaders_src} ${Common_src}) 
target_link_libraries (${PROJECT_NAME} LINK_PUBLIC Cauldron_VK ImGUI)

set_target_properties(${PROJECT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_HOME_DIRECTORY}/bin")

IF (MSVC)
	IF (CMAKE_MAJOR_VERSION LESS 3)
		MESSAGE(WARNING "CMake version 3.0 or newer is required use build variable TARGET_FILE")
	ELSE()
		ADD_CUSTOM_COMMAND(
			TARGET ${PROJECT_NAME}
			POST_BUILD
			COMMAND "mt.exe" -manifest \"${CMAKE_CURRENT_SOURCE_DIR}\\dpiawarescaling.manifest\" -inputresource:\"$<TARGET_FILE:${PROJECT_NAME}>\"\;\#1 -outputresource:\"$<TARGET_FILE:${PROJECT_NAME}>\"\;\#1
			COMMENT "Adding display aware manifest..." 
		)
	ENDIF()
ENDIF(MSVC)