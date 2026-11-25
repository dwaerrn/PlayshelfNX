# Core library, command line tool and tests (PC). The Borealis app is built from app/ (see README).
cmake_minimum_required(VERSION 3.10)
project(LibraryCore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (PLATFORM_SWITCH OR USE_DEKO3D)
    message(FATAL_ERROR "The Switch app is built from the app/ folder, not from here:\n"
                        "  cd app\n"
                        "  cmake -B build_switch -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -DCMAKE_DEPENDS_USE_COMPILER=OFF\n"
                        "  cmake --build build_switch --target Library.nro\n"
                        "(this folder only builds the core library, its tests and libcli)")
endif ()

add_subdirectory(core)

add_executable(libcli tools/libcli.cpp)
target_link_libraries(libcli PRIVATE librarycore)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE librarycore)
find_package(ZLIB)
if (ZLIB_FOUND)
    target_compile_definitions(core_tests PRIVATE HAVE_ZLIB)
    target_link_libraries(core_tests PRIVATE ZLIB::ZLIB)
else ()
    message(STATUS "zlib not found: compressed-NACP tests will be skipped")
endif ()
add_test(NAME core_tests COMMAND core_tests)
