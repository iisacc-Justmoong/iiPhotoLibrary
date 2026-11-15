cmake_minimum_required(VERSION 3.24)
project(PhotoLibraryConsumer LANGUAGES CXX)
find_package(iiPhotoLibrary 0.1 CONFIG REQUIRED)
if(NOT iiPhotoLibrary_QML_IMPORT_PATH STREQUAL iiPhotoLibrary_LEGACY_QML_IMPORT_PATH OR
   NOT EXISTS "${iiPhotoLibrary_LEGACY_QML_IMPORT_PATH}/iiPhotoLibrary/PhotosView.qml")
    message(FATAL_ERROR "The QML compatibility import must resolve to the isolated legacy view")
endif()
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Gui)
option(PHOTO_CONSUMER_LEGACY_QML "Exercise the isolated legacy QML view" OFF)
qt_add_executable(PhotoLibraryConsumer main.cpp)
target_link_libraries(PhotoLibraryConsumer PRIVATE iiPhotoLibrary::iiPhotoLibrary Qt6::Gui)
if(PHOTO_CONSUMER_LEGACY_QML)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Quick QuickControls2)
    find_package(LVRS CONFIG REQUIRED)
    target_link_libraries(PhotoLibraryConsumer PRIVATE Qt6::Quick Qt6::QuickControls2)
    lvrs_configure_qml_app(PhotoLibraryConsumer NO_PLATFORM_RUNTIME_TARGETS)
    target_compile_definitions(PhotoLibraryConsumer PRIVATE PHOTO_CONSUMER_LEGACY_QML=1
        PHOTO_QML_PATH="${iiPhotoLibrary_QML_IMPORT_PATH}"
        LVRS_QML_PATH="${LVRS_QML_IMPORT_PATH}"
        PHOTO_LEGACY_QML_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../Legacy/PhotoConsumerQmlFixture.qml")
endif()

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/run-path.txt" CONTENT "$<TARGET_FILE:PhotoLibraryConsumer>")
