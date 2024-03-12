if(TARGET glm)
    return()
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(
    cglm
    GIT_REPOSITORY https://github.com/recp/cglm
    GIT_TAG v0.9.2
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(cglm)
FetchContent_MakeAvailable(cglm)


# FetchContent_GetProperties(glfw)
# if(NOT glfw_POPULATED)
#     FetchContent_Populate(glfw)

#     set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "Build the GLFW example programs")
#     set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "Build the GLFW test programs")
#     set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "Build the GLFW documentation")
#     set(GLFW_INSTALL OFF CACHE INTERNAL "Generate installation target")

#     add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR})
# endif()

