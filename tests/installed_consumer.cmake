function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${output}\n${error}")
    endif()
    message(STATUS "${output}${error}")
endfunction()
set(stage "${PROJECT_BUILD}/consumer-stage")
set(relocated "${PROJECT_BUILD}/consumer-relocated")
# Only disposable outputs created by this test are replaced.
file(REMOVE_RECURSE "${stage}" "${relocated}")
run("${CMAKE_COMMAND}" --install "${PROJECT_BUILD}" --prefix "${stage}")
file(RENAME "${stage}" "${relocated}")
file(WRITE "${PROJECT_BUILD}/consumer-prefix.cmake" "set(CMAKE_PREFIX_PATH [==[${PREFIX_PATH}]==] CACHE STRING Dependencies FORCE)\n")
run("${CMAKE_COMMAND}" --fresh -G "${GENERATOR}" -C "${PROJECT_BUILD}/consumer-prefix.cmake" -S "${PROJECT_SOURCE}/tests/consumer" -B "${PROJECT_BUILD}/consumer/build"
    "-DiiPhotoLibrary_DIR=${relocated}/lib/cmake/iiPhotoLibrary"
    "-DQt6_DIR=${Qt6_DIR}"
    "-DiiSocietyContainer_DIR=${iiSocietyContainer_DIR}" "-DLVRS_DIR=${LVRS_DIR}"
    -DCMAKE_BUILD_TYPE=Release)
run("${CMAKE_COMMAND}" --build "${PROJECT_BUILD}/consumer/build" --parallel 4)
file(READ "${PROJECT_BUILD}/consumer/build/run-path.txt" consumer_executable)
run("${CMAKE_COMMAND}" -E env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_DISABLE_DISK_CACHE=1
    "${consumer_executable}")
