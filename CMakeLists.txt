cmake_minimum_required(VERSION 3.21)

project(QuirewiseSpreadsheet VERSION 0.1.0 LANGUAGES C)
option(QUIREWISE_BUILD_SHARED "Build Quirewise libraries as shared libraries" OFF)
if(QUIREWISE_BUILD_SHARED)
    set(QW_LIBRARY_TYPE SHARED)
else()
    set(QW_LIBRARY_TYPE STATIC)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(NOT TARGET Quirewise::Runtime)
    find_package(QuirewiseRuntime CONFIG REQUIRED)
endif()
if(NOT TARGET Quirewise::Archive)
    find_package(QuirewiseArchive CONFIG REQUIRED)
endif()
if(NOT TARGET Quirewise::Data)
    find_package(QuirewiseData CONFIG REQUIRED)
endif()
file(GLOB_RECURSE QW_SOURCES CONFIGURE_DEPENDS src/*.c src/*.cpp)
add_library(quirewise_spreadsheet ${QW_LIBRARY_TYPE} ${QW_SOURCES})
add_library(Quirewise::Spreadsheet ALIAS quirewise_spreadsheet)
set_target_properties(quirewise_spreadsheet PROPERTIES EXPORT_NAME Spreadsheet POSITION_INDEPENDENT_CODE ON)
target_compile_features(quirewise_spreadsheet PUBLIC c_std_11)
target_include_directories(quirewise_spreadsheet PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/core ${CMAKE_CURRENT_SOURCE_DIR}/src/formats ${CMAKE_CURRENT_SOURCE_DIR}/src/backends)
target_link_libraries(quirewise_spreadsheet PRIVATE Quirewise::Archive Quirewise::Data)
install(TARGETS quirewise_spreadsheet EXPORT QuirewiseSpreadsheetTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT QuirewiseSpreadsheetTargets FILE QuirewiseSpreadsheetTargets.cmake NAMESPACE Quirewise:: DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseSpreadsheet")
configure_package_config_file(cmake/QuirewiseSpreadsheetConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseSpreadsheetConfig.cmake" INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseSpreadsheet")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/QuirewiseSpreadsheetConfigVersion.cmake" VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseSpreadsheetConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/QuirewiseSpreadsheetConfigVersion.cmake" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QuirewiseSpreadsheet")

if(QUIREWISE_BUILD_SHARED AND WIN32)
    set_target_properties(
        quirewise_spreadsheet
        PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(quirewise_spreadsheet_fuzz_smoke tests/spreadsheet_fuzz_smoke.c)
    target_compile_features(quirewise_spreadsheet_fuzz_smoke PRIVATE c_std_11)
    target_link_libraries(quirewise_spreadsheet_fuzz_smoke PRIVATE Quirewise::Spreadsheet)
    add_test(NAME quirewise_spreadsheet_fuzz_smoke
        COMMAND quirewise_spreadsheet_fuzz_smoke)
endif()
target_link_libraries(quirewise_spreadsheet PUBLIC Quirewise::Runtime)
