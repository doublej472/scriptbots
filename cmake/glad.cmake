if(TARGET glad)
    return()
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# FetchContent_Declare(
#     glew
#     GIT_REPOSITORY https://github.com/nigels-com/glew
#     GIT_TAG glew-2.2.0
#     GIT_SHALLOW TRUE
# )

# FetchContent_MakeAvailable(glew)

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad
    GIT_TAG v2.0.5
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
    add_subdirectory("${glad_SOURCE_DIR}/cmake" glad_cmake)
endif()


# FetchContent_GetProperties(glfw)
# if(NOT glfw_POPULATED)
#     FetchContent_Populate(glfw)

#     set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Build the GLFW example programs")
#     set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Build the GLFW test programs")
#     set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Build the GLFW documentation")
#     set(GLFW_INSTALL OFF CACHE INTERNAL "Generate installation target")

#     add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR})
# endif()

