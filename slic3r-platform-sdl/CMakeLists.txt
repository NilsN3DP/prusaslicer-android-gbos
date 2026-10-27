# SDL2 implementation of slic3r-platform (used by the Android app instead of slic3r-platform-wx).
add_library(slic3r-platform-sdl STATIC
    include/Slic3r/App/Platform/SDL/SDLRenderCanvas.hpp
    src/Slic3r/App/Platform/SDL/SDLRenderCanvas.cpp
    include/Slic3r/App/Platform/SDL/SDLMainThreadDispatcher.hpp
    src/Slic3r/App/Platform/SDL/SDLMainThreadDispatcher.cpp
)

find_package(SDL2 2 REQUIRED)

target_link_libraries(slic3r-platform-sdl PUBLIC
    SDL2::SDL2
    PlatformGL
    imgui_backend_ogl3
    slic3r-shared
)
target_include_directories(slic3r-platform-sdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
