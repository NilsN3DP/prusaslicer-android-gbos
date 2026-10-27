# PrusaSlicer Android app: libmain.so, loaded by SDLActivity (see android-native/app).
add_library(main SHARED
    src/AndroidApp.cpp
    src/AndroidBridge.hpp
    src/AndroidBridge.cpp
    src/AndroidDialogManager.hpp
    src/AndroidDialogManager.cpp
    src/AndroidFontManager.hpp
    src/AndroidFontManager.cpp
    src/AndroidSecretStore.hpp
    src/AndroidSecretStore.cpp
    src/AndroidWebView.hpp
    src/AndroidWebView.cpp
)

target_link_libraries(main PRIVATE
    slic3r-platform-sdl
    slic3r-shared
    android
    log
)

# SDLActivity looks up SDL_main in libmain.so; keep it and the JNI entry points exported.
target_link_options(main PRIVATE -Wl,--export-dynamic-symbol=SDL_main -Wl,--no-undefined)
