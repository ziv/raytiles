cmake_minimum_required(VERSION 3.31)
project(raytiles VERSION 0.8.6) # x-release-please-version

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if (NOT TARGET raylib)
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
endif ()

add_library(${PROJECT_NAME}
        include/raytiles/raytiles.h
        include/raytiles/craytiles.h
        include/raytiles/detail/raii.hpp
        include/raytiles/detail/tile.hpp
        include/raytiles/detail/utils.hpp
        src/raytiles.cpp
        src/craytiles.cpp
        src/renderer.cpp
        src/downloader.hpp
        src/shaders.hpp
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(${PROJECT_NAME} PUBLIC raylib)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (EMSCRIPTEN)
    target_compile_options(${PROJECT_NAME} PUBLIC "-pthread" "-fexceptions")
    target_link_options(${PROJECT_NAME} PUBLIC
            "-sUSE_GLFW=3"
            "-sFETCH=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-pthread"
            "-sPTHREAD_POOL_SIZE=4"
            "--llvm-lto 1"
            "-fexceptions"
            "-sMIN_WEBGL_VERSION=2"
            "-sMAX_WEBGL_VERSION=2"
            "-sGL_ASSERTIONS=0"
            "SHELL:-lidbfs.js"
    )
else ()
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
endif ()



# sandbox example application
add_executable(Sandbox sandbox/main.cpp)
target_link_libraries(Sandbox PRIVATE ${PROJECT_NAME})

if (EMSCRIPTEN)
    set_target_properties(Sandbox PROPERTIES SUFFIX ".html")
endif ()

# demos creator
add_executable(demo sandbox/demo.cpp)
target_link_libraries(demo PRIVATE ${PROJECT_NAME})

# tests
FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest
        GIT_TAG v2.5.2
)
FetchContent_MakeAvailable(doctest)
add_executable(run_tests
        tests/tests_main.cpp
        tests/example_tests.cpp
)

target_link_libraries(run_tests PRIVATE doctest::doctest)

include(CTest)
add_test(NAME AllTests COMMAND run_tests)

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