cmake_minimum_required(VERSION 3.24)
project(iiPhotoLibrary VERSION 0.1.1 LANGUAGES CXX)





include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)
if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/build/install" CACHE PATH "Install prefix" FORCE)
endif()

option(IIPHOTOLIBRARY_ALBUMS_ONLY "Build the C++23 album core without the legacy Qt photo/UI layer" OFF)
find_package(Threads REQUIRED)
add_library(iiPhotoLibraryAlbums STATIC src/Albums/Album.cpp src/Albums/GooglePhotosSource.cpp)
add_library(iiPhotoLibrary::Albums ALIAS iiPhotoLibraryAlbums)
target_compile_features(iiPhotoLibraryAlbums PUBLIC cxx_std_23)
target_include_directories(iiPhotoLibraryAlbums PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(iiPhotoLibraryAlbums SYSTEM PRIVATE ThirdParty)
target_link_libraries(iiPhotoLibraryAlbums PUBLIC Threads::Threads)
set_target_properties(iiPhotoLibraryAlbums PROPERTIES EXPORT_NAME Albums CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(iiPhotoLibraryAlbumTests tests/album_tests.cpp)
    target_link_libraries(iiPhotoLibraryAlbumTests PRIVATE iiPhotoLibrary::Albums)
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
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Concurrent)
find_package(iiSocietyContainer 0.14.0 CONFIG REQUIRED COMPONENTS Sharing
    HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")
# Qt 6.8 refers to AGL, removed from current macOS SDKs.
include(cmake/QtApple.cmake)

add_library(iiPhotoLibrary STATIC
    src/PhotoStore.cpp src/PhotoController.cpp src/Albums/PhotoLibrarySource.cpp
    include/iiPhotoLibrary/PhotoController.h)
add_library(iiPhotoLibrary::iiPhotoLibrary ALIAS iiPhotoLibrary)
target_compile_features(iiPhotoLibrary PUBLIC cxx_std_23)
target_include_directories(iiPhotoLibrary PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(iiPhotoLibrary PUBLIC iiPhotoLibrary::Albums Qt6::Gui Qt6::Concurrent iiSocietyContainer::Sharing PRIVATE iiFileProvider::iiFileProvider)
set_target_properties(iiPhotoLibrary PROPERTIES AUTOMOC ON CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(iiPhotoLibrary PRIVATE src/PhotoLibraryApple.mm)
    set_source_files_properties(src/PhotoLibraryApple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(iiPhotoLibrary PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED ON)
    target_link_libraries(iiPhotoLibrary PRIVATE "-framework Photos" "-framework Foundation" "-framework UniformTypeIdentifiers")
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(iiPhotoLibrary PRIVATE "-framework UIKit")
    else()
        target_link_libraries(iiPhotoLibrary PRIVATE "-framework AppKit")
    endif()
elseif(ANDROID)
    target_sources(iiPhotoLibrary PRIVATE src/PhotoLibraryAndroid.cpp)
elseif(WIN32)
    target_sources(iiPhotoLibrary PRIVATE src/PhotoLibraryWindows.cpp)
    target_compile_definitions(iiPhotoLibrary PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(iiPhotoLibrary PRIVATE shell32 ole32 gdi32 uuid)
else()
    target_sources(iiPhotoLibrary PRIVATE src/PhotoLibraryUnsupported.cpp)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(iiPhotoLibraryAlbumBridgeTests tests/album_bridge_tests.cpp)
    target_link_libraries(iiPhotoLibraryAlbumBridgeTests PRIVATE iiPhotoLibrary::iiPhotoLibrary)
    add_test(NAME iiPhotoLibrary.NativeAlbumBridge COMMAND iiPhotoLibraryAlbumBridgeTests)
    set_tests_properties(iiPhotoLibrary.NativeAlbumBridge PROPERTIES TIMEOUT 30)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test Quick QuickControls2)
    find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/LVRS" "$ENV{HOME}/.local/SDK/LVRS")
    find_package(iiSocietySync 0.8.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietySync")
    qt_add_executable(iiPhotoLibraryTests tests/tst_photos.cpp)
    target_link_libraries(iiPhotoLibraryTests PRIVATE iiPhotoLibrary::iiPhotoLibrary
        iiSocietySync::iiSocietySync Qt6::Test Qt6::Quick Qt6::QuickControls2)
    lvrs_configure_qml_app(iiPhotoLibraryTests NO_PLATFORM_RUNTIME_TARGETS)
    set_target_properties(iiPhotoLibraryTests PROPERTIES AUTOMOC ON)
    target_compile_definitions(iiPhotoLibraryTests PRIVATE
        IIPHOTOLIBRARY_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        IIPHOTOLIBRARY_QML_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/PhotosHarness.qml"
        IIPHOTOLIBRARY_QML_IMPORT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qml"
        IIPHOTOLIBRARY_LVRS_QML_IMPORT_PATH="${LVRS_QML_IMPORT_PATH}")
    add_test(NAME iiPhotoLibrary.Photos COMMAND iiPhotoLibraryTests)
    set_tests_properties(iiPhotoLibrary.Photos PROPERTIES TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1")
    if(WIN32)
        set_property(TEST iiPhotoLibrary.Photos PROPERTY TIMEOUT 300)
    endif()
    add_test(NAME iiPhotoLibrary.InstalledConsumer COMMAND ${CMAKE_COMMAND}
        "-DGENERATOR=${CMAKE_GENERATOR}" "-DPROJECT_BUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DPROJECT_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DPREFIX_PATH=${CMAKE_PREFIX_PATH}" "-DQt6_DIR=${Qt6_DIR}"
        "-DiiSocietyContainer_DIR=${iiSocietyContainer_DIR}"
        "-DLVRS_DIR=${LVRS_DIR}" "-DLVRS_QML_IMPORT_PATH=${LVRS_QML_IMPORT_PATH}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer.cmake")
    set_tests_properties(iiPhotoLibrary.InstalledConsumer PROPERTIES TIMEOUT 120)
endif()

install(TARGETS iiPhotoLibrary EXPORT iiPhotoLibraryTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY qml/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibrary/qml)
install(DIRECTORY platform/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibrary/platform)
endif()

install(TARGETS iiPhotoLibraryAlbums EXPORT iiPhotoLibraryAlbumTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(IIPHOTOLIBRARY_ALBUMS_ONLY)
    install(FILES include/iiPhotoLibrary/Album.h include/iiPhotoLibrary/GooglePhotosAlbumSource.h
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
