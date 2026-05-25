cmake_minimum_required(VERSION 3.24)
project(RetroDocWriter VERSION 0.1.0 LANGUAGES CXX)

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

# Single-product layout: every .cpp lives under src/ with subdirectories
# editor/, platform/, render/, ui/, app/. The src/ directory is the include
# root so existing #include "editor/CharStyle.h" style paths resolve.
add_executable(RetroDocWriter
    src/main.cpp
    src/app/Application.cpp
    src/editor/Dictionary.cpp
    src/editor/Dictionary.gen.cpp
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
    src/platform/Beep.cpp
    src/platform/Print.cpp
    src/platform/Window.cpp
    src/render/GlyphCache.cpp
    src/render/RetroRenderer.cpp
    src/render/ScreenBuffer.cpp
    src/render/Theme.cpp
    src/render/WysiwygRenderer.cpp
    src/ui/RetroUi.cpp
)

target_include_directories(RetroDocWriter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(RetroDocWriter PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    $<$<PLATFORM_ID:Windows>:Winmm>     # PlaySound, used by platform/Beep.cpp
    $<$<PLATFORM_ID:Windows>:Gdi32>     # CreateDC/TextOut/etc., used by platform/Print.cpp
    $<$<PLATFORM_ID:Windows>:Winspool>  # EnumPrinters/OpenPrinter/etc., used by platform/Print.cpp
)

target_compile_options(RetroDocWriter PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
)

# The generated dictionary translation unit emits more than 65536 COFF
# sections in /O2 Release; compile it with /bigobj.
set_source_files_properties(src/editor/Dictionary.gen.cpp
    PROPERTIES COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/bigobj>"
)

# Copy SDL3.dll, SDL3_ttf.dll, and assets/ next to the EXE after each build.
add_custom_command(TARGET RetroDocWriter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3::SDL3>
        $<TARGET_FILE_DIR:RetroDocWriter>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL3_ttf::SDL3_ttf>
        $<TARGET_FILE_DIR:RetroDocWriter>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:RetroDocWriter>/assets
    COMMENT "Copying SDL3 DLLs and assets for RetroDocWriter"
)
