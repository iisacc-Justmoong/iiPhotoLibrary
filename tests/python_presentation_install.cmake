if(NOT SOURCE_ROOT OR NOT PRESENTATION_BUILD OR NOT TEST_ROOT OR NOT PYTHON OR NOT CORE_DIR OR NOT CORE_PACKAGE_DIR OR NOT GENERATOR OR NOT INSTALL_LIBDIR OR NOT INSTALL_DATADIR OR NOT CTEST_EXECUTABLE)
    message(FATAL_ERROR "Missing installed presentation consumer test arguments")
endif()
set(prefix "${TEST_ROOT}/prefix")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${PRESENTATION_BUILD}" --prefix "${prefix}" --config "${BUILD_CONFIG}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot install the presentation test package")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DYLD_LIBRARY_PATH --unset=DYLD_FALLBACK_LIBRARY_PATH
    --unset=DYLD_FRAMEWORK_PATH --unset=DYLD_FALLBACK_FRAMEWORK_PATH
    --unset=IIPHOTOLIBRARY_PRESENTATION_LIBRARY "PYTHONDONTWRITEBYTECODE=1"
    "IIPHOTOLIBRARY_TEST_PYTHON_MODULE=${prefix}/${INSTALL_DATADIR}/iiPhotoLibraryPresentation/python/iiphotolibrary_presentation.py"
    "${PYTHON}" -B "${SOURCE_ROOT}/tests/test_photo_presentation_python.py" - "${CORE_DIR}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed Python presentation consumer failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_ROOT}/tests/c-presentation-consumer" -B "${TEST_ROOT}/c-consumer"
    -G "${GENERATOR}" "-DiiPhotoLibraryPresentation_DIR=${prefix}/${INSTALL_LIBDIR}/cmake/iiPhotoLibraryPresentation"
    "-DiiSDKCore_DIR=${CORE_PACKAGE_DIR}" "-DCMAKE_BUILD_TYPE=${BUILD_CONFIG}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot configure installed C presentation consumer")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${TEST_ROOT}/c-consumer" --config "${BUILD_CONFIG}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot build installed C presentation consumer")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=DYLD_LIBRARY_PATH --unset=DYLD_FALLBACK_LIBRARY_PATH
    --unset=DYLD_FRAMEWORK_PATH --unset=DYLD_FALLBACK_FRAMEWORK_PATH
    "${CTEST_EXECUTABLE}" --test-dir "${TEST_ROOT}/c-consumer" -C "${BUILD_CONFIG}" --output-on-failure
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Installed C presentation consumer failed")
endif()
