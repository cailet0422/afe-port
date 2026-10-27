cmake_minimum_required(VERSION 3.16)
project(afe_pc C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Do NOT set a default build type. Optimization exposes undefined behavior in
# the decompiled code (see ACGC-PC-Port's notes); GCC's default -O0 is used.
# NDEBUG is defined below so decomp asserts stay disabled.

option(PC_CONSOLE "Show console window (disables -mwindows, useful for debugging)" OFF)
option(PC_BUILD_TESTS "Build unit tests for the PC port layer" ON)

set(AFE_VERSION "GAEJ01_01" CACHE STRING "Game version to build (GAEJ01_00 or GAEJ01_01)")
if(AFE_VERSION STREQUAL "GAEJ01_00")
    set(AFE_VERSION_NUM 0)
elseif(AFE_VERSION STREQUAL "GAEJ01_01")
    set(AFE_VERSION_NUM 1)
else()
    message(FATAL_ERROR "Unknown AFE_VERSION '${AFE_VERSION}'")
endif()

set(DECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(DECOMP_BUILD_DIR "${DECOMP_ROOT}/build/${AFE_VERSION}")

# =============================================================================
# Unit tests (host architecture, no game code)
# =============================================================================
if(PC_BUILD_TESTS)
    enable_testing()
    add_executable(test_ko_ime
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ko/ko_ime.c
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ko_ime.c
    )
    target_include_directories(test_ko_ime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(test_ko_ime PRIVATE -Wall -Wextra)
    add_test(NAME ko_ime COMMAND test_ko_ime)

    add_executable(test_ko_charset
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ko/ko_charset.c
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ko_charset.c
    )
    target_include_directories(test_ko_charset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(test_ko_charset PRIVATE -Wall -Wextra)
    add_test(NAME ko_charset COMMAND test_ko_charset)
endif()

# =============================================================================
# 32-bit enforcement (the decomp casts pointers to u32 throughout)
# =============================================================================
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "The PC port MUST be built as 32-bit (pointer-to-u32 casts in the decomp).\n"
        "  Windows/MSYS2: Use the MINGW32 shell (not MINGW64)\n"
        "  Linux: cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/Toolchain-linux32.cmake ..")
endif()

# =============================================================================
# Extracted game assets
# =============================================================================
# Data sources #include the assets dtk extracts from the user's disc image.
# They are produced by the matching build's extraction step:
#     python configure.py --version GAEJ01_01 && ninja
if(NOT EXISTS "${DECOMP_BUILD_DIR}/include/assets")
    message(FATAL_ERROR
        "Extracted assets not found in ${DECOMP_BUILD_DIR}/include/assets.\n"
        "Place your disc files in orig/${AFE_VERSION}/ and run, from the repository root:\n"
        "    python configure.py --version ${AFE_VERSION}\n"
        "    ninja\n")
endif()

# Allow mixed C/C++ linkage
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_EXTENSIONS ON)

# =============================================================================
# Compiler definitions
# =============================================================================
add_compile_definitions(
    TARGET_PC
    VERSION=${AFE_VERSION_NUM}
    F3DEX_GBI_2
    NDEBUG
    BUGFIXES          # Original out-of-bounds bugs that corrupt host memory (e.g. m_collision_bg_column)
    _LANGUAGE_C       # Required for N64 GBI/ultratypes headers
    PC_ENHANCEMENTS   # Optional visual improvements (MSAA, etc). Remove to get pure PC port.
)

add_compile_options(
    -fno-strict-aliasing
    -fwrapv
    -malign-double    # PowerPC aligns double/long long to 8 bytes inside structs
    -g
)

# =============================================================================
# Include paths
# =============================================================================
include_directories(
    ${DECOMP_ROOT}/include
    # NOTE: Do NOT add ${DECOMP_ROOT}/include/libc here; it breaks the
    # #include_next chains in the decomp's shadow libc headers.
    ${DECOMP_ROOT}/src
    ${DECOMP_ROOT}
    ${DECOMP_BUILD_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/fixnes
)
# The matching build also searches include/dolphin ("os.h", "os/OSCache.h", ...).
# Search it last so it never shadows system headers like <string.h>.
add_compile_options("SHELL:-idirafter ${DECOMP_ROOT}/include/dolphin")

# =============================================================================
# SDL2
# =============================================================================
if(WIN32)
    set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/SDL2/cmake" CACHE PATH "SDL2 cmake dir")
endif()
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
endif()
if(NOT SDL2_FOUND)
    message(FATAL_ERROR
        "SDL2 not found!\n"
        "  Linux:         sudo apt install libsdl2-dev:i386  (Debian/Ubuntu)\n"
        "  Windows/MSYS2: pacman -S mingw-w64-i686-SDL2")
endif()
include_directories(${SDL2_INCLUDE_DIRS})

# =============================================================================
# GLAD (OpenGL 3.3 loader - generated by glad2)
# =============================================================================
add_library(glad STATIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/glad/src/gl.c)
target_include_directories(glad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib/glad/include)

# =============================================================================
# Game sources (same object list as the matching build, see gen_sources.py)
# =============================================================================
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DECOMP_SOURCES_CMAKE "${CMAKE_BINARY_DIR}/decomp_sources.cmake")
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_sources.py"
            ${AFE_VERSION} "${DECOMP_SOURCES_CMAKE}"
    WORKING_DIRECTORY "${DECOMP_ROOT}"
    RESULT_VARIABLE GEN_SOURCES_RESULT
)
if(NOT GEN_SOURCES_RESULT EQUAL 0)
    message(FATAL_ERROR "gen_sources.py failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${DECOMP_ROOT}/configure.py" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_sources.py")
include("${DECOMP_SOURCES_CMAKE}")

# .c files the matching build compiles with -lang=c++
set_source_files_properties(${DECOMP_CXX_SOURCES} PROPERTIES LANGUAGE CXX)

# The indoor/outdoor data modules (foresti/foresto) have no C sources yet; the
# matching build links them from dtk's assembly. Convert them with the decomp's
# batch_assetdis tool into the build directory (slow, so only when missing).
set(MODULE_DATA_DIR "${CMAKE_BINARY_DIR}/gen")
if(NOT EXISTS "${MODULE_DATA_DIR}/module_data.cmake")
    message(STATUS "Generating C sources for foresti/foresto data (takes a few minutes)")
    execute_process(
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_module_data.py"
                ${AFE_VERSION} "${MODULE_DATA_DIR}"
        WORKING_DIRECTORY "${DECOMP_ROOT}"
        RESULT_VARIABLE GEN_DATA_RESULT
    )
    if(NOT GEN_DATA_RESULT EQUAL 0)
        file(REMOVE "${MODULE_DATA_DIR}/module_data.cmake")
        message(FATAL_ERROR "gen_module_data.py failed")
    endif()
endif()
include("${MODULE_DATA_DIR}/module_data.cmake")
include_directories(BEFORE "${MODULE_DATA_DIR}/include")
list(APPEND DECOMP_C_SOURCES ${GENERATED_DATA_SOURCES})

# Raw big-endian blobs that game code reads as typed data, swapped at boot.
set(DATA_FIXUPS_C "${MODULE_DATA_DIR}/pc_data_fixups.c")
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_data_fixups.py"
            ${AFE_VERSION} "${DATA_FIXUPS_C}" "${DECOMP_SOURCES_CMAKE}" "${MODULE_DATA_DIR}/module_data.cmake"
    WORKING_DIRECTORY "${DECOMP_ROOT}"
    RESULT_VARIABLE GEN_FIXUPS_RESULT
)
if(NOT GEN_FIXUPS_RESULT EQUAL 0)
    message(FATAL_ERROR "gen_data_fixups.py failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_data_fixups.py")

# The system font is linked from an .s file with .incbin on GameCube; embed it as a C array.
set(SYSTEM_FONT_BFN "${DECOMP_BUILD_DIR}/bin/assets/JSystem/JUtility/FontData/Ascfont_fix12.bfn")
set(SYSTEM_FONT_C "${CMAKE_BINARY_DIR}/gen/pc_system_font.c")
add_custom_command(
    OUTPUT "${SYSTEM_FONT_C}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2c.py"
            "${SYSTEM_FONT_BFN}" "${SYSTEM_FONT_C}" JUTResFONT_Ascfont_fix12 32
    DEPENDS "${SYSTEM_FONT_BFN}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/bin2c.py"
    COMMENT "Embedding system font"
)

# =============================================================================
# PC port source files
# =============================================================================
set(PC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_boot_fixups.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_sdk_extra.c
    ${DATA_FIXUPS_C}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_gx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_gx_tev.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_gx_texture.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_os.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_dvd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_pad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_typing.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_input_script.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_ko.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_ko_editor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_rel_overlay.c
    ${DECOMP_ROOT}/src/static/GAEJ01_01/dolphin/pad/Padclamp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_card.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_vi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_aram.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_misc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_mtx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_texture_pack.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_pause_menu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_settings_menu.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_text_draw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_keybindings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_nes_fixnes.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_stubs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_stubs_cpp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pc_disc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ko/ko_ime.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ko/ko_charset.c
    ${SYSTEM_FONT_C}
)

# =============================================================================
# fixNES — vendored NES emulator (MIT License, by FIX94)
# =============================================================================
set(FIXNES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/fixnes)
file(GLOB FIXNES_MAPPER_SOURCES ${FIXNES_DIR}/mapper/*.c)
set(FIXNES_SOURCES
    ${FIXNES_DIR}/cpu.c
    ${FIXNES_DIR}/ppu.c
    ${FIXNES_DIR}/apu.c
    ${FIXNES_DIR}/mem.c
    ${FIXNES_DIR}/input.c
    ${FIXNES_DIR}/mapper.c
    ${FIXNES_DIR}/mapperList.c
    ${FIXNES_DIR}/vrc_irq.c
    ${FIXNES_DIR}/audio.c
    ${FIXNES_DIR}/fm2play.c
    ${FIXNES_DIR}/audio_fds.c
    ${FIXNES_DIR}/audio_mmc5.c
    ${FIXNES_DIR}/audio_vrc6.c
    ${FIXNES_DIR}/audio_vrc7.c
    ${FIXNES_DIR}/audio_n163.c
    ${FIXNES_DIR}/audio_s5b.c
    ${FIXNES_MAPPER_SOURCES}
)
set_source_files_properties(${FIXNES_SOURCES}
    PROPERTIES
    COMPILE_OPTIONS "-w;-O2"
    COMPILE_DEFINITIONS "COL_TEX_BSWAP;COL_GL_BSWAP;DO_INLINE_ATTRIBS=1"
)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/pc_nes_fixnes.c
    PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-O2;-Wno-unused-parameter"
)

# Rename the game's main() functions to avoid clashing with the PC main()
set_source_files_properties("${DECOMP_ROOT}/src/main.c" PROPERTIES COMPILE_DEFINITIONS "main=ac_entry")
set_source_files_properties("${DECOMP_ROOT}/src/static/boot.c" PROPERTIES COMPILE_DEFINITIONS "main=boot_main")

# Some TUs keep functions that reference static helpers only another TU
# defines (code MWCC drops as unused). Let the linker discard unused sections
# so those references never need resolving.
add_compile_options(-ffunction-sections -fdata-sections)

# MinGW lays out bit-fields the MSVC way by default; the game's structs (and
# the data that fills them) assume the GCC/SysV layout used everywhere else.
if(MINGW)
    add_compile_options(-mno-ms-bitfields)
endif()

# Decomp C files use nullptr; define it as NULL for C compilation.
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Dnullptr=NULL>)

# =============================================================================
# Per-source warning settings
# =============================================================================
set(DECOMP_WARN_FLAGS "-w;-fpermissive")
set_source_files_properties(${DECOMP_C_SOURCES} ${DECOMP_CXX_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${DECOMP_WARN_FLAGS}"
)
set(PC_C_SOURCES ${PC_SOURCES})
list(FILTER PC_C_SOURCES EXCLUDE REGEX "\\.cpp$")
set(PC_CXX_SOURCES ${PC_SOURCES})
list(FILTER PC_CXX_SOURCES INCLUDE REGEX "\\.cpp$")
set(PC_WARN_SUPPRESS "-Wno-unused-parameter;-Wno-sign-compare;-Wno-comment;-Wno-unused-function;-Wno-builtin-declaration-mismatch")
set_source_files_properties(${PC_C_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;${PC_WARN_SUPPRESS}"
)
set_source_files_properties(${PC_CXX_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;${PC_WARN_SUPPRESS};-fpermissive"
)

# Padclamp.c defines a plain `inline` helper; MWCC emits it like GNU89 does.
set_property(SOURCE "${DECOMP_ROOT}/src/static/GAEJ01_01/dolphin/pad/Padclamp.c"
    APPEND PROPERTY COMPILE_OPTIONS "-fgnu89-inline")

if(WIN32)
    set(RC_FILE "${CMAKE_CURRENT_SOURCE_DIR}/res/app.rc")
endif()

# =============================================================================
# Executable
# =============================================================================
add_executable(afe_pc
    ${PC_SOURCES}
    ${FIXNES_SOURCES}
    ${DECOMP_C_SOURCES}
    ${DECOMP_CXX_SOURCES}
    ${RC_FILE}
)

if(TARGET SDL2::SDL2)
    target_link_libraries(afe_pc glad SDL2::SDL2)
else()
    list(FILTER SDL2_LIBRARIES EXCLUDE REGEX "SDL2main")
    target_link_libraries(afe_pc glad ${SDL2_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(afe_pc opengl32 winmm imm32 version setupapi)
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(afe_pc OpenGL::GL dl pthread m)
endif()

set_target_properties(afe_pc PROPERTIES
    OUTPUT_NAME "DobutsuNoMoriEPlus"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET afe_pc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/default.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/default.frag"
        "${CMAKE_BINARY_DIR}/bin/shaders/"
    COMMENT "Copying shader files to bin/shaders/"
)

if(WIN32)
    # MSYS2 keeps it in $MINGW_PREFIX/bin; the SDL2 MinGW devel package (for
    # cross builds) next to its lib/cmake/SDL2
    find_file(SDL2_DLL_PATH SDL2.dll PATHS "$ENV{MINGW_PREFIX}/bin" "${SDL2_DIR}/../../../bin" NO_CMAKE_FIND_ROOT_PATH)
    if(SDL2_DLL_PATH)
        add_custom_command(TARGET afe_pc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DLL_PATH}" "${CMAKE_BINARY_DIR}/bin/"
            COMMENT "Copying SDL2.dll to bin/"
        )
    endif()
endif()

target_link_options(afe_pc PRIVATE -Wl,--gc-sections "-Wl,-Map=${CMAKE_BINARY_DIR}/DobutsuNoMoriEPlus.map")
# Table that lets the game load REL data from the disc it runs from (pc_rel_overlay.c)
add_custom_command(TARGET afe_pc POST_BUILD
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_rel_overlay.py" ${AFE_VERSION}
            "${CMAKE_BINARY_DIR}/DobutsuNoMoriEPlus.map" -o "${CMAKE_BINARY_DIR}/bin/DobutsuNoMoriEPlus.relmap"
    COMMENT "Generating REL data overlay table"
)
if(NOT WIN32)
    target_link_options(afe_pc PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/exe_exports.ver")
endif()

if(MINGW)
    target_link_options(afe_pc PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()

if(WIN32 AND NOT PC_CONSOLE)
    target_link_options(afe_pc PRIVATE -mwindows)
endif()
