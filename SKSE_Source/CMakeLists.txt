cmake_minimum_required(VERSION 3.21)

project(MORWHA VERSION 0.1.1 LANGUAGES CXX)

set(MOD_FOLDER_NAME "MORWHA Dev")
set(SKYRIM_MODS_FOLDER "E:\\Skyrim\\development\\mods")
set(OUTPUT_FOLDER "${SKYRIM_MODS_FOLDER}/${MOD_FOLDER_NAME}")

# ---------------------------------------------------- #
#                     CommonLib                        #
# ---------------------------------------------------- #

if(NOT DEFINED ENV{COMMONLIB_SSE_FOLDER})
    message(FATAL_ERROR "Missing COMMONLIB_SSE_FOLDER environment variable")
endif()

set(CommonLibPath "$ENV{COMMONLIB_SSE_FOLDER}")
set(CommonLibName "CommonLibSSE")

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

find_path(RAPIDCSV_INCLUDE_DIRS
    NAMES rapidcsv.h
    PATH_SUFFIXES rapidcsv
    REQUIRED
)

add_subdirectory("${CommonLibPath}" ${CMAKE_CURRENT_BINARY_DIR}/_deps/clib-build EXCLUDE_FROM_ALL)

find_package(nlohmann_json CONFIG REQUIRED)

# ---------------------------------------------------- #
#   Layers 0-1: Pure Engine-Free STATIC Libraries      #
#   Precompiled Header: pch_core.h                     #
# ---------------------------------------------------- #

add_library(morwha_foundation STATIC
    modules/foundation/src/Logging.cpp
)
target_compile_features(morwha_foundation PUBLIC cxx_std_23)
target_compile_definitions(morwha_foundation PUBLIC _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING)
if(MSVC)
    target_compile_options(morwha_foundation PUBLIC /FS)
endif()
target_include_directories(morwha_foundation
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/foundation/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/foundation/src
)
target_precompile_headers(morwha_foundation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch_core.h
)

add_library(morwha_state STATIC
    modules/state/src/SessionEpoch.cpp
    modules/state/src/CoSaveCodec.cpp
    modules/state/src/DynamicNpcRegistry.cpp
    modules/state/src/MortalRequestJson.cpp
    modules/state/src/RegistryGarbageCollector.cpp
    modules/state/src/ChimeraAccessoryManifest.cpp
    modules/state/src/MorwhaIniConfig.cpp
    modules/state/src/JsonDom.cpp
    modules/state/src/FaceAnchorPreset.cpp
    modules/state/src/DiagnosticZipWriter.cpp
)
target_compile_features(morwha_state PUBLIC cxx_std_23)
target_include_directories(morwha_state
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/state/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/state/src
)
target_link_libraries(morwha_state PUBLIC morwha_foundation)
target_precompile_headers(morwha_state REUSE_FROM morwha_foundation)

# ---------------------------------------------------- #
#   Layer 2: Dual-Mode MORWHA-Core Integration         #
#   (Private Source Dir vs Prebuilt Static Libraries)  #
# ---------------------------------------------------- #

set(MORWHA_CORE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../MORWHA-Core" CACHE PATH "Optional path to private MORWHA-Core repository")
option(MORWHA_FORCE_PREBUILT_CORE "Force linking prebuilt external/morwha_core static libraries even when MORWHA-Core exists" OFF)

if(NOT MORWHA_FORCE_PREBUILT_CORE AND EXISTS "${MORWHA_CORE_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "MORWHA-Core: Building from private source at '${MORWHA_CORE_SOURCE_DIR}'")
    add_subdirectory("${MORWHA_CORE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/morwha-core-build")
else()
    set(_morwha_prebuilt_core "${CMAKE_CURRENT_SOURCE_DIR}/external/morwha_core/lib/Release/morwha_core.lib")
    if(NOT EXISTS "${_morwha_prebuilt_core}")
        message(FATAL_ERROR "MORWHA-Core source not found at '${MORWHA_CORE_SOURCE_DIR}' and prebuilt static library missing at '${_morwha_prebuilt_core}'.")
    endif()
    message(STATUS "MORWHA-Core: Linking prebuilt static library 'external/morwha_core/lib/Release/morwha_core.lib'")

    add_library(morwha_core STATIC IMPORTED GLOBAL)
    set_target_properties(morwha_core PROPERTIES
        IMPORTED_LOCATION "${_morwha_prebuilt_core}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/external/morwha_core/include"
        INTERFACE_LINK_LIBRARIES "morwha_foundation"
    )
endif()

# ---------------------------------------------------- #
#   Layers 3-5: Engine / App / Bridge OBJECT Modules   #
#   Precompiled Header: pch_engine.h                   #
# ---------------------------------------------------- #

set(_morwha_pch_anchor "${CMAKE_CURRENT_BINARY_DIR}/morwha_pch_engine_anchor.cpp")
if(NOT EXISTS "${_morwha_pch_anchor}")
    file(WRITE "${_morwha_pch_anchor}" "// Anchor translation unit for CMake pch_engine.pch generation.\n")
endif()
add_library(morwha_pch_engine STATIC
    "${_morwha_pch_anchor}"
)
target_compile_features(morwha_pch_engine PUBLIC cxx_std_23)
target_link_libraries(morwha_pch_engine PUBLIC morwha_foundation ${CommonLibName}::${CommonLibName})
target_precompile_headers(morwha_pch_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pch_engine.h
)

add_library(morwha_engine OBJECT
    modules/engine/src/AgeProfileResolver.cpp
    modules/engine/src/BeastTintSynthesizer.cpp
    modules/engine/src/ChimeraAccessoryCatalog.cpp
    modules/engine/src/ChimeraEngineSupport.cpp
    modules/engine/src/ChimeraMeshTinter.cpp
    modules/engine/src/ChimeraSpecBuilder.cpp
    modules/engine/src/DiagnosticBundleBuilder.cpp
    modules/engine/src/DonorPhenotypeExtractor.cpp
    modules/engine/src/EngineLogging.cpp
    modules/engine/src/DpfClient.cpp
    modules/engine/src/FaceAnchorCatalog.cpp
    modules/engine/src/FormRefResolver.cpp
    modules/engine/src/GeneticSpecBuilder.cpp
    modules/engine/src/HeadPartPoolIndexer.cpp
    modules/engine/src/HybridSkinService.cpp
    modules/engine/src/LiveActorMeshExtractor.cpp
    modules/engine/src/MortalHeritageKeywordService.cpp
    modules/engine/src/Mortal3dSyncService.cpp
    modules/engine/src/MortalSpawner.cpp
    modules/engine/src/SkeeOverlayClient.cpp
    modules/engine/src/SkseSerializationCallbacks.cpp
    modules/engine/src/TesNpcHydrator.cpp
    modules/engine/src/TriProjectorCache.cpp
    modules/engine/src/VfsAssetReader.cpp
)
target_compile_features(morwha_engine PUBLIC cxx_std_23)
target_include_directories(morwha_engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/engine/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/engine/src
            ${CMAKE_CURRENT_SOURCE_DIR}/external/dpf
)
target_link_libraries(morwha_engine
    PUBLIC
    morwha_core
    morwha_state
    morwha_foundation
    ${CommonLibName}::${CommonLibName}
    nlohmann_json::nlohmann_json
)
target_precompile_headers(morwha_engine REUSE_FROM morwha_pch_engine)

add_library(morwha_app OBJECT
    modules/app/src/MortalOverridesApplier.cpp
    modules/app/src/SynthesisCoordinator.cpp
    modules/app/src/SynthesisRegistryMaintenance.cpp
)
target_compile_features(morwha_app PUBLIC cxx_std_23)
target_include_directories(morwha_app
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/app/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/app/src
)
target_link_libraries(morwha_app PUBLIC morwha_engine)
target_precompile_headers(morwha_app REUSE_FROM morwha_pch_engine)

add_library(morwha_bridge_papyrus OBJECT
    modules/bridge_papyrus/src/ConsoleCommands.cpp
    modules/bridge_papyrus/src/ConsoleRacePairSpawner.cpp
    modules/bridge_papyrus/src/PapyrusInterface.cpp
)
target_compile_features(morwha_bridge_papyrus PUBLIC cxx_std_23)
target_include_directories(morwha_bridge_papyrus
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_papyrus/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_papyrus/src
)
target_link_libraries(morwha_bridge_papyrus PRIVATE morwha_app morwha_engine)
target_precompile_headers(morwha_bridge_papyrus REUSE_FROM morwha_pch_engine)

add_library(morwha_bridge_c_api OBJECT
    modules/bridge_c_api/src/MORWHA_PublicAPI.cpp
)
target_compile_features(morwha_bridge_c_api PUBLIC cxx_std_23)
target_include_directories(morwha_bridge_c_api
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_c_api/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules/bridge_c_api/src
)
target_link_libraries(morwha_bridge_c_api PRIVATE morwha_app morwha_engine)
target_precompile_headers(morwha_bridge_c_api REUSE_FROM morwha_pch_engine)

# ---------------------------------------------------- #
#   Composition Root: MORWHA.dll                       #
# ---------------------------------------------------- #

add_library(${PROJECT_NAME} SHARED
    plugin/plugin.cpp
    version.rc
)
set_target_properties(${PROJECT_NAME} PROPERTIES DEFINE_SYMBOL "")
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} REUSE_FROM morwha_pch_engine)
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    morwha_bridge_papyrus
    morwha_bridge_c_api
    morwha_app
    morwha_engine
)

if(MSVC)
    foreach(_tgt IN ITEMS
        morwha_foundation
        morwha_state
        morwha_pch_engine
        morwha_engine
        morwha_app
        morwha_bridge_papyrus
        morwha_bridge_c_api
        ${PROJECT_NAME}
    )
        set_property(TARGET ${_tgt} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endforeach()

    if(TARGET ${CommonLibName})
        set_property(TARGET ${CommonLibName} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    target_link_options(${PROJECT_NAME} PRIVATE
        /INCREMENTAL:NO
        /OPT:REF
        /OPT:ICF
        "/PDBALTPATH:%_PDB%"
    )
endif()

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()

# ---------------------------------------------------- #
#   Headless State & Parser Unit Tests                 #
# ---------------------------------------------------- #

option(MORWHA_BUILD_TESTS "Build MORWHA headless state & parser unit tests" ON)
if(MORWHA_BUILD_TESTS)
    enable_testing()
    add_executable(morwha_tests
        tests/main.cpp
        tests/test_cosave_codec.cpp
        tests/test_dynamic_npc_registry.cpp
        tests/test_registry_gc.cpp
        tests/test_morwha_ini_config.cpp
        tests/test_state_parsers.cpp
        tests/test_archetype_and_request_json.cpp
        tests/test_diagnostic_zip_writer.cpp
    )
    target_compile_features(morwha_tests PRIVATE cxx_std_23)
    target_include_directories(morwha_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_precompile_headers(morwha_tests REUSE_FROM morwha_foundation)
    # Physical LNK2019 link gate: morwha_tests links ONLY pure STATIC state/foundation targets
    target_link_libraries(morwha_tests PRIVATE
        morwha_state
        morwha_foundation
    )
    if(MSVC)
        set_property(TARGET morwha_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME morwha_headless_tests COMMAND morwha_tests)
endif()
