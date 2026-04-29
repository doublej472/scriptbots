cmake_minimum_required(VERSION 3.28)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Create compile_commands.json" FORCE)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Path to vcpkg.cmake" FORCE)

project(scriptbots)

option(USE_OPENGL "Compile with Vulkan+GLFW" YES)
option(BUILD_TESTS "Compile tests" NO)
option(USE_LTO "Compile with LTO optimizations" YES)
option(MARCH_NATIVE "Compile with -march=native" YES)
option(ASAN "Compile with address sanitation enabled" NO)
option(TRAP_NAN "Make program exit on math errors" NO)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
	option(GPROF "Compile with -pg" NO)
	option(GRAPHITE "Compile with graphite optimizations" NO)
	option(IPAPTA "Compile with ipa-pta optimizations" NO)
	option(DEVIRTLTO "Compile with LTO devirtualization optimizations" NO)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOSX TRUE)
endif()

# ---- Shader compilation (build time) ----
set(SHADER_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

set(SHADER_SOURCES
    agent_circle.vert agent_circle.frag
    agent_lines.vert  agent_lines.frag
    agent_hud.vert    agent_hud.frag
    food.vert         food.frag
)

foreach(SHADER ${SHADER_SOURCES})
    add_custom_command(
        OUTPUT "${SHADER_OUT_DIR}/${SHADER}.spv"
        COMMAND glslc -O "${SHADER_DIR}/${SHADER}" -o "${SHADER_OUT_DIR}/${SHADER}.spv"
        DEPENDS "${SHADER_DIR}/${SHADER}"
        COMMENT "Compiling shader: ${SHADER}"
    )
    list(APPEND SPV_FILES "${SHADER_OUT_DIR}/${SHADER}.spv")
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPV_FILES})

# ---- Core executable ----
add_executable(scriptbots
	src/vec2f.c
	src/queue.c
	src/vec.c
	src/Base.c
	src/Agent.c
	src/main.c
	src/AVXBrain.c
	src/helpers.c
	src/World.c
	src/lock.c
	src/mtwister.c
	src/Food.c
	include/settings.h
	include/vec.h
	include/mtwister.h
	include/lock.h
	include/Agent.h
	include/World.h
	include/vec2f.h
	include/helpers.h
	include/AVXBrain.h
	include/queue.h
	include/Base.h
	include/Food.h
)

# ---- Compile options ----
if (TRAP_NAN)
	add_compile_definitions(TRAP_NAN)
endif()

if (GPROF)
	target_link_options(scriptbots PRIVATE -pg)
	target_compile_options(scriptbots PRIVATE -pg)
endif()

if (ASAN)
	target_link_options(scriptbots PRIVATE -fsanitize=address)
	target_compile_options(scriptbots PRIVATE -fsanitize=address)
endif()

if (DEVIRTLTO)
	target_link_options(scriptbots PRIVATE -fdevirtualize-at-ltrans)
	target_compile_options(scriptbots PRIVATE -fdevirtualize-at-ltrans)
endif()

if (IPAPTA)
	target_link_options(scriptbots PRIVATE -fipa-pta)
	target_compile_options(scriptbots PRIVATE -fipa-pta)
endif()

if (GRAPHITE)
	target_link_options(scriptbots PRIVATE -fgraphite-identity -floop-nest-optimize)
	target_compile_options(scriptbots PRIVATE -fgraphite-identity -floop-nest-optimize)
endif()

if (MARCH_NATIVE)
	target_link_options(scriptbots PRIVATE -march=native)
	target_compile_options(scriptbots PRIVATE -march=native)
else()
	if (MACOSX)
		target_link_options(scriptbots PRIVATE -mcpu=apple-m1)
		target_compile_options(scriptbots PRIVATE -mcpu=apple-m1)
	else()
		target_link_options(scriptbots PRIVATE -march=x86-64-v4)
		target_compile_options(scriptbots PRIVATE -march=x86-64-v4)
	endif()
endif()

# ---- Tests ----
if (BUILD_TESTS)
	add_executable(test_vec2f
		src/vec2f.c
		src/helpers.c
		src/mtwister.c
		tests/vec2f.c
		include/settings.h
		include/vec.h
		include/mtwister.h
		include/lock.h
		include/Agent.h
		include/World.h
		include/vec2f.h
		include/helpers.h
		include/AVXBrain.h
		include/queue.h
		include/Base.h
	)
	target_link_libraries(test_vec2f PRIVATE m)
	target_include_directories(test_vec2f PUBLIC ./include)
endif()

# ---- LTO ----
if (USE_LTO)
	set_property(TARGET scriptbots PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# ---- Vulkan / GLFW / ImGui ----
if (USE_OPENGL)
	find_package(glfw3 CONFIG REQUIRED)
	find_package(imgui CONFIG REQUIRED)

	# Use system Vulkan, not vcpkg's
	find_package(Vulkan REQUIRED)

	# glm from system (or vcpkg if not found)
	find_package(glm QUIET)
	if(NOT glm_FOUND)
		find_path(GLM_INCLUDE_DIR "glm/glm.hpp" REQUIRED)
	endif()

	add_compile_definitions(OPENGL)

	# New Vulkan sources
	target_sources(scriptbots PRIVATE
		src/vkinit.c
		src/vkdraw.c
		src/vkview.cpp
		src/imgui_impl_glfw.cpp
		src/imgui_impl_vulkan.cpp
		include/vkhelpers.h
		include/vkview.h
	)

	# ImGui: link vcpkg's imgui target (provides include path for imgui.h)
	target_link_libraries(scriptbots PRIVATE
		glfw
		Vulkan::Vulkan
		imgui::imgui
		m
		wayland-client
		wayland-cursor
		wayland-egl
		xkbcommon
		decor-0
	)

	if(glm_FOUND)
		target_link_libraries(scriptbots PRIVATE glm::glm)
	else()
		target_include_directories(scriptbots PRIVATE ${GLM_INCLUDE_DIR})
	endif()

	# Ensure shaders are built before the executable
	add_dependencies(scriptbots shaders)

	# Copy SPV files next to binary for runtime loading
	add_custom_command(TARGET scriptbots POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${SHADER_OUT_DIR}" "$<TARGET_FILE_DIR:scriptbots>/shaders"
	)
endif()

# ---- SIMD ----
find_path(SIMDE_INCLUDE_DIRS "simde/x86/avx512.h")
target_include_directories(scriptbots PRIVATE ${SIMDE_INCLUDE_DIRS})

# ---- Compile flags ----
target_compile_options(scriptbots PRIVATE -Wall)
target_link_options(scriptbots PRIVATE -pthread)
target_link_libraries(scriptbots PRIVATE m)
target_include_directories(scriptbots PUBLIC ./include)
