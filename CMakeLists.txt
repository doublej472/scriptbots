cmake_minimum_required(VERSION 3.28)
project(scriptbots LANGUAGES C VERSION "1.0.0")

option(BUILD_TESTS "Compile tests" NO)
if (BUILD_TESTS)
	add_executable(test_vec2f
		src/vec2f.c
		src/helpers.c
		src/mtwister.c
		src/GLView.c
		tests/vec2f.c
	)
	target_link_libraries(test_vec2f PRIVATE m)
	target_include_directories(test_vec2f PUBLIC ./include)
endif()

add_executable(scriptbots
	src/main.c
	# src/vec2f.c
	# src/queue.c
	# src/vec.c
	# src/Base.c
	# src/Agent.c
	src/AVXBrain.c
	# src/helpers.c
	# src/World.c
	# src/lock.c
	# src/mtwister.c
)

include(CheckCCompilerFlag)

CHECK_C_COMPILER_FLAG("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
	option(MARCH_NATIVE "Compile with -march=native" NO)
	if (MARCH_NATIVE)
		target_link_options(scriptbots PRIVATE -march=native)
		target_compile_options(scriptbots PRIVATE -march=native)
	endif()
endif()

CHECK_C_COMPILER_FLAG("-fsanitize=address" COMPILER_SUPPORTS_ASAN)
if(COMPILER_SUPPORTS_ASAN)
	option(ASAN "Compile with address sanitation enabled" NO)
	if (ASAN)
		target_link_options(scriptbots PRIVATE -fsanitize=address)
		target_compile_options(scriptbots PRIVATE -fsanitize=address)
	endif()
endif()

CHECK_C_COMPILER_FLAG("-pg" COMPILER_SUPPORTS_GPROF)
if(COMPILER_SUPPORTS_GPROF)
	option(GPROF "Compile with -pg" NO)
		if (GPROF)
		target_link_options(scriptbots PRIVATE -pg)
		target_compile_options(scriptbots PRIVATE -pg)
	endif()
endif()

CHECK_C_COMPILER_FLAG("-fgraphite-identity -floop-nest-optimize" COMPILER_SUPPORTS_GRAPHITE)
if(COMPILER_SUPPORTS_GRAPHITE)
	option(GRAPHITE "Compile with graphite optimizations" NO)
	if (GRAPHITE)
		target_link_options(scriptbots PRIVATE -fgraphite-identity -floop-nest-optimize)
		target_compile_options(scriptbots PRIVATE -fgraphite-identity -floop-nest-optimize)
	endif()
endif()

CHECK_C_COMPILER_FLAG("-fipa-pta" COMPILER_SUPPORTS_IPAPTA)
if(COMPILER_SUPPORTS_IPAPTA)
	option(IPAPTA "Compile with ipa-pta optimizations" NO)
	if (IPAPTA)
		target_link_options(scriptbots PRIVATE -fipa-pta)
		target_compile_options(scriptbots PRIVATE -fipa-pta)
	endif()
endif()

CHECK_C_COMPILER_FLAG("-fdevirtualize-at-ltrans" COMPILER_SUPPORTS_DEVIRTLTO)
if(COMPILER_SUPPORTS_DEVIRTLTO)
	option(DEVIRTLTO "Compile with LTO devirtualization optimizations" NO)
	if (DEVIRTLTO)
		target_link_options(scriptbots PRIVATE -fdevirtualize-at-ltrans)
		target_compile_options(scriptbots PRIVATE -fdevirtualize-at-ltrans)
	endif()
endif()

option(USE_LTO "Compile with LTO optimizations" NO)
if (USE_LTO)
	set_property(TARGET scriptbots PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

find_package(Vulkan REQUIRED)
target_link_libraries(scriptbots PRIVATE Vulkan::Vulkan)

option(USE_SYSTEM_SDL3 "Compile with system SDL3 libraries" NO)
if (NOT USE_SYSTEM_SDL3)
	set(SDL_SHARED ON)
	set(SDL_VENDOR_INFO "scriptbots")
	set(SDL_VULKAN ON)
	include(cmake/sdl3.cmake)
else()
	find_package(SDL3 CONFIG REQUIRED)
endif()
target_link_libraries(scriptbots PRIVATE SDL3::SDL3)

include(cmake/simde.cmake)
target_link_libraries(scriptbots PRIVATE simde::simde)

include(cmake/shaders.cmake)
add_shaders(
	shaders/basic.vert
	shaders/basic.frag
)

target_include_directories(scriptbots PRIVATE ./include)

if(WIN32 AND NOT USE_SYSTEM_SDL3)
    add_custom_command(
        TARGET scriptbots POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:scriptbots>
        VERBATIM
    )
endif()
