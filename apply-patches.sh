#!/bin/bash
# Android-specific source adjustments, applied to the build copy (idempotent).
set -eux
SRC=${1:-/work/PrusaSlicer-android}
cd "$SRC"

# GMP: clang rejects the C-only -std=gnu11 in CXXFLAGS (gcc ignores it)
if ! grep -q _gmp_cxxflags deps/+GMP/GMP.cmake; then
  perl -0pi -e 's/\Q"CXXFLAGS=${_gmp_ccflags}"\E/"CXXFLAGS=\${_gmp_cxxflags}"/; s/(\n(\s*)set\(_cfg_cmd env "CFLAGS)/\n$2string(REPLACE "\${_stdlib}" "" _gmp_cxxflags "\${_gmp_ccflags}")$1/' deps/+GMP/GMP.cmake
fi
grep -q 'string(REPLACE "${_stdlib}" "" _gmp_cxxflags' deps/+GMP/GMP.cmake

# OpenSSL: has its own android-* targets (uses ANDROID_NDK_ROOT + clang from PATH), no cross prefix
if ! grep -q 'android-${ANDROID_ABI}\|_ossl_android' deps/+OpenSSL/OpenSSL.cmake; then
  perl -0pi -e 's/\nelseif \(CMAKE_CROSSCOMPILING\)\n/\nelseif (ANDROID)\n    set(_conf_cmd env -u CC -u CXX -u AR -u RANLIB -u NM -u LD ".\/Configure")\n    set(_cross_arch android-\${ANDROID_ABI} -D__ANDROID_API__=\${ANDROID_NATIVE_API_LEVEL})\nelseif (CMAKE_CROSSCOMPILING)\n/' deps/+OpenSSL/OpenSSL.cmake
fi
grep -q 'android-${ANDROID_ABI}\|_ossl_android' deps/+OpenSSL/OpenSSL.cmake
# OpenSSL names the arm targets android-arm64 / android-arm, not after the Android ABI
if ! grep -q '_ossl_android' deps/+OpenSSL/OpenSSL.cmake; then
  perl -0pi -e 's/(\n(\s*))set\(_cross_arch android-\$\{ANDROID_ABI\}/$1if (ANDROID_ABI STREQUAL "arm64-v8a")\n$2    set(_ossl_android android-arm64)\n$2elseif (ANDROID_ABI STREQUAL "armeabi-v7a")\n$2    set(_ossl_android android-arm)\n$2else ()\n$2    set(_ossl_android android-\${ANDROID_ABI})\n$2endif ()\n$2set(_cross_arch \${_ossl_android}/' deps/+OpenSSL/OpenSSL.cmake
fi
grep -q 'set(_cross_arch ${_ossl_android}' deps/+OpenSSL/OpenSSL.cmake
# arm64: OpenSSL 3.5's poly1305 SVE2 assembly is not position independent and breaks linking libmain.so
sed -i 's|set(_ossl_android android-arm64)$|set(_ossl_android android-arm64 no-asm) # android: arm64 no-asm|' deps/+OpenSSL/OpenSSL.cmake
grep -q 'android: arm64 no-asm' deps/+OpenSSL/OpenSSL.cmake

# ---------------------------------------------------------------------------------------------
# Main build: Android = SDL platform + OpenGL ES instead of wxWidgets + desktop GL
# ---------------------------------------------------------------------------------------------

if ! grep -q 'SLIC3R_ANDROID_PORT_DIR' CMakeLists.txt; then
  perl -0pi -e 's/\nset\(SLIC3R_YAML /\nif (ANDROID)\n    # Android port: SDL + OpenGL ES, see SLIC3R_ANDROID_PORT_DIR\n    set(SLIC3R_OPENGL_ES ON)\n    set(SLIC3R_ENABLE_WEBKIT OFF)\n    set(SLIC3R_BUILD_TESTS OFF)\n    set(SLIC3R_ANDROID_PORT_DIR "" CACHE PATH "Directory with the Android platform sources")\n    add_compile_definitions(SLIC3R_OPENGL_ES=1 IMGUI_IMPL_OPENGL_ES3)\nendif ()\n\nset(SLIC3R_YAML /' CMakeLists.txt
fi
grep -q 'SLIC3R_ANDROID_PORT_DIR' CMakeLists.txt

if ! grep -q 'GLESv3' cmake/modules/GlobalDependencies.cmake; then
  perl -0pi -e 's/\nif\(SLIC3R_GUI\)\n(\s*)set\(OpenGL_GL_PREFERENCE "LEGACY"\)/\nif (SLIC3R_GUI AND ANDROID)\n    add_library(PlatformGL INTERFACE)\n    target_include_directories(PlatformGL INTERFACE \${SLIC3R_ANDROID_PORT_DIR}\/slic3r-platform-sdl\/gles-compat)\n    target_link_libraries(PlatformGL INTERFACE GLESv3 EGL)\nelseif(SLIC3R_GUI)\n$1set(OpenGL_GL_PREFERENCE "LEGACY")/' cmake/modules/GlobalDependencies.cmake
fi
grep -q 'GLESv3' cmake/modules/GlobalDependencies.cmake

if ! grep -q 'SLIC3R_ANDROID_PORT_DIR' src/CMakeLists.txt; then
  perl -0pi -e 's/(\n\s*)add_subdirectory\(slic3r-app-cli\)\s*\n\s*add_subdirectory\(slic3r-app-launcher\)\s*\n/$1if (NOT ANDROID)$1    add_subdirectory(slic3r-app-cli)$1    add_subdirectory(slic3r-app-launcher)$1endif ()\n/; s/\n(\s*)if \(NOT EMSCRIPTEN\)\n(\s*)add_subdirectory\(slic3r-platform-wx\)/\n$1if (ANDROID)\n$2add_subdirectory(\${SLIC3R_ANDROID_PORT_DIR}\/slic3r-platform-sdl \${CMAKE_BINARY_DIR}\/slic3r-platform-sdl)\n$2add_subdirectory(\${SLIC3R_ANDROID_PORT_DIR}\/slic3r-app-android \${CMAKE_BINARY_DIR}\/slic3r-app-android)\n$1elseif (NOT EMSCRIPTEN)\n$2add_subdirectory(slic3r-platform-wx)/' src/CMakeLists.txt
fi
grep -q 'slic3r-app-android' src/CMakeLists.txt

if ! grep -q 'slic3r-shared-android' src/slic3r-shared/CMakeLists.txt; then
  perl -0pi -e 's/\nelseif \(CMAKE_SYSTEM_NAME STREQUAL "Linux"\)\n(\s*)list\(APPEND SLIC3R_SHARED_FILES\n(\s*)src\/Slic3r\/Biz\/SHA256Linux.cpp/\nelseif (ANDROID)\n$1list(APPEND SLIC3R_SHARED_FILES\n$2src\/Slic3r\/Biz\/SHA256Linux.cpp\n$2\${SLIC3R_ANDROID_PORT_DIR}\/slic3r-shared-android\/AppInstanceMessageHandlerFactoryAndroid.cpp\n$2include\/Slic3r\/Biz\/AppInstance\/SingleInstanceCheckerLock.hpp\n$2src\/Slic3r\/Biz\/AppInstance\/SingleInstanceCheckerLock.cpp\n$2src\/Slic3r\/Biz\/RemovableDrive\/RemovableDriveServiceLinux.cpp\n$2src\/Slic3r\/Biz\/RemovableDrive\/RemovableDriveMonitorLinux.hpp\n$2src\/Slic3r\/Biz\/RemovableDrive\/RemovableDriveMonitorLinux.cpp\n$1)\nelseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")\n$1list(APPEND SLIC3R_SHARED_FILES\n$2src\/Slic3r\/Biz\/SHA256Linux.cpp/' src/slic3r-shared/CMakeLists.txt
fi
grep -q 'slic3r-shared-android' src/slic3r-shared/CMakeLists.txt

# imgui: no GLFW on Android (only the SDL and plain OpenGL backends are used)
if ! grep -q 'NOT ANDROID' bundled_deps/imgui/CMakeLists.txt; then
  perl -0pi -e 's/\nadd_library\(imgui_backend_ogl3_glfw STATIC/\nif (NOT ANDROID)\nadd_library(imgui_backend_ogl3_glfw STATIC/; s/(target_link_libraries\(imgui_backend_ogl3_glfw PUBLIC "-framework Cocoa" "-framework IOKit"\)\n    endif \(\)\nendif\(\))/$1\nendif () # NOT ANDROID/' bundled_deps/imgui/CMakeLists.txt
fi
grep -q 'endif () # NOT ANDROID' bundled_deps/imgui/CMakeLists.txt

# Boost.Process: bionic has no <wordexp.h>; use the shim (patched into the extracted source)
for BOOST_SHELL in deps/build*/dep_Boost-prefix/src/dep_Boost/libs/process/src/shell.cpp; do
  if [ -f "$BOOST_SHELL" ] && ! grep -q android_wordexp.h "$BOOST_SHELL"; then
    cp "/mnt/z/Prusa Slicer Googlebook OS/android-native/shims/android_wordexp.h" "$(dirname "$BOOST_SHELL")/"
    sed -i 's|#include <wordexp.h>|#if defined(__ANDROID__)\n#include "android_wordexp.h"\n#else\n#include <wordexp.h>\n#endif|' "$BOOST_SHELL"
  fi
done

# PNG: Linux takes the system libpng; Android has none, so build it like on macOS / Windows
if ! grep -q 'MSVC OR APPLE OR EMSCRIPTEN OR ANDROID' deps/+PNG/PNG.cmake; then
  sed -i 's/^if (MSVC OR APPLE OR EMSCRIPTEN)$/if (MSVC OR APPLE OR EMSCRIPTEN OR ANDROID)/' deps/+PNG/PNG.cmake
fi
grep -q 'MSVC OR APPLE OR EMSCRIPTEN OR ANDROID' deps/+PNG/PNG.cmake

# ---------------------------------------------------------------------------------------------
# Sources: Prusa's OpenGL ES branches (never compiled upstream) and clang-vs-gcc strictness
# ---------------------------------------------------------------------------------------------
FV=src/libvgcode/src/Slic3r/App/libvgcode/FdmViewer.cpp
FVH=src/libvgcode/include/Slic3r/App/libvgcode/FdmViewer.hpp
if ! grep -q 'android: ES texture path' "$FV"; then
  sed -i 's/Render::PixelFormat::/Domain::PixelFormat::/g; s/\.get_or_create_dynamic(format(/.get_or_create_dynamic(Slic3r::format(/g' "$FV"
  # Texture::set_data gained a data size argument
  perl -pi -e 's/set_data\((Domain::PixelFormat::RGBA32F), 0, w, h, (&\w+\[offset\])\);/set_data($1, 0, w, h, $2, size_t(w) * h * sizeof(Domain::Vec4f));/; s/set_data\((Domain::PixelFormat::R32F), 0, w, h, (&\w+\[offset\])\);/set_data($1, 0, w, h, $2, size_t(w) * h * sizeof(float));/; s/set_data\((Domain::PixelFormat::\w+), 0, w, h, nullptr\);/set_data($1, 0, w, h, nullptr, 0);/; s/set_data\((Domain::PixelFormat::R32UI), 0, w, (h|1), (\w+)\.data\(\)\);/set_data($1, 0, w, $2, $3.data(), $3.size() * sizeof(uint32_t));/' "$FV"
  sed -i '0,/^#include <map>/s//\/\/ android: ES texture path\n#include "Slic3r\/LegacyFormat.hpp"\n#include <map>/' "$FV"
  sed -i 's/void set_positions(const std::vector<Vec4f>& positions);/void set_positions(const std::vector<Domain::Vec4f>\& positions);/; s/void set_heights_widths_angles(const std::vector<Vec4f>& heights_widths_angles);/void set_heights_widths_angles(const std::vector<Domain::Vec4f>\& heights_widths_angles);/' "$FVH"
fi

grep -q 'boost/algorithm/string/predicate.hpp' src/slic3r-render/src/Slic3r/App/Render/Context.cpp || \
  sed -i '0,/^#include/s//#include <boost\/algorithm\/string\/predicate.hpp>\n#include/' src/slic3r-render/src/Slic3r/App/Render/Context.cpp
grep -q '"Slic3r/Assert.hpp"' src/slic3r-render/src/Slic3r/App/Render/GeometryBuilder.cpp || \
  sed -i '0,/^#include/s//#include "Slic3r\/Assert.hpp"\n#include/' src/slic3r-render/src/Slic3r/App/Render/GeometryBuilder.cpp

# clang (NDK r27, clang 18) has no CTAD for alias templates and is stricter about constexpr
sed -i 's/return unexpected{s.error()};/return ResultError{s.error()};/' src/slic3r-shared/src/Slic3r/Biz/Yaml/Yaml.hpp
sed -i 's/const constexpr Settings ARRANGE_SETTINGS{/const Settings ARRANGE_SETTINGS{/' src/slic3r-shared/src/Slic3r/Biz/ArrangeInteractor.cpp
sed -i 's/std::initializer_list{_u8L("Fast geometry"),/std::initializer_list<std::string>{_u8L("Fast geometry"),/' src/slic3r-shared/src/Slic3r/App/Plater/ArrangeDialog.cpp
# m_vertices became a shared_ptr; the non-texture-buffer branch was not updated
sed -i 's/heights_widths_angles.reserve(m_vertices.size());/heights_widths_angles.reserve(m_vertices->size());/; s/extract_pos_and_or_hwa(m_vertices, m_travels_radius/extract_pos_and_or_hwa(*m_vertices, m_travels_radius/' "$FV"

# Lua: built with plain make; the bare NDK clang lacks --target, so it compiled for the host (glibc)
if ! grep -q 'android: Lua' deps/+Lua/Lua.cmake; then
  perl -0pi -e 's/\n(\s*)if\(APPLE\)\n(\s*)set\(LUA_MAKE_TARGET "macosx"\)/\n$1if(ANDROID) # android: Lua\n$2set(LUA_MAKE_TARGET "posix")\n$2set(LUA_ENV "CC=\$ENV{CC}" "MYCFLAGS=-fPIC")\n$1elseif(APPLE)\n$2set(LUA_MAKE_TARGET "macosx")/' deps/+Lua/Lua.cmake
fi
grep -q 'android: Lua' deps/+Lua/Lua.cmake
# $ENV{CC} is whatever the configuring shell had: name the target's NDK clang wrapper explicitly
sed -i 's|"CC=$ENV{CC}"|"CC=${ANDROID_TOOLCHAIN_ROOT}/bin/${TOOLCHAIN_PREFIX}${ANDROID_NATIVE_API_LEVEL}-clang"|' deps/+Lua/Lua.cmake
grep -q 'TOOLCHAIN_PREFIX}${ANDROID_NATIVE_API_LEVEL}-clang' deps/+Lua/Lua.cmake

# glBlitFramebuffer and the READ/DRAW framebuffer bindings are core in OpenGL ES 3.0
perl -0pi -e 's/#if SLIC3R_OPENGL_ES\n(\s*)PANIC\("Not implemented yet"\);\n#else/#if 0 \/\/ android: blitting is core in OpenGL ES 3.0\n$1PANIC("Not implemented yet");\n#else/g' src/slic3r-render/src/Slic3r/App/Render/GL/GLDeviceInternal.cpp

# Report incomplete framebuffers in release builds too (only a DEBUG_ASSERT upstream)
FBGL=src/slic3r-render/src/Slic3r/App/Render/Framebuffer_gl.cpp
if ! grep -q 'android: framebuffer status' "$FBGL"; then
  perl -0pi -e 's/DEBUG_ASSERT\(glCheckFramebufferStatus\(self.m_target\) == GL_FRAMEBUFFER_COMPLETE\);/if (const GLenum status = glCheckFramebufferStatus(self.m_target); status != GL_FRAMEBUFFER_COMPLETE) \/\/ android: framebuffer status\n        SPDLOG_ERROR("Framebuffer {}x{} incomplete: 0x{:04x} (color attachments: {}, depth: {}, samples: {})", data.width, data.height, status, data.color_attachments.size(), data.depth, self.num_samples);/' "$FBGL"
  grep -q 'Slic3r/Log.hpp' "$FBGL" || sed -i '0,/^#include/s//#include "Slic3r\/Log.hpp"\n#include/' "$FBGL"
fi

# GL ES needs sized internal formats for render targets and GL_UNSIGNED_INT for 24-bit depth
GLT=src/slic3r-render/src/Slic3r/App/Render/GL/GLTypes.cpp
perl -0pi -e 's/case PixelFormat::RGB8:(\s+)return GL_RGB;\n(\s*)case PixelFormat::RGBA8:(\s+)return GL_RGBA;\n(\s*)case PixelFormat::R16F:(\s+)return GL_R16F;/case PixelFormat::RGB8:$1return GL_RGB8;\n$2case PixelFormat::RGBA8:$3return GL_RGBA8;\n$4case PixelFormat::R16F:$5return GL_R16F;/; s/case PixelFormat::DepthComponent: return GL_FLOAT;/case PixelFormat::DepthComponent: return GL_UNSIGNED_INT;/' "$GLT"

# HTTPS on Android: use the CA bundle the app builds from the system store (SSL_CERT_FILE)
HC=src/slic3r-shared/src/Slic3r/Biz/Network/HttpCurl.cpp
if ! grep -q 'android: CA bundle' "$HC"; then
  perl -0pi -e 's/(\n(\s*)::curl_easy_setopt\(m_curl.get\(\), CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1\);)/$1\n#ifdef __ANDROID__ \/\/ android: CA bundle\n$2if (const char* ca_bundle = ::getenv("SSL_CERT_FILE"))\n$2    ::curl_easy_setopt(m_curl.get(), CURLOPT_CAINFO, ca_bundle);\n#endif/' "$HC"
fi
grep -q 'android: CA bundle' "$HC"

# The bed texture is compressed to S3TC (DXT), which OpenGL ES lacks: keep it uncompressed
sed -i 's|^\(\s*\)opts.compressed = true;$|\1opts.compressed = false; // android: no S3TC in OpenGL ES|' src/slic3r-shared/src/Slic3r/App/Scene/BedRenderHelper.cpp

# G-code preview, ES path: the ES segments shader pulls vertices like the desktop one (gl_VertexID),
# but the C++ side still treated the node as an instanced mesh -> null component
if ! grep -q 'android: ES vertex pulling' "$FV"; then
  perl -0pi -e 's/(\.set_texture\(ENABLED_SEGMENTS_TEX_ID, es_tex\);\n\s*\n?)(\s*)node->set_material_override\(material\);\n\s*Scene::InstancedMeshRenderNodeComponent\* r_comp = dynamic_cast<Scene::InstancedMeshRenderNodeComponent\*>\(node->render_component\(\)\);\n\s*r_comp->set_instances_count\(count\);/$1$2\/\/ android: ES vertex pulling\n$2const std::size_t total_vertex_count{count * 24};\n$2material.set_uniform("total_vertex_count", (int) total_vertex_count);\n$2node->set_material_override(material);\n$2if (auto* r_comp = dynamic_cast<Scene::VertexPulledRenderNodeComponent*>(node->render_component()))\n$2    r_comp->set_total_vertex_count(total_vertex_count);/' "$FV"
fi
grep -q 'android: ES vertex pulling' "$FV"

# Prusa Account / Connect: slic3r-shared gates them on SLIC3R_HAS_WEBKIT; Android has its WebView
if ! grep -q 'android: WebView counts as webkit' src/slic3r-shared/CMakeLists.txt; then
  perl -0pi -e 's/\nif \(SLIC3R_ENABLE_WEBKIT\)\n(\s*)target_compile_definitions\(slic3r-shared PRIVATE SLIC3R_HAS_WEBKIT\)\nendif\(\)/\nif (SLIC3R_ENABLE_WEBKIT OR ANDROID) # android: WebView counts as webkit\n$1target_compile_definitions(slic3r-shared PRIVATE SLIC3R_HAS_WEBKIT)\nendif()/' src/slic3r-shared/CMakeLists.txt
fi
grep -q 'android: WebView counts as webkit' src/slic3r-shared/CMakeLists.txt

# Touchpad pinch: the camera zooms one full notch per wheel event whatever its size, which makes
# the many small gesture events jerky. Pass fractional wheel steps on to the camera instead.
ACG=src/slic3r-shared/src/Slic3r/App/Scene/AbstractCameraGizmo.cpp
if ! grep -q 'android: fractional zoom' "$ACG"; then
  perl -0pi -e 's/(\n(\s*))update_zoom\(wheel_delta_y\);/$1\/\/ android: fractional zoom (wheel_delta_y is 120 per notch)\n$2if (wheel_delta_y != 0)\n$2    m_scene_provider.scene().camera_trackball().update_zoom(wheel_delta_y \/ 120.f);/' "$ACG"
fi
grep -q 'android: fractional zoom' "$ACG"

# Prusa Account / Connect: the desktop switches it on in its configuration wizard; on Android on by default
APC=src/slic3r-shared/src/Slic3r/App/AppConfig.cpp
if ! grep -q 'android: account on by default' "$APC"; then
  perl -0pi -e 's/(def = defs.add\("enable_prusa_account", typeid\(bool\)\);(?:.|\n)*?def->init_fn = \[\]\(\) \{ return Domain::ConfigValue\()false(\); \};)/$1__ANDROID__$2 \/\/ android: account on by default/' "$APC"
  perl -0pi -e 's/ConfigValue\(__ANDROID__\)/ConfigValue(bool(__ANDROID__))/' "$APC"
fi
grep -q 'android: account on by default' "$APC"

# OpenGL ES: DEPTH_COMPONENT24 and 32-bit float textures are not filterable. Sampled with LINEAR they
# are incomplete and read as 0 - ANGLE then shades the whole background black (ao_lighting discards
# only depth == 1.0) and the SSAO noise breaks into diagonal patterns. Sample them NEAREST.
FBGL=src/slic3r-render/src/Slic3r/App/Render/Framebuffer_gl.cpp
if ! grep -q 'android: depth texture nearest' "$FBGL"; then
  perl -0pi -e 's/(set_data\(PixelFormat::DepthComponent, 0, data.width, data.height, nullptr, 0\);\n(\s*))m_textures.back\(\)->set_filtering\(TextureMinFilter::Linear, TextureMagFilter::Linear\);/$1m_textures.back()->set_filtering(TextureMinFilter::Nearest, TextureMagFilter::Nearest); \/\/ android: depth texture nearest/' "$FBGL"
fi
grep -q 'android: depth texture nearest' "$FBGL"
SCN=src/slic3r-shared/src/Slic3r/App/Scene/Scene.cpp
if ! grep -q 'android: noise nearest' "$SCN"; then
  perl -0pi -e 's/(\n(\s*)ao.noise_tex->set_data\(Domain::PixelFormat::RGB32F[^\n]*\n)/$1$2ao.noise_tex->set_filtering(Render::TextureMinFilter::Nearest, Render::TextureMagFilter::Nearest); \/\/ android: noise nearest\n/' "$SCN"
fi
grep -q 'android: noise nearest' "$SCN"

# Preferences -> Application: switch for the log copy in Downloads (PrusaSlicerActivity.exportLog)
APC=src/slic3r-shared/src/Slic3r/App/AppConfig.cpp
if ! grep -q 'android_log_export' "$APC"; then
  perl -0pi -e 's/(\n(\s*)def = defs.add\("restore_win_position", typeid\(bool\)\);)/\n#ifdef __ANDROID__\n$2def = defs.add("android_log_export", typeid(bool));\n$2def->location = Domain::AppConfigLocation{};\n$2def->gui_type = GUIType::checkbox;\n$2def->label = L("Log in Downloads speichern");\n$2def->tooltip = L("Kopiert das Protokoll alle paar Sekunden nach Downloads\/PrusaSlicer-log.txt (zur Fehlersuche).");\n$2def->category = Domain::ConfigItemDef::Category::AppConfig_General;\n$2def->option_group = Domain::ConfigItemDef::OptionGroup::AppConfig_General_Application;\n$2def->init_fn = []() { return Domain::ConfigValue(true); };\n#endif$1/' "$APC"
fi
grep -q 'android_log_export' "$APC"
