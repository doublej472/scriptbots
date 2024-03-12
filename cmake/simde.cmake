if(TARGET simde)
    return()
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)
FetchContent_Declare(
    simde
    GIT_REPOSITORY https://github.com/simd-everywhere/simde.git
    GIT_TAG v0.8.0-rc2
    GIT_SHALLOW TRUE
)

if(NOT simde_POPULATED)
    FetchContent_Populate(simde)
endif()


# FetchContent_MakeAvailable(simde)
# target_include_directories(simde INTERFACE "${simde_SOURCE_DIR}")

# add_library(simde::simde INTERFACE IMPORTED GLOBAL)
# target_include_directories(simde::simde INTERFACE "${simde_SOURCE_DIR}")

# Enables native aliases. Not ideal but makes it easier to convert old code.
# target_compile_definitions(simde::simde INTERFACE SIMDE_ENABLE_NATIVE_ALIASES)

# Uncomment this line to ensure code can be compiled without native SIMD (i.e. emulates everything)
# target_compile_definitions(simde::simde INTERFACE SIMDE_NO_NATIVE)
