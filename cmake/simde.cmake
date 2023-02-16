if(TARGET simde::simde)
    return()
endif()

message(STATUS "Third-party: creating target 'simde::simde'")

include(FetchContent)
FetchContent_Declare(
    simde
    GIT_REPOSITORY https://github.com/simd-everywhere/simde.git
    GIT_TAG 3465b57c68ed4d5bef6678507541369c36548c7e
)
FetchContent_MakeAvailable(simde)

add_library(simde::simde INTERFACE IMPORTED GLOBAL)
target_include_directories(simde::simde INTERFACE "${simde_SOURCE_DIR}")

# Enables native aliases. Not ideal but makes it easier to convert old code.
target_compile_definitions(simde::simde INTERFACE SIMDE_ENABLE_NATIVE_ALIASES)

# Uncomment this line to ensure code can be compiled without native SIMD (i.e. emulates everything)
# target_compile_definitions(simde::simde INTERFACE SIMDE_NO_NATIVE)
