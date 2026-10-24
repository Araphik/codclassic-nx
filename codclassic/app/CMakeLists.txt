# codclassic - ReXGlue Recompiled Project (PC + Nintendo Switch)
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(codclassic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

set(CODCLASSIC_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(codclassic WIN32 ${CODCLASSIC_SOURCES})
else()
    add_executable(codclassic ${CODCLASSIC_SOURCES})
endif()

rexglue_setup_target(codclassic GPU_PLUGINS xenos)

# Runtime thunks for imports between game modules (x360shared.xdl exports)
target_sources(codclassic_recomp PRIVATE src/guest_imports_default.cpp src/codclassic_guards.cpp src/codclassic_perf.cpp)
if(TARGET codclassic_cgame_sp)
    target_sources(codclassic_cgame_sp PRIVATE src/guest_imports_cgame_sp.cpp)
endif()
if(TARGET codclassic_game_sp)
    target_sources(codclassic_game_sp PRIVATE src/codclassic_game_sp_hooks.cpp)
endif()
if(TARGET codclassic_ui_sp)
    target_sources(codclassic_ui_sp PRIVATE src/guest_imports_ui_sp.cpp)
endif()

# Guest DLL modules (*.xdl). On PC they are shared libraries the kernel loads
# with dlopen. Horizon has no dynamic loading, so on Switch each module is a
# static library linked into the NRO: its two entry points get a per-module
# name (they are the same in every module) and src/static_modules_switch.cpp
# publishes them in rex::platform's static library registry, where
# DynamicLibrary::Load() finds them by the module's shared_lib_name.
set(CODCLASSIC_DLL_MODULES x360shared game_sp cgame_sp ui_sp)

if(REXGLUE_PLATFORM_SWITCH)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(_dka64_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(CODCLASSIC_LD aarch64-none-elf-ld HINTS "${_dka64_bin}" NO_DEFAULT_PATH REQUIRED)
    find_program(CODCLASSIC_READELF aarch64-none-elf-readelf HINTS "${_dka64_bin}" NO_DEFAULT_PATH REQUIRED)
    foreach(mod IN LISTS CODCLASSIC_DLL_MODULES)
        set(lib codclassic_${mod})
        if(NOT TARGET ${lib})
            message(FATAL_ERROR "missing module target ${lib}: run codegen first")
        endif()
        target_compile_definitions(${lib} PRIVATE
            ReXModule_Register=ReXModule_Register__${lib}
            ReXModule_GetImageInfo=ReXModule_GetImageInfo__${lib})
        # One object per module with its internal symbols made local, so that
        # PPCImageConfig, __imp__xstart, __savegprlr_* ... of different modules
        # stay apart (see cmake/localize_module.py).
        set(obj "${CMAKE_CURRENT_BINARY_DIR}/${lib}_local.o")
        add_custom_command(
            OUTPUT "${obj}"
            COMMAND "${Python3_EXECUTABLE}" -I
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/localize_module.py"
                "${CODCLASSIC_LD}" "${CMAKE_OBJCOPY}" "${CODCLASSIC_READELF}"
                "$<TARGET_FILE:${lib}>" "${obj}"
            DEPENDS ${lib} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/localize_module.py"
            COMMENT "Localizing internal symbols of ${lib}"
            VERBATIM)
        set_source_files_properties("${obj}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_sources(codclassic PRIVATE "${obj}")
    endforeach()
    target_sources(codclassic PRIVATE src/static_modules_switch.cpp src/codclassic_savedata_switch.cpp)
    # codclassic_guards.cpp asks the Switch guest-memory layer what the exception handler would do.
    target_include_directories(codclassic_recomp PRIVATE "${REXSDK_DIR}/src/core")

    # Many object files and a 2-core/7 GB build machine: no LTO for now.
    option(CODCLASSIC_RECOMP_O2 "Recompiled code with -O2 (smaller than -O3)" ON)
    if(CODCLASSIC_RECOMP_O2)
        foreach(t codclassic_recomp ${CODCLASSIC_DLL_MODULES})
            if(TARGET ${t})
                target_compile_options(${t} PRIVATE -O2)
            elseif(TARGET codclassic_${t})
                target_compile_options(codclassic_${t} PRIVATE -O2)
            endif()
        endforeach()
    endif()

    set(CODCLASSIC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")
    set(CODCLASSIC_ICON_ARG)
    if(EXISTS "${CODCLASSIC_ICON}")
        set(CODCLASSIC_ICON_ARG ICON "${CODCLASSIC_ICON}")
    endif()
    rexglue_switch_add_nro(codclassic
        NAME "Call of Duty Classic"
        AUTHOR "ReXGlue port"
        VERSION "0.1.0"
        SAVE_DATA_SIZE 0x4000000          # 64 MB account save data for installed builds
        SAVE_DATA_JOURNAL_SIZE 0x2000000  # (codclassic_savedata_switch.cpp)
        ${CODCLASSIC_ICON_ARG})
endif()
