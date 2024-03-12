cmake_minimum_required(VERSION 3.24)
project(scriptbots C)

# Disable GLFW tests, examples, and docss
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)

# Include external libraries
include(cmake/simde.cmake)

option(USE_OPENGL "Compile with OpenGL gui" YES)
option(BUILD_TESTS "Compile tests" NO)
option(USE_LTO "Compile with LTO optimizations" NO)

add_executable(${PROJECT_NAME}
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
	include/Agent.h
	include/AVXBrain.h
	include/Base.h
	include/helpers.h
	include/lock.h
	include/mtwister.h
	include/queue.h
	include/settings.h
	include/vec.h
	include/vec2f.h
	include/World.h
)

if (BUILD_TESTS)
	add_executable(test_vec2f
		src/vec2f.c
		src/helpers.c
		src/mtwister.c
		tests/vec2f.c
		include/Agent.h
		include/AVXBrain.h
		include/Base.h
		include/GLView.h
		include/helpers.h
		include/lock.h
		include/mtwister.h
		include/queue.h
		include/settings.h
		include/vec.h
		include/vec2f.h
		include/World.h
	)
	target_link_libraries(test_vec2f PRIVATE m)
	target_include_directories(test_vec2f PRIVATE ./include)
endif()

if (USE_LTO)
	set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if (USE_OPENGL)
	add_compile_definitions(OPENGL)

	target_sources(${PROJECT_NAME} PRIVATE include/GLView.h src/GLView.c)
endif()

include(cmake/glfw.cmake)
include(cmake/glad.cmake)
include(cmake/glm.cmake)

glad_add_library(glad_gl STATIC API gl:core=3.3)

target_link_libraries(${PROJECT_NAME} PRIVATE glfw cglm glad_gl)

target_include_directories(${PROJECT_NAME} PRIVATE ${simde_SOURCE_DIR})

set_property(TARGET ${PROJECT_NAME} PROPERTY C_STANDARD 17)

target_include_directories(${PROJECT_NAME} PRIVATE ./include)
