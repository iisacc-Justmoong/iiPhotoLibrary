find_package(iiSDKCore 0.20 CONFIG REQUIRED COMPONENTS Runtime
    HINTS "$ENV{HOME}/.local/SDK/iiSDKCore")

foreach(_header PhotoViewModel PresentationExport)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/iiPhotoLibrary/${_header}.h"
        CONTENT "#pragma once\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/${_header}.h\"\n")
endforeach()
add_library(iiPhotoLibraryPresentation PhotoViewModel.cpp PhotoViewModel.h PresentationExport.h)
add_library(iiPhotoLibrary::Presentation ALIAS iiPhotoLibraryPresentation)
target_compile_features(iiPhotoLibraryPresentation PUBLIC cxx_std_23)
target_compile_definitions(iiPhotoLibraryPresentation PRIVATE IIPHOTO_PRESENTATION_BUILD)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiPhotoLibraryPresentation PUBLIC IIPHOTO_PRESENTATION_STATIC)
endif()
target_include_directories(iiPhotoLibraryPresentation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(iiPhotoLibraryPresentation PUBLIC iiSDKCore::Runtime)
set_target_properties(iiPhotoLibraryPresentation PROPERTIES EXPORT_NAME Presentation
    CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES POSITION_INDEPENDENT_CODE ON)
iiphoto_configure_shared(iiPhotoLibraryPresentation)

set(_presentation_package "${CMAKE_INSTALL_LIBDIR}/cmake/iiPhotoLibraryPresentation")
install(TARGETS iiPhotoLibraryPresentation EXPORT iiPhotoLibraryPresentationTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES PhotoViewModel.h PresentationExport.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiPhotoLibrary)
install(FILES "${PROJECT_SOURCE_DIR}/src/Views/PhotosView.lvui"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibraryPresentation/views)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/resources/photo-view/strings/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibraryPresentation/strings)
install(EXPORT iiPhotoLibraryPresentationTargets NAMESPACE iiPhotoLibrary:: DESTINATION "${_presentation_package}")
configure_package_config_file("${PROJECT_SOURCE_DIR}/cmake/iiPhotoLibraryPresentationConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryPresentationConfig.cmake" INSTALL_DESTINATION "${_presentation_package}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryPresentationConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryPresentationConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiPhotoLibraryPresentationConfigVersion.cmake" DESTINATION "${_presentation_package}")
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(iiPhotoLibraryPresentationTests "${PROJECT_SOURCE_DIR}/tests/photo_presentation.cpp")
    target_link_libraries(iiPhotoLibraryPresentationTests PRIVATE iiPhotoLibrary::Presentation)
    add_test(NAME iiPhotoLibrary.Presentation COMMAND iiPhotoLibraryPresentationTests)
    set_tests_properties(iiPhotoLibrary.Presentation PROPERTIES TIMEOUT 30)
    add_test(NAME iiPhotoLibrary.PresentationStrings COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/photo_strings.py" "${PROJECT_SOURCE_DIR}")
endif()
