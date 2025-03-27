if(TARGET SDL3::SDL3)
    return()
endif()

message(STATUS "Third-party: creating target 'SDL3::SDL3'")

include(FetchContent)
FetchContent_Declare(
    VENDORED_SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.8
)
FetchContent_MakeAvailable(VENDORED_SDL3)
