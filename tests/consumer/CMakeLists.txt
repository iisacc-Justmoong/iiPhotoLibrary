cmake_minimum_required(VERSION 3.24)
project(PhotoLibraryConsumer LANGUAGES CXX)
find_package(iiPhotoLibrary 0.1 CONFIG REQUIRED)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Quick QuickControls2)
find_package(LVRS CONFIG REQUIRED)
qt_add_executable(PhotoLibraryConsumer main.cpp)
target_link_libraries(PhotoLibraryConsumer PRIVATE iiPhotoLibrary::iiPhotoLibrary Qt6::Quick Qt6::QuickControls2)
lvrs_configure_qml_app(PhotoLibraryConsumer NO_PLATFORM_RUNTIME_TARGETS)
target_compile_definitions(PhotoLibraryConsumer PRIVATE
    PHOTO_QML_PATH="${iiPhotoLibrary_QML_IMPORT_PATH}"
    LVRS_QML_PATH="${LVRS_QML_IMPORT_PATH}")

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/run-path.txt" CONTENT "$<TARGET_FILE:PhotoLibraryConsumer>")
