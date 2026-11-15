cmake_minimum_required(VERSION 3.24)
project(iiPhotoLibrary VERSION 0.1.1 LANGUAGES CXX)





include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
include(cmake/LegacyViewOptions.cmake)
if(BUILD_TESTING)
    foreach(_legacy_case IN ITEMS default on off)
        set(_legacy_option_arg)
        set(_legacy_expected OFF)
        if(_legacy_case STREQUAL "on")
            set(_legacy_option_arg "-DIIPHOTOLIBRARY_TEST_LEGACY_QML:BOOL=ON")
            set(_legacy_expected ON)
        elseif(_legacy_case STREQUAL "off")
            set(_legacy_option_arg "-DIIPHOTOLIBRARY_TEST_LEGACY_QML:BOOL=OFF")
        endif()
        add_test(NAME iiPhotoLibrary.LegacyViewOption.${_legacy_case}
            COMMAND "${CMAKE_COMMAND}" "-DPHOTO_OPTIONS=${CMAKE_CURRENT_SOURCE_DIR}/cmake/LegacyViewOptions.cmake"
                "-DEXPECTED_LEGACY=${_legacy_expected}" ${_legacy_option_arg}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy_view_option.cmake")
    endforeach()
    add_test(NAME iiPhotoLibrary.InstalledConsumerPathGuard
        COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DCONSUMER_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer.cmake"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer_policy.cmake")
endif()
option(BUILD_SHARED_LIBS "Build photo components as shared libraries" ON)
if(EMSCRIPTEN AND BUILD_SHARED_LIBS)
    set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)
endif()
function(iiphoto_configure_shared target)
    if(NOT BUILD_SHARED_LIBS)
        return()
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set_target_properties(${target} PROPERTIES FRAMEWORK TRUE FRAMEWORK_VERSION A
            MACOSX_FRAMEWORK_IDENTIFIER "com.iisacc.${target}")
    elseif(APPLE)
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@loader_path")
    elseif(UNIX AND NOT EMSCRIPTEN)
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
    if(EMSCRIPTEN)
        target_compile_options(${target} PRIVATE -fPIC -sSIDE_MODULE=1)
        target_link_options(${target} PRIVATE -sSIDE_MODULE=1)
    endif()
endfunction()
if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/build/install" CACHE PATH "Install prefix" FORCE)
endif()

option(IIPHOTOLIBRARY_ALBUMS_ONLY "Build the C++23 album core without the legacy Qt photo/UI layer" OFF)
if(IIPHOTOLIBRARY_ALBUMS_ONLY)
    set(_photo_presentation_default OFF)
else()
    set(_photo_presentation_default ON)
endif()
option(IIPHOTOLIBRARY_BUILD_PRESENTATION "Build the Qt-free photo view model" ${_photo_presentation_default})
if(IIPHOTOLIBRARY_BUILD_PRESENTATION)
    add_subdirectory(src/Presentation)
endif()
find_package(Threads REQUIRED)
set(_photo_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Albums/Album.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Albums/GooglePhotosAlbumSource.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Library/PhotoController.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Library/PhotoLibrary.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Library/PhotoLibraryAlbumSource.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Library/PhotoStore.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Library/iiPhotoLibrary.h")
if(IIPHOTOLIBRARY_BUILD_PRESENTATION AND NOT IIPHOTOLIBRARY_ALBUMS_ONLY)
    list(APPEND _photo_public_headers "${CMAKE_CURRENT_SOURCE_DIR}/src/Presentation/PhotoPresentationBridge.h")
endif()
foreach(_header IN LISTS _photo_public_headers)
    get_filename_component(_name "${_header}" NAME)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/iiPhotoLibrary/${_name}"
        CONTENT "#pragma once\n#include \"${_header}\"\n")
endforeach()
add_library(iiPhotoLibraryAlbums src/Albums/Album.cpp src/Albums/GooglePhotosSource.cpp)
iiphoto_configure_shared(iiPhotoLibraryAlbums)
add_library(iiPhotoLibrary::Albums ALIAS iiPhotoLibraryAlbums)
target_compile_features(iiPhotoLibraryAlbums PUBLIC cxx_std_23)
target_include_directories(iiPhotoLibraryAlbums PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/Albums>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(iiPhotoLibraryAlbums SYSTEM PRIVATE ThirdParty)
target_link_libraries(iiPhotoLibraryAlbums PUBLIC Threads::Threads)
set_target_properties(iiPhotoLibraryAlbums PROPERTIES EXPORT_NAME Albums CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(BUILD_TESTING)
    add_test(NAME iiPhotoLibrary.source_layout COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_layout.cmake")
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(iiPhotoLibraryAlbumTests tests/album_tests.cpp)
    target_link_libraries(iiPhotoLibraryAlbumTests PRIVATE iiPhotoLibrary::Albums)
    if(BUILD_SHARED_LIBS)
        add_test(NAME iiPhotoLibrary.AlbumsSharedType COMMAND "${CMAKE_COMMAND}"
            "-DTARGET_TYPE=$<TARGET_PROPERTY:iiPhotoLibraryAlbums,TYPE>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_target_type.cmake")
        add_test(NAME iiPhotoLibrary.AlbumsSharedLink COMMAND "${CMAKE_COMMAND}"
            "-DIMAGE=$<TARGET_FILE:iiPhotoLibraryAlbumTests>" "-DEXPECTED=$<TARGET_FILE:iiPhotoLibraryAlbums>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_link.cmake")
    endif()
    add_test(NAME iiPhotoLibrary.AlbumManager COMMAND iiPhotoLibraryAlbumTests manager)
    add_test(NAME iiPhotoLibrary.GooglePhotosAlbums COMMAND iiPhotoLibraryAlbumTests google)
    set_tests_properties(iiPhotoLibrary.AlbumManager iiPhotoLibrary.GooglePhotosAlbums PROPERTIES TIMEOUT 30)
    add_executable(iiPhotoLibraryAllLibraryTests tests/all_library_tests.cpp)
    target_link_libraries(iiPhotoLibraryAllLibraryTests PRIVATE iiPhotoLibrary::Albums)
    add_test(NAME iiPhotoLibrary.AllLibrary COMMAND iiPhotoLibraryAllLibraryTests)
    set_tests_properties(iiPhotoLibrary.AllLibrary PROPERTIES TIMEOUT 30)
    add_executable(iiPhotoLibrarySmartAlbumTests tests/smart_album_tests.cpp)
    target_link_libraries(iiPhotoLibrarySmartAlbumTests PRIVATE iiPhotoLibrary::Albums)
    add_test(NAME iiPhotoLibrary.SmartAlbums COMMAND iiPhotoLibrarySmartAlbumTests)
    set_tests_properties(iiPhotoLibrary.SmartAlbums PROPERTIES TIMEOUT 30)
    add_test(NAME iiPhotoLibrary.AlbumsInstalledConsumer COMMAND ${CMAKE_COMMAND}
        "-DGENERATOR=${CMAKE_GENERATOR}" "-DPROJECT_BUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DPROJECT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/albums_installed_consumer.cmake")
    set_tests_properties(iiPhotoLibrary.AlbumsInstalledConsumer PROPERTIES TIMEOUT 120)
endif()

set(IIPHOTOLIBRARY_HAS_LEGACY OFF)
if(NOT IIPHOTOLIBRARY_ALBUMS_ONLY)
# File creation is owned by iiFileProvider.
find_package(iiFileProvider 0.6 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")
set(IIPHOTOLIBRARY_HAS_LEGACY ON)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui)
find_package(iiSocietyContainer 0.14.0 CONFIG REQUIRED COMPONENTS Sharing
    HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")
# Qt 6.8 refers to AGL, removed from current macOS SDKs.
include(cmake/QtApple.cmake)

add_library(iiPhotoLibrary
    src/Library/PhotoStore.cpp src/Library/PhotoController.cpp src/Library/PhotoLibrarySource.cpp
    src/Library/PhotoController.h src/Library/PhotoWorker_p.h)
iiphoto_configure_shared(iiPhotoLibrary)
add_library(iiPhotoLibrary::iiPhotoLibrary ALIAS iiPhotoLibrary)
target_compile_features(iiPhotoLibrary PUBLIC cxx_std_23)
target_include_directories(iiPhotoLibrary PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/Library>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(iiPhotoLibrary PUBLIC iiPhotoLibrary::Albums Qt6::Gui iiSocietyContainer::Sharing
    PRIVATE iiFileProvider::iiFileProvider Threads::Threads)
if(IIPHOTOLIBRARY_BUILD_PRESENTATION)
    add_library(iiPhotoLibraryPresentationBridge src/Presentation/PhotoPresentationBridge.cpp src/Presentation/PhotoPresentationBridge.h)
    add_library(iiPhotoLibrary::PresentationBridge ALIAS iiPhotoLibraryPresentationBridge)
    target_compile_features(iiPhotoLibraryPresentationBridge PUBLIC cxx_std_23)
    target_compile_definitions(iiPhotoLibraryPresentationBridge PRIVATE IIPHOTO_BRIDGE_BUILD)
    if(NOT BUILD_SHARED_LIBS)
        target_compile_definitions(iiPhotoLibraryPresentationBridge PUBLIC IIPHOTO_BRIDGE_STATIC)
    endif()
    target_include_directories(iiPhotoLibraryPresentationBridge PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/Library>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(iiPhotoLibraryPresentationBridge PUBLIC iiPhotoLibrary::iiPhotoLibrary iiPhotoLibrary::Presentation)
    set_target_properties(iiPhotoLibraryPresentationBridge PROPERTIES EXPORT_NAME PresentationBridge CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON WINDOWS_EXPORT_ALL_SYMBOLS ON)
    iiphoto_configure_shared(iiPhotoLibraryPresentationBridge)
    install(TARGETS iiPhotoLibraryPresentationBridge EXPORT iiPhotoLibraryPresentationBridgeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES src/Presentation/PhotoPresentationBridge.h src/Presentation/PresentationBridgeExport.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiPhotoLibrary)
    set(_photo_bridge_package "${CMAKE_INSTALL_LIBDIR}/cmake/iiPhotoLibraryPresentationBridge")
    install(EXPORT iiPhotoLibraryPresentationBridgeTargets NAMESPACE iiPhotoLibrary:: DESTINATION "${_photo_bridge_package}")
    configure_package_config_file(cmake/iiPhotoLibraryPresentationBridgeConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryPresentationBridgeConfig.cmake" INSTALL_DESTINATION "${_photo_bridge_package}")
    write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryPresentationBridgeConfigVersion.cmake"
        VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryPresentationBridgeConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryPresentationBridgeConfigVersion.cmake" DESTINATION "${_photo_bridge_package}")
endif()
set_target_properties(iiPhotoLibrary PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(iiPhotoLibrary PRIVATE IIPHOTOLIBRARY_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiPhotoLibrary PUBLIC IIPHOTOLIBRARY_STATIC)
endif()
if(APPLE)
    enable_language(OBJCXX)
    target_sources(iiPhotoLibrary PRIVATE src/Platform/PhotoLibraryApple.mm)
    set_source_files_properties(src/Platform/PhotoLibraryApple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(iiPhotoLibrary PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED ON)
    target_link_libraries(iiPhotoLibrary PRIVATE "-framework Photos" "-framework Foundation" "-framework UniformTypeIdentifiers")
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(iiPhotoLibrary PRIVATE "-framework UIKit")
    else()
        target_link_libraries(iiPhotoLibrary PRIVATE "-framework AppKit")
    endif()
elseif(ANDROID)
    target_sources(iiPhotoLibrary PRIVATE src/Platform/PhotoLibraryAndroid.cpp)
elseif(WIN32)
    target_sources(iiPhotoLibrary PRIVATE src/Platform/PhotoLibraryWindows.cpp)
    target_compile_definitions(iiPhotoLibrary PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(iiPhotoLibrary PRIVATE shell32 ole32 gdi32 uuid)
else()
    target_sources(iiPhotoLibrary PRIVATE src/Platform/PhotoLibraryUnsupported.cpp)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(iiPhotoLibraryAlbumBridgeTests tests/album_bridge_tests.cpp)
    target_link_libraries(iiPhotoLibraryAlbumBridgeTests PRIVATE iiPhotoLibrary::iiPhotoLibrary)
    if(BUILD_SHARED_LIBS)
        add_test(NAME iiPhotoLibrary.LegacySharedType COMMAND "${CMAKE_COMMAND}"
            "-DTARGET_TYPE=$<TARGET_PROPERTY:iiPhotoLibrary,TYPE>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_target_type.cmake")
        add_test(NAME iiPhotoLibrary.LegacySharedLink COMMAND "${CMAKE_COMMAND}"
            "-DIMAGE=$<TARGET_FILE:iiPhotoLibraryAlbumBridgeTests>" "-DEXPECTED=$<TARGET_FILE:iiPhotoLibrary>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_link.cmake")
    endif()
    add_test(NAME iiPhotoLibrary.NativeAlbumBridge COMMAND iiPhotoLibraryAlbumBridgeTests)
    set_tests_properties(iiPhotoLibrary.NativeAlbumBridge PROPERTIES TIMEOUT 30)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    add_executable(iiPhotoLibraryGalleryModelTests tests/gallery_model.cpp)
    target_link_libraries(iiPhotoLibraryGalleryModelTests PRIVATE iiPhotoLibrary::iiPhotoLibrary Qt6::Test)
    set_target_properties(iiPhotoLibraryGalleryModelTests PROPERTIES AUTOMOC ON)
    add_test(NAME iiPhotoLibrary.gallery_model COMMAND iiPhotoLibraryGalleryModelTests)
    set_tests_properties(iiPhotoLibrary.gallery_model PROPERTIES TIMEOUT 30)
    if(IIPHOTOLIBRARY_BUILD_PRESENTATION)
        add_executable(iiPhotoLibraryPresentationBridgeTests tests/photo_presentation_bridge.cpp)
        target_link_libraries(iiPhotoLibraryPresentationBridgeTests PRIVATE iiPhotoLibrary::PresentationBridge Qt6::Test)
        set_target_properties(iiPhotoLibraryPresentationBridgeTests PROPERTIES AUTOMOC ON)
        add_test(NAME iiPhotoLibrary.PresentationBridge COMMAND iiPhotoLibraryPresentationBridgeTests)
        set_tests_properties(iiPhotoLibrary.PresentationBridge PROPERTIES TIMEOUT 30)
    endif()
    find_package(iiSocietyHelper 0.10.0 CONFIG REQUIRED)
    qt_add_executable(iiPhotoLibraryTests tests/tst_photos.cpp)
    target_link_libraries(iiPhotoLibraryTests PRIVATE iiPhotoLibrary::iiPhotoLibrary
        iiSocietyHelper::iiSocietyHelper Qt6::Test Qt6::Gui)
    if(UNIX AND TARGET iiSharedCanvas::Core)
        # CMake may treat this SDK directory as implicit because of LIBRARY_PATH,
        # but the helper's transitive shared libraries still need it at runtime.
        set_property(TARGET iiPhotoLibraryTests APPEND PROPERTY BUILD_RPATH "$<TARGET_FILE_DIR:iiSharedCanvas::Core>")
    endif()
    set_target_properties(iiPhotoLibraryTests PROPERTIES AUTOMOC ON)
    target_compile_definitions(iiPhotoLibraryTests PRIVATE
        IIPHOTOLIBRARY_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    add_test(NAME iiPhotoLibrary.Photos COMMAND iiPhotoLibraryTests)
    set_tests_properties(iiPhotoLibrary.Photos PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    if(WIN32)
        set_property(TEST iiPhotoLibrary.Photos PROPERTY TIMEOUT 300)
    endif()
    if(IIPHOTOLIBRARY_TEST_LEGACY_QML)
        find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Quick QuickControls2)
        # Native LVRS and the Qt test-support package can coexist in the SDK prefix.
        if(APPLE AND NOT LVRS_DIR AND "$ENV{LVRS_DIR}" STREQUAL "")
            set(_photo_lvrs_qt "$ENV{HOME}/.local/SDK/LVRS/platforms/macos/lib/cmake/LVRS")
            if(EXISTS "${_photo_lvrs_qt}/LVRSConfig.cmake")
                find_package(LVRS CONFIG QUIET PATHS "${_photo_lvrs_qt}" NO_DEFAULT_PATH)
            endif()
        endif()
        find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/LVRS" "$ENV{HOME}/.local/SDK/LVRS")
        if(NOT COMMAND lvrs_configure_qml_app)
            message(FATAL_ERROR "Legacy photo view tests require the Qt LVRS package; set LVRS_DIR to its CMake package directory")
        endif()
        qt_add_executable(iiPhotoLibraryLegacyViewTests tests/tst_photos.cpp)
        target_link_libraries(iiPhotoLibraryLegacyViewTests PRIVATE iiPhotoLibrary::iiPhotoLibrary
            iiSocietyHelper::iiSocietyHelper Qt6::Test Qt6::Quick Qt6::QuickControls2)
        if(UNIX AND TARGET iiSharedCanvas::Core)
            set_property(TARGET iiPhotoLibraryLegacyViewTests APPEND PROPERTY BUILD_RPATH "$<TARGET_FILE_DIR:iiSharedCanvas::Core>")
        endif()
        lvrs_configure_qml_app(iiPhotoLibraryLegacyViewTests NO_PLATFORM_RUNTIME_TARGETS)
        set_target_properties(iiPhotoLibraryLegacyViewTests PROPERTIES AUTOMOC ON)
        target_compile_definitions(iiPhotoLibraryLegacyViewTests PRIVATE IIPHOTOLIBRARY_TEST_LEGACY_QML=1
            IIPHOTOLIBRARY_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources"
            IIPHOTOLIBRARY_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/Legacy/PhotosHarness.qml"
            IIPHOTOLIBRARY_QML_IMPORT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/Legacy/qml"
            IIPHOTOLIBRARY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
        add_test(NAME iiPhotoLibrary.LegacyViews COMMAND iiPhotoLibraryLegacyViewTests
            chronologicalGalleryOpensAtNewestAndPreservesBrowsing galleryRendersAndLatePreviewKeepsBrowsingPosition)
        set_tests_properties(iiPhotoLibrary.LegacyViews PROPERTIES TIMEOUT 180
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
        if(WIN32)
            set_property(TEST iiPhotoLibrary.LegacyViews PROPERTY TIMEOUT 300)
        endif()
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME iiPhotoLibrary.LegacyTestInventory COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy_test_inventory.py"
            "$<TARGET_FILE:iiPhotoLibraryTests>" "$<TARGET_FILE:iiPhotoLibraryLegacyViewTests>")
        set_tests_properties(iiPhotoLibrary.LegacyTestInventory PROPERTIES TIMEOUT 30)
    endif()
    function(iiphoto_add_installed_consumer name legacy)
        add_test(NAME "${name}" COMMAND ${CMAKE_COMMAND}
            "-DGENERATOR=${CMAKE_GENERATOR}" "-DPROJECT_BUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DPROJECT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DPREFIX_PATH=${CMAKE_PREFIX_PATH}" "-DQt6_DIR=${Qt6_DIR}"
            "-DiiSocietyContainer_DIR=${iiSocietyContainer_DIR}" "-DiiSDKCore_DIR=${iiSDKCore_DIR}" "-DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
            "-DLVRS_DIR=${LVRS_DIR}" "-DLVRS_QML_IMPORT_PATH=${LVRS_QML_IMPORT_PATH}" "-DLEGACY_QML=${legacy}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer.cmake")
        set_tests_properties("${name}" PROPERTIES TIMEOUT 120)
    endfunction()
    iiphoto_add_installed_consumer(iiPhotoLibrary.InstalledConsumer OFF)
    if(IIPHOTOLIBRARY_TEST_LEGACY_QML)
        iiphoto_add_installed_consumer(iiPhotoLibrary.InstalledLegacyViewConsumer ON)
    endif()
endif()

install(TARGETS iiPhotoLibrary EXPORT iiPhotoLibraryTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
    src/Albums/Album.h
    src/Albums/GooglePhotosAlbumSource.h
    src/Library/PhotoController.h
    src/Library/PhotoLibrary.h
    src/Library/PhotoLibraryAlbumSource.h
    src/Library/PhotoLibraryExport.h
    src/Library/PhotoStore.h
    src/Library/iiPhotoLibrary.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiPhotoLibrary)
install(DIRECTORY src/Legacy/qml/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibrary/legacy/qml)
install(DIRECTORY platform/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibrary/platform)
endif()

install(TARGETS iiPhotoLibraryAlbums EXPORT iiPhotoLibraryAlbumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(IIPHOTOLIBRARY_ALBUMS_ONLY)
    install(FILES src/Albums/Album.h src/Albums/GooglePhotosAlbumSource.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiPhotoLibrary)
endif()
install(FILES ThirdParty/nlohmann/LICENSE.MIT DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibrary/licenses/nlohmann-json)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibrary/docs)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibrary)
set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiPhotoLibrary")
install(EXPORT iiPhotoLibraryAlbumTargets NAMESPACE iiPhotoLibrary:: DESTINATION ${package_dir})
if(IIPHOTOLIBRARY_HAS_LEGACY)
    install(EXPORT iiPhotoLibraryTargets NAMESPACE iiPhotoLibrary:: DESTINATION ${package_dir})
endif()
configure_package_config_file(cmake/iiPhotoLibraryConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryConfig.cmake" INSTALL_DESTINATION ${package_dir}
    PATH_VARS CMAKE_INSTALL_DATADIR)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryConfigVersion.cmake" cmake/QtApple.cmake DESTINATION ${package_dir})
