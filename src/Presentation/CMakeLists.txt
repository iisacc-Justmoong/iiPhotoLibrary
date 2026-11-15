find_package(iiSDKCore 0.20 CONFIG REQUIRED COMPONENTS Runtime
    HINTS "$ENV{HOME}/.local/SDK/iiSDKCore")

foreach(_header PhotoViewModel PhotoPresentationC PresentationExport)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/iiPhotoLibrary/${_header}.h"
        CONTENT "#pragma once\n#include \"${CMAKE_CURRENT_SOURCE_DIR}/${_header}.h\"\n")
endforeach()
add_library(iiPhotoLibraryPresentation PhotoViewModel.cpp PhotoViewModel.h PhotoPresentationC.cpp PhotoPresentationC.h
    PhotoInformationFormatter.cpp PhotoInformationFormatter.h PresentationExport.h)
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
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND BUILD_SHARED_LIBS)
    # Keep repeat installs from rewriting an already-installed Mach-O rpath.
    set_property(TARGET iiPhotoLibraryPresentation PROPERTY BUILD_WITH_INSTALL_RPATH TRUE)
endif()

set(_presentation_package "${CMAKE_INSTALL_LIBDIR}/cmake/iiPhotoLibraryPresentation")
install(TARGETS iiPhotoLibraryPresentation EXPORT iiPhotoLibraryPresentationTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES PhotoViewModel.h PhotoPresentationC.h PresentationExport.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiPhotoLibrary)
if(BUILD_SHARED_LIBS AND NOT EMSCRIPTEN)
    install(FILES "${PROJECT_SOURCE_DIR}/src/python/iiphotolibrary_presentation.py"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/iiPhotoLibraryPresentation/python)
endif()
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
    if(BUILD_SHARED_LIBS AND NOT EMSCRIPTEN AND TARGET iiSDKCore::iiSDKCore)
        add_test(NAME iiPhotoLibrary.PresentationPython COMMAND "${Python3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/tests/test_photo_presentation_python.py"
            "$<TARGET_FILE:iiPhotoLibraryPresentation>" "$<TARGET_FILE_DIR:iiSDKCore::iiSDKCore>")
        set_tests_properties(iiPhotoLibrary.PresentationPython PROPERTIES TIMEOUT 30
            ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
        add_test(NAME iiPhotoLibrary.PresentationPythonInstalled COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}" "-DPRESENTATION_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/python-installed-consumer" "-DPYTHON=${Python3_EXECUTABLE}"
            "-DCORE_DIR=$<TARGET_FILE_DIR:iiSDKCore::iiSDKCore>" "-DCORE_PACKAGE_DIR=${iiSDKCore_DIR}"
            "-DGENERATOR=${CMAKE_GENERATOR}" "-DBUILD_CONFIG=$<CONFIG>"
            "-DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}" "-DINSTALL_DATADIR=${CMAKE_INSTALL_DATADIR}"
            "-DCTEST_EXECUTABLE=${CMAKE_CTEST_COMMAND}" -P "${PROJECT_SOURCE_DIR}/tests/python_presentation_install.cmake")
        set_tests_properties(iiPhotoLibrary.PresentationPythonInstalled PROPERTIES TIMEOUT 90)
    endif()
    if(APPLE AND BUILD_SHARED_LIBS)
        add_test(NAME iiPhotoLibrary.PresentationReinstall
            COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                "-DREINSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/reinstall-check"
                "-DSOURCE_LIBRARY=$<TARGET_FILE:iiPhotoLibraryPresentation>"
                "-DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}" "-DBUILD_CONFIG=$<CONFIG>"
                -P "${PROJECT_SOURCE_DIR}/tests/presentation_reinstall.cmake")
    endif()
endif()
