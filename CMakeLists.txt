cmake_minimum_required(VERSION 3.28)
project(scriptbots)

option(USE_OPENGL "Compile with OpenGL gui" YES)
option(BUILD_TESTS "Compile tests" NO)
option(USE_LTO "Compile with LTO optimizations" YES)
option(MARCH_NATIVE "Compile with -march=native" NO)
option(ASAN "Compile with address sanitation enabled" NO)
option(TRAP_NAN "Make program exit on math errors" NO)
option(FASTMATH "Compile with fast math optimizations (untested)" NO)

if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
	option(GPROF "Compile with -pg" NO)
	option(GRAPHITE "Compile with graphite optimizations" NO)
	option(IPAPTA "Compile with ipa-pta optimizations" NO)
	option(DEVIRTLTO "Compile with LTO devirtualization optimizations" NO)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(MACOSX TRUE)
endif()

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
	include/GLView.h
	include/Base.h
)

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

if (FASTMATH)
	target_link_options(scriptbots PRIVATE -ffast-math)
	target_compile_options(scriptbots PRIVATE -ffast-math)
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
		include/GLView.h
		include/Base.h
	)
	target_link_libraries(test_vec2f PRIVATE m)
	target_include_directories(test_vec2f PUBLIC ./include)
endif()

if (USE_LTO)
	set_property(TARGET scriptbots PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if (USE_OPENGL)
	add_compile_definitions(OPENGL)

	if(WIN32)
		find_package(GLEW REQUIRED)
		find_package(GLUT REQUIRED)

    add_dependencies(scriptbots GLEW::GLEW GLUT::GLUT)
    target_link_libraries(scriptbots PRIVATE GLEW::GLEW GLUT::GLUT m)
		target_sources(scriptbots PRIVATE src/GLView.c)
    elseif(MACOSX)
		find_package(GLEW REQUIRED)
		find_package(FreeGLUT REQUIRED)

    add_dependencies(scriptbots GLEW::GLEW FreeGLUT::freeglut)
    target_link_libraries(scriptbots PRIVATE GLEW::GLEW FreeGLUT::freeglut m)
		target_sources(scriptbots PRIVATE src/GLView.c)
	else()
		find_package(OpenGL REQUIRED)
		find_package(GLUT REQUIRED)

    add_dependencies(scriptbots OpenGL::OpenGL GLUT::GLUT)
    target_link_libraries(scriptbots PRIVATE OpenGL::OpenGL GLUT::GLUT m)
		target_sources(scriptbots PRIVATE src/GLView.c)
	endif()
endif()

include(cmake/simde.cmake)
target_link_libraries(scriptbots PRIVATE simde::simde)

target_compile_options(scriptbots PRIVATE -Wall)
target_link_options(scriptbots PRIVATE -pthread)
target_include_directories(scriptbots PUBLIC ./include)
