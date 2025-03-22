if(TARGET simde::simde)
    return()
endif()

message(STATUS "Third-party: creating target 'simde::simde'")

include(FetchContent)
FetchContent_Declare(
    simde
    GIT_REPOSITORY https://github.com/simd-everywhere/simde.git
    GIT_TAG v0.8.4-rc1
)
FetchContent_MakeAvailable(simde)

add_library(simde::simde INTERFACE IMPORTED GLOBAL)
target_include_directories(simde::simde INTERFACE "${simde_SOURCE_DIR}")

# Uncomment this line to ensure code can be compiled without native SIMD (i.e. emulates everything)
# target_compile_definitions(simde::simde INTERFACE SIMDE_NO_NATIVE)
