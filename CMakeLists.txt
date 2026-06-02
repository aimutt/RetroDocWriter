cmake_minimum_required(VERSION 3.24)
project(RetroDocWriter VERSION 0.1.0 LANGUAGES CXX)

# Windows resource compiler — used to embed the app icon and version info
# (RetroDocWriter.rc). Gated to Windows so a future cross-platform port is
# unaffected.
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable SDL build targets we don't need to keep compile times short
set(SDL_SHARED       ON  CACHE BOOL "Build SDL as a shared library" FORCE)
set(SDL_STATIC       OFF CACHE BOOL "Build SDL as a static library" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL_test library"        FORCE)
set(SDL_TESTS        OFF CACHE BOOL "Build SDL tests"               FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "Build SDL examples"            FORCE)
add_subdirectory(SDL)

# SDL_ttf — vendored prebuilt VC binaries (SDL3_ttf-devel-3.2.2-VC).
# The shipped Config.cmake ends with check_required_components(), a macro that
# CMake normally injects when packages are generated via
# configure_package_config_file. The vendored config is missing that injection,
# so define a no-op stub before find_package picks it up.
if(NOT COMMAND check_required_components)
    macro(check_required_components _name)
    endmacro()
endif()
find_package(SDL3_ttf CONFIG REQUIRED
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/SDL_ttf/cmake" NO_DEFAULT_PATH)

# SDL_image — vendored prebuilt VC binaries (SDL3_image-devel-3.2.4-VC), same
# layout as SDL_ttf. Reuses the check_required_components stub defined above.
find_package(SDL3_image CONFIG REQUIRED
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/SDL_image/cmake" NO_DEFAULT_PATH)

# Single-product layout: every .cpp lives under src/ with subdirectories
# editor/, platform/, render/, ui/, app/. The src/ directory is the include
# root so existing #include "editor/CharStyle.h" style paths resolve.

# ---------------------------------------------------------------------------
# RetroDocCore — the SDL-free logic core (editor model, RTF I/O, settings,
# screen buffer, theme). Built once as a STATIC library and linked by BOTH the
# GUI app and the unit-test executable, so the tests exercise the real shipping
# code with NO SDL context (see CLAUDE.md "Testing"), and the slow
# Dictionary.gen.cpp compiles only once. ONLY add SDL-free translation units
# here — anything that includes <SDL3/...> belongs in the executable below.
add_library(RetroDocCore STATIC
    src/editor/Dictionary.cpp
    src/editor/Dictionary.gen.cpp
    src/editor/DocumentSettingsStore.cpp
    src/editor/FileDocument.cpp
    src/editor/FileSettings.cpp
    src/editor/FormattedTextBuffer.cpp
    src/editor/Palette.cpp
    src/editor/RichFileDocument.cpp
    src/editor/RichUndoHistory.cpp
    src/editor/RtfReader.cpp
    src/editor/RtfWriter.cpp
    src/editor/TextBuffer.cpp
    src/editor/UndoHistory.cpp
    src/editor/WordCount.cpp
    src/platform/AppData.cpp
    src/platform/DirListing.cpp
    src/render/ScreenBuffer.cpp
    src/render/Theme.cpp
)

# PUBLIC so both the app and the test exe inherit the src/ include root simply
# by linking RetroDocCore.
target_include_directories(RetroDocCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(RetroDocCore PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

# The generated dictionary translation unit emits more than 65536 COFF
# sections in /O2 Release; compile it with /bigobj. (Applies wherever the file
# compiles — now the RetroDocCore library.)
set_source_files_properties(src/editor/Dictionary.gen.cpp
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/bigobj>"
)

# ---------------------------------------------------------------------------
# RetroDocWriter — the GUI application. main.cpp + every SDL-dependent unit;
# links the SDL-free RetroDocCore for the document model.
# WIN32 selects the Windows GUI subsystem so launching the app shows only the
# SDL window — no console window behind it. The entry point stays int main(...);
# src/main.cpp includes <SDL3/SDL_main.h>, which provides the WinMain shim.
# (WIN32 is ignored on non-Windows platforms.)
add_executable(RetroDocWriter WIN32
    src/main.cpp
    # App icon + version info (Windows only; needs the RC language enabled above).
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/RetroDocWriter.rc>
    src/app/Application.cpp
    src/platform/Beep.cpp
    src/platform/ImageDecode.cpp
    src/platform/Print.cpp
    src/platform/Window.cpp
    src/render/GlyphCache.cpp
    src/render/ImageCache.cpp
    src/render/RetroRenderer.cpp
    src/render/WysiwygRenderer.cpp
    src/ui/RetroUi.cpp
)

target_include_directories(RetroDocWriter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(RetroDocWriter PRIVATE
    RetroDocCore
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    SDL3_image::SDL3_image
    $<$<PLATFORM_ID:Windows>:Winmm>     # PlaySound, used by platform/Beep.cpp
    $<$<PLATFORM_ID:Windows>:Gdi32>     # CreateDC/TextOut/etc., used by platform/Print.cpp
    $<$<PLATFORM_ID:Windows>:Winspool>  # EnumPrinters/OpenPrinter/etc., used by platform/Print.cpp
)

target_compile_options(RetroDocWriter PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

# Copy SDL3.dll, SDL3_ttf.dll, and assets/ next to the EXE after each build.
add_custom_command(TARGET RetroDocWriter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:RetroDocWriter>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
        $<TARGET_FILE_DIR:RetroDocWriter>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3_image::SDL3_image>
        $<TARGET_FILE_DIR:RetroDocWriter>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:RetroDocWriter>/assets
    COMMENT "Copying SDL3 DLLs and assets for RetroDocWriter"
)

# ---------------------------------------------------------------------------
# Unit tests — doctest (vendored single header at tests/doctest/doctest.h).
# A console executable that links RetroDocCore only (no SDL). It runs
# automatically as a POST_BUILD step every time it is built, and FAILS THE
# BUILD if any assertion fails (doctest exits with the failed-assertion count).
# To add tests: drop a tests/test_*.cpp file and list it below.
enable_testing()
add_executable(RetroDocWriterTests
    tests/test_main.cpp
    tests/test_utf8.cpp
    tests/test_textbuffer.cpp
    tests/test_wordwrap.cpp
    tests/test_wordcount.cpp
    tests/test_formattedbuffer.cpp
    tests/test_rtf_roundtrip.cpp
    tests/test_richdocument.cpp
    tests/test_undo.cpp
    tests/test_selection.cpp
    tests/test_filesettings.cpp
    tests/test_docsettingsstore.cpp
    tests/test_dirlisting.cpp
    tests/test_headerfooter.cpp
    tests/test_palette.cpp
    tests/test_theme.cpp
    tests/test_screenbuffer.cpp
    tests/test_dictionary.cpp
)

target_link_libraries(RetroDocWriterTests PRIVATE RetroDocCore)

target_include_directories(RetroDocWriterTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests)

target_compile_options(RetroDocWriterTests PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

add_test(NAME unit COMMAND RetroDocWriterTests)

# Run the tests after they build; a non-zero exit (any failing assertion)
# fails the build — for both Debug and Release.
add_custom_command(TARGET RetroDocWriterTests POST_BUILD
    COMMAND $<TARGET_FILE:RetroDocWriterTests>
    COMMENT "Running RetroDocWriter unit tests"
)
