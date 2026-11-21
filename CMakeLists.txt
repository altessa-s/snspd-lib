# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.

cmake_minimum_required(VERSION 3.21)

# The version lives in the public header and nowhere else: read out of it
# here rather than written a second time, so that a release is one edit and the
# library cannot report a number its package does not carry.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS include/snspd/snspd.h)
foreach(part MAJOR MINOR PATCH)
    file(STRINGS include/snspd/snspd.h line
         REGEX "^#define SNSPD_VERSION_${part} [0-9]+$")
    if(NOT line MATCHES "^#define SNSPD_VERSION_${part} ([0-9]+)$")
        message(FATAL_ERROR "SNSPD_VERSION_${part} not found in include/snspd/snspd.h")
    endif()
    set(SNSPD_VERSION_${part} "${CMAKE_MATCH_1}")
endforeach()

project(snspd
    VERSION ${SNSPD_VERSION_MAJOR}.${SNSPD_VERSION_MINOR}.${SNSPD_VERSION_PATCH}
    DESCRIPTION "Reading Supernote Atelier .spd drawings"
    HOMEPAGE_URL "https://github.com/altessa-s/snspd-lib"
    LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SNSPD_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(SNSPD_BUILD_TOOLS "Build the spddump command-line tool" ${PROJECT_IS_TOP_LEVEL})
option(SNSPD_WERROR "Turn compiler warnings into errors" OFF)
option(SNSPD_LTO "Link-time optimization for optimized shared builds" ON)
option(SNSPD_BUILD_FUZZERS "Build the libFuzzer targets (clang only)" OFF)

# ---------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------

set(SNSPD_SOURCES src/spd_model.c src/spd_parser.c src/spd_log.c)
set(SNSPD_PUBLIC_HEADERS include/snspd/snspd.h include/snspd/export.h
    include/snspd/types.h include/snspd/parser.h include/snspd/log.h)

# Honours BUILD_SHARED_LIBS: static by default, shared with
# -DBUILD_SHARED_LIBS=ON. One target either way, so the exported CMake package
# and the pkg-config file describe whatever was built.
add_library(snspd ${SNSPD_SOURCES})
add_library(SNSpd::snspd ALIAS snspd)
find_package(Threads REQUIRED)
target_link_libraries(snspd PUBLIC Threads::Threads)

target_include_directories(snspd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The included amalgamation is a system header; upstream warnings do not weaken
# the warning policy for the reader. All SQLite API/data symbols are static.
target_include_directories(snspd SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)
target_compile_definitions(snspd PRIVATE
    SQLITE_OS_OTHER=1 SQLITE_THREADSAFE=0 SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_ZERO_MALLOC=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0
    SQLITE_OMIT_WAL SQLITE_OMIT_SHARED_CACHE SQLITE_OMIT_UTF16
    SQLITE_OMIT_DEPRECATED SQLITE_OMIT_TCL_VARIABLE SQLITE_OMIT_TRACE
    SQLITE_OMIT_LOCALTIME
    SQLITE_TEMP_STORE=3 SQLITE_DEFAULT_LOOKASIDE=0,0 SQLITE_ENABLE_API_ARMOR
    SQLITE_MAX_LENGTH=67108864 SQLITE_MAX_SQL_LENGTH=4096 SQLITE_MAX_COLUMN=32
    SQLITE_MAX_EXPR_DEPTH=32 SQLITE_MAX_VARIABLE_NUMBER=8)
# GCC reports the static declarations of omitted SQLite features at the end of
# the unit, where the system-header suppression no longer applies. Clang keeps
# checking unused functions in the reader itself.
target_compile_options(snspd PRIVATE $<$<C_COMPILER_ID:GNU>:-Wno-unused-function>)

set(SNSPD_SOVERSION ${PROJECT_VERSION_MAJOR})
if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(SNSPD_SOVERSION "0.${PROJECT_VERSION_MINOR}")
endif()

set_target_properties(snspd PROPERTIES
    C_STANDARD 23
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    # The plumbing — the arena and the byte reader — is not part of
    # the contract and must not reach a consumer's symbol table.
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${SNSPD_SOVERSION}
    PUBLIC_HEADER "${SNSPD_PUBLIC_HEADERS}")

if(BUILD_SHARED_LIBS)
    target_compile_definitions(snspd PRIVATE SNSPD_BUILDING_SHARED)
    target_compile_definitions(snspd INTERFACE SNSPD_USING_SHARED)
    if(APPLE)
        # GCC otherwise appends runtime exports even with an explicit symbol list.
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_link_options(snspd PRIVATE -nodefaultexport)
        endif()
        set(SNSPD_EXPORTS "")
        foreach(header IN LISTS SNSPD_PUBLIC_HEADERS)
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${header}")
            file(READ "${header}" declarations)
            string(REGEX MATCHALL "SPD_API[^;]*spd_[A-Za-z0-9_]+[ \t\r\n]*\\(" declarations "${declarations}")
            foreach(declaration IN LISTS declarations)
                string(REGEX MATCH "spd_[A-Za-z0-9_]+[ \t\r\n]*\\(" symbol "${declaration}")
                string(REGEX REPLACE "[ \t\r\n]*\\($" "" symbol "${symbol}")
                string(APPEND SNSPD_EXPORTS "_${symbol}\n")
            endforeach()
        endforeach()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/snspd.exports" "${SNSPD_EXPORTS}")
        target_link_options(snspd PRIVATE
            "LINKER:-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/snspd.exports")
        set_property(TARGET snspd APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_BINARY_DIR}/snspd.exports")
    endif()
endif()

# Link-time optimization for the shared library only: a static archive built
# with it holds compiler bitcode that another compiler cannot link.
if(BUILD_SHARED_LIBS AND SNSPD_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT SNSPD_IPO OUTPUT SNSPD_IPO_ERROR LANGUAGES C)
    if(SNSPD_IPO)
        set_target_properties(snspd PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
            INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
            INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
    else()
        message(STATUS "snspd: link-time optimization unavailable: ${SNSPD_IPO_ERROR}")
    endif()
endif()

# The warning set, and it is one set: the library is built with clang or
# gcc on every platform, Windows included. MSVC's C compiler stops at the
# C23 nullptr and typed enum. A build that carries a second warning
# dialect for a compiler that cannot read the code is a build with a branch
# nobody can exercise.
target_compile_options(snspd PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion
    -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wcast-qual)
if(SNSPD_WERROR)
    target_compile_options(snspd PRIVATE -Werror)
endif()
# `isfinite` lives in libm on the platforms that still split it out.
include(CheckLibraryExists)
check_library_exists(m ceil "" SNSPD_HAVE_LIBM)
if(SNSPD_HAVE_LIBM)
    target_link_libraries(snspd PUBLIC m)
endif()

# ---------------------------------------------------------------------------
# The tool
# ---------------------------------------------------------------------------

if(SNSPD_BUILD_TOOLS)
    add_executable(spddump tools/spddump.c)
    target_link_libraries(spddump PRIVATE snspd)
    if(UNIX AND BUILD_SHARED_LIBS)
        file(RELATIVE_PATH SNSPD_TOOL_LIBDIR
            "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}")
        if(APPLE)
            set_target_properties(spddump PROPERTIES INSTALL_RPATH "@loader_path/${SNSPD_TOOL_LIBDIR}")
        else()
            set_target_properties(spddump PROPERTIES INSTALL_RPATH "$ORIGIN/${SNSPD_TOOL_LIBDIR}")
        endif()
    endif()
    set_target_properties(spddump PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON
                                            C_EXTENSIONS OFF)
    if(WIN32)
        # The UCRT deprecates fopen; the tool opens paths the user names.
        target_compile_definitions(spddump PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

# ---------------------------------------------------------------------------
# The tests
# ---------------------------------------------------------------------------

if(SNSPD_BUILD_TESTS OR SNSPD_BUILD_FUZZERS)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/seeds/stamp"
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tests/generate_fixtures.py"
                "${CMAKE_CURRENT_BINARY_DIR}/seeds"
        DEPENDS tests/generate_fixtures.py tests/generate_btrees.py)
    add_custom_target(seed_corpus DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/seeds/stamp")
endif()
if(SNSPD_BUILD_TESTS)
    enable_testing()
    add_executable(test_parser tests/test_parser.c)
    target_link_libraries(test_parser PRIVATE snspd)
    target_compile_definitions(test_parser PRIVATE SPD_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}/seeds")
    if(WIN32)
        target_compile_definitions(test_parser PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_dependencies(test_parser seed_corpus)
    add_executable(test_abi tests/test_abi.c)
    target_link_libraries(test_abi PRIVATE snspd)
    add_executable(consumer_c tests/consumer.c)
    target_link_libraries(consumer_c PRIVATE snspd)
    add_test(NAME parser COMMAND test_parser)
    add_test(NAME btree_limits COMMAND test_parser --btree)
    set_tests_properties(btree_limits PROPERTIES TIMEOUT 5)
    add_test(NAME large_file COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_large.py" "$<TARGET_FILE:test_parser>"
        "${CMAKE_CURRENT_BINARY_DIR}/large.spd")
    set_tests_properties(large_file PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME abi COMMAND test_abi)
    add_test(NAME consumer_c COMMAND consumer_c)
    set_tests_properties(parser PROPERTIES TIMEOUT 30)
    add_custom_target(build_tests DEPENDS test_parser test_abi consumer_c seed_corpus)
    include(CheckLanguage)
    check_language(CXX)
    if(CMAKE_CXX_COMPILER)
        enable_language(CXX)
        add_executable(consumer_cpp tests/consumer.cpp)
        target_link_libraries(consumer_cpp PRIVATE snspd)
        set_target_properties(consumer_cpp PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        add_test(NAME consumer_cpp COMMAND consumer_cpp "${CMAKE_CURRENT_BINARY_DIR}/seeds/valid.spd")
        find_package(Threads REQUIRED)
        add_executable(test_threads tests/test_threads.cpp)
        target_link_libraries(test_threads PRIVATE snspd Threads::Threads)
        set_target_properties(test_threads PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        add_test(NAME threads COMMAND test_threads "${CMAKE_CURRENT_BINARY_DIR}/seeds/valid.spd")
        set_tests_properties(threads PROPERTIES TIMEOUT 30)
        add_dependencies(build_tests consumer_cpp test_threads)
    endif()
    if(UNIX)
        add_test(NAME exports COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_exports.py" "$<TARGET_FILE:snspd>"
            "${CMAKE_CURRENT_SOURCE_DIR}/include/snspd" "$<IF:$<BOOL:${BUILD_SHARED_LIBS}>,shared,static>")
    endif()
    if(SNSPD_BUILD_TOOLS)
        add_test(NAME dumps COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_dumps.py" "$<TARGET_FILE:spddump>"
            "${CMAKE_CURRENT_BINARY_DIR}/seeds")
        add_dependencies(build_tests spddump)
    endif()
    add_custom_target(check COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        "$<$<BOOL:$<CONFIG>>:-C;$<CONFIG>>" DEPENDS build_tests COMMAND_EXPAND_LISTS)
endif()
if(SNSPD_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "libFuzzer requires clang")
    endif()
    target_compile_options(snspd PRIVATE -fsanitize=fuzzer-no-link)
    # Other executables need the coverage runtime without the fuzzer main.
    target_link_options(snspd PUBLIC -fsanitize=fuzzer-no-link)
    add_executable(fuzz_parse tests/fuzz_parse.c)
    target_link_libraries(fuzz_parse PRIVATE snspd)
    target_compile_options(fuzz_parse PRIVATE -fsanitize=fuzzer)
    target_link_options(fuzz_parse PRIVATE -fsanitize=fuzzer)
    if(SNSPD_BUILD_TESTS)
        add_dependencies(build_tests fuzz_parse)
        add_test(NAME fuzz_coverage COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_fuzz.py" "$<TARGET_FILE:fuzz_parse>"
            "${CMAKE_CURRENT_BINARY_DIR}/seeds")
        set_tests_properties(fuzz_coverage PROPERTIES TIMEOUT 30)
    endif()
endif()
# Apply the same warning contract to first-party executables, including tests.
get_property(SNSPD_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS SNSPD_TARGETS)
    get_target_property(kind ${target} TYPE)
    if(kind STREQUAL "EXECUTABLE")
        set_target_properties(${target} PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion)
        if(SNSPD_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

install(FILES LICENSE COPYRIGHT DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/snspd")

install(TARGETS snspd
    EXPORT SNSpdTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/snspd)

if(SNSPD_BUILD_TOOLS)
    install(TARGETS spddump RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(EXPORT SNSpdTargets
    FILE SNSpdTargets.cmake
    NAMESPACE SNSpd::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNSpd)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SNSpdConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SNSpdConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNSpd)

set(SNSPD_COMPATIBILITY SameMajorVersion)
if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(SNSPD_COMPATIBILITY SameMinorVersion)
endif()

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SNSpdConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ${SNSPD_COMPATIBILITY})

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SNSpdConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/SNSpdConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SNSpd)

# pkg-config, for the builds that never learned CMake.
#
# The way back from the `.pc` to the prefix is worked out here rather than
# written as a fixed `../..`: CMAKE_INSTALL_LIBDIR is `lib` on one
# distribution, `lib64` on another and `lib/x86_64-linux-gnu` on a third, and
# only one of those is two levels down.
file(RELATIVE_PATH SNSPD_PC_PREFIX_RELATIVE
     "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
if(SNSPD_PC_PREFIX_RELATIVE STREQUAL "")
    set(SNSPD_PC_PREFIX_RELATIVE ".")
endif()
string(REGEX REPLACE "/$" "" SNSPD_PC_PREFIX_RELATIVE "${SNSPD_PC_PREFIX_RELATIVE}")

set(SNSPD_PC_LIBS_PRIVATE "${CMAKE_THREAD_LIBS_INIT}")
if(SNSPD_HAVE_LIBM)
    string(APPEND SNSPD_PC_LIBS_PRIVATE " -lm")
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/snspd.pc.in
               ${CMAKE_CURRENT_BINARY_DIR}/snspd.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/snspd.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
