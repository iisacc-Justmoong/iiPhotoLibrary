cmake_minimum_required(VERSION 3.24)
project(iiPhotoLibraryQtPresentationBridge VERSION 0.1.1 LANGUAGES CXX)
set(iiPhotoLibraryQtPresentationBridge_SPDX_LICENSE "Apache-2.0")

set(CMAKE_CXX_EXTENSIONS OFF)
option(BUILD_SHARED_LIBS "Build the compatibility adapter as a shared library" OFF)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(iiPhotoLibrary 0.1 CONFIG REQUIRED COMPONENTS Library)
find_package(iiPhotoLibraryPresentation 0.1 CONFIG REQUIRED)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

# A shared adapter must not embed its own copy of a static PhotoController
# while the application also links that controller. Use a static adapter in
# that configuration, or select a shared photo service.
get_target_property(_photo_service_type iiPhotoLibrary::iiPhotoLibrary TYPE)
if(BUILD_SHARED_LIBS AND NOT _photo_service_type STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "A shared photo adapter requires a shared photo service; use BUILD_SHARED_LIBS=OFF for a static service")
endif()

add_library(iiPhotoLibraryQtPresentationBridge ../PhotoPresentationBridge.cpp
    ../PhotoPresentationBridge.h ../PresentationBridgeExport.h)
add_library(iiPhotoLibrary::QtPresentationBridge ALIAS iiPhotoLibraryQtPresentationBridge)
set_target_properties(iiPhotoLibraryQtPresentationBridge PROPERTIES EXPORT_NAME QtPresentationBridge
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR} POSITION_INDEPENDENT_CODE ON)
target_compile_features(iiPhotoLibraryQtPresentationBridge PUBLIC cxx_std_23)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(iiPhotoLibraryQtPresentationBridge PRIVATE IIPHOTO_BRIDGE_BUILD)
    if(APPLE)
        set_property(TARGET iiPhotoLibraryQtPresentationBridge PROPERTY INSTALL_RPATH "@loader_path")
    elseif(UNIX AND NOT EMSCRIPTEN)
        set_property(TARGET iiPhotoLibraryQtPresentationBridge PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
else()
    target_compile_definitions(iiPhotoLibraryQtPresentationBridge PUBLIC IIPHOTO_BRIDGE_STATIC)
endif()
target_include_directories(iiPhotoLibraryQtPresentationBridge PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_link_libraries(iiPhotoLibraryQtPresentationBridge PUBLIC
    iiPhotoLibrary::iiPhotoLibrary iiPhotoLibrary::Presentation Qt6::Core)
foreach(header PhotoPresentationBridge.h PresentationBridgeExport.h)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/iiPhotoLibrary/${header}"
        CONTENT "#pragma once\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/../${header}\"\n")
endforeach()

install(TARGETS iiPhotoLibraryQtPresentationBridge EXPORT iiPhotoLibraryQtPresentationBridgeTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../PhotoPresentationBridge.h ../PresentationBridgeExport.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiPhotoLibrary)
set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiPhotoLibraryQtPresentationBridge")
install(EXPORT iiPhotoLibraryQtPresentationBridgeTargets NAMESPACE iiPhotoLibrary:: DESTINATION "${package_dir}")
configure_package_config_file(iiPhotoLibraryQtPresentationBridgeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryQtPresentationBridgeConfig.cmake" INSTALL_DESTINATION "${package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryQtPresentationBridgeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryQtPresentationBridgeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryQtPresentationBridgeConfigVersion.cmake" DESTINATION "${package_dir}")

if(BUILD_TESTING)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(iiPhotoLibraryQtPresentationBridgeTests ../../../tests/photo_qt_compatibility.cpp)
    target_link_libraries(iiPhotoLibraryQtPresentationBridgeTests PRIVATE iiPhotoLibraryQtPresentationBridge Qt6::Test)
    add_test(NAME iiPhotoLibrary.QtPresentationBridge COMMAND iiPhotoLibraryQtPresentationBridgeTests)
    set_tests_properties(iiPhotoLibrary.QtPresentationBridge PROPERTIES TIMEOUT 30)
    if(APPLE)
        set_tests_properties(iiPhotoLibrary.QtPresentationBridge PROPERTIES ENVIRONMENT_MODIFICATION
            "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:")
    endif()
endif()
