cmake_minimum_required(VERSION 3.31)
project(raytiles VERSION 0.17.0) # x-release-please-version

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Mode switch for Python/C# bindings ---
option(RAYTILES_BINDINGS_MODE "Build shared library for Python/C# (defers raylib linking)" OFF)

if (RAYTILES_BINDINGS_MODE)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
endif ()

include(FetchContent)

if (NOT TARGET raylib)
    if (PROJECT_IS_TOP_LEVEL)
        # set(SUPPORT_FILEFORMAT_JPG ON CACHE BOOL "" FORCE)
        add_compile_definitions(SUPPORT_FILEFORMAT_JPG)
        FetchContent_Declare(
                raylib
                GIT_REPOSITORY https://github.com/raysan5/raylib.git
                GIT_TAG 6.0
                GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(raylib)
        if (TARGET raylib)
            set_target_properties(raylib PROPERTIES SYSTEM TRUE)
        endif ()
    else ()
        find_package(raylib 6.0 REQUIRED)
    endif ()
endif ()

add_library(${PROJECT_NAME}
        include/raytiles/raytiles.h
        include/raytiles/craytiles.h
        src/detail/raii.hpp
        src/detail/lod.hpp
        src/detail/terrain_synth.hpp
        src/detail/tile.hpp
        src/detail/utils.hpp
        src/raytiles.cpp
        src/craytiles.cpp
        src/terrain_renderer.cpp
        src/detail/terrain_renderer.h
        src/detail/tile_source.h
        src/tile_source.cpp
        src/tile_shader.cpp
        src/detail/tile_shader.h
        src/tile_store.cpp
        src/detail/tile_store.h
        src/detail/sky_shader.h
        src/sky_shader.cpp
        src/detail/sky_renderer.h
        src/sky_renderer.cpp
        include/raytiles/rayskies.h
        src/rayskies.cpp
        include/raytiles/crayskies.h
        src/crayskies.cpp
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
#target_link_libraries(${PROJECT_NAME} PUBLIC raylib)

if (RAYTILES_BINDINGS_MODE)
    # in bindings mode, we don't link raylib at all! We just need its headers for compilation.
    # target_include_directories(${PROJECT_NAME} PRIVATE ${raylib_SOURCE_DIR}/src)
    target_include_directories(${PROJECT_NAME} PRIVATE $<TARGET_PROPERTY:raylib,INTERFACE_INCLUDE_DIRECTORIES>)

    # tell the linker to allow undefined symbols (i.e. raylib functions) since we won't be linking
    # raylib directly in this mode. The actual binding code will load raylib dynamically at runtime.
    if (APPLE)
        set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-undefined dynamic_lookup")
    elseif (UNIX AND NOT APPLE) # Linux
        target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--allow-shlib-undefined")
    elseif (MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE "/FORCE:UNRESOLVED")
    endif ()
else ()
    # for typical C/C++ usage, we link raylib directly
    target_link_libraries(${PROJECT_NAME} PUBLIC raylib)
endif ()


if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else ()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif ()

if (NOT TARGET httplib)
    FetchContent_Declare(
            httplib
            GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
            GIT_TAG v0.18.5
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(httplib)
endif ()

target_link_libraries(${PROJECT_NAME} PRIVATE httplib)

if (APPLE)
    find_library(SECURITY_FRAMEWORK Security)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${SECURITY_FRAMEWORK})
endif ()

find_package(OpenSSL REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(${PROJECT_NAME} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)


if (NOT RAYTILES_BINDINGS_MODE)

    # sandbox example application
    add_executable(Sandbox sandbox/main.cpp)
    target_link_libraries(Sandbox PRIVATE ${PROJECT_NAME})

    # demos creator
    add_executable(demo sandbox/demo.cpp)
    target_link_libraries(demo PRIVATE ${PROJECT_NAME})

    # quick start
    add_executable(quick sandbox/quick_start.cpp)
    target_link_libraries(quick PRIVATE ${PROJECT_NAME})

    # perf baseline harness: default-config streamer, logs initial-load wall-time
    add_executable(baseline sandbox/base_line.cpp)
    target_link_libraries(baseline PRIVATE ${PROJECT_NAME})
    target_link_libraries(baseline PRIVATE raylib)

    # C demo: exercises the C wrapper (craytiles.h)
    add_executable(demo_c sandbox/demo_c.c)
    set_target_properties(demo_c PROPERTIES
            C_STANDARD 11
            C_STANDARD_REQUIRED ON
    )
    target_link_libraries(demo_c PRIVATE ${PROJECT_NAME})

    # tests
    FetchContent_Declare(
            doctest
            GIT_REPOSITORY https://github.com/doctest/doctest
            GIT_TAG v2.5.2
    )
    FetchContent_MakeAvailable(doctest)
    add_executable(run_tests
            tests/tests_main.cpp
            tests/lod_tests.cpp
            tests/utils_tests.cpp
            tests/terrain_synth_tests.cpp
            tests/tile_source_tests.cpp
    )

    target_include_directories(run_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    # tile_source tests drive the real worker pool against a local httplib
    # server, so the tests link the library (raylib arrives transitively —
    # everything stays windowless: CPU-side raylib calls only). The OpenSSL
    # define must match the library's httplib configuration to avoid ODR
    # mismatches between the two httplib TUs.
    target_compile_definitions(run_tests PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(run_tests PRIVATE ${PROJECT_NAME} doctest::doctest httplib OpenSSL::SSL OpenSSL::Crypto)

    include(CTest)
    add_test(NAME AllTests COMMAND run_tests)
endif ()

# install rules — only emit when raytiles is the top-level project, so
# consumers using add_subdirectory / FetchContent don't accidentally install us.
if (PROJECT_IS_TOP_LEVEL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(TARGETS ${PROJECT_NAME}
            EXPORT raytilesTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/raytiles
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
    )

    install(EXPORT raytilesTargets
            FILE raytilesTargets.cmake
            NAMESPACE raytiles::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/raytiles
    )

    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/raytilesConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
    )

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/raytilesConfig.cmake"
            "include(CMakeFindDependencyMacro)\n"
            "find_dependency(raylib)\n"
            "include(\"\${CMAKE_CURRENT_LIST_DIR}/raytilesTargets.cmake\")\n"
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/raytilesConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/raytilesConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/raytiles
    )
endif ()
