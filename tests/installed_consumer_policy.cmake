if(NOT SOURCE_ROOT OR NOT CONSUMER_SCRIPT)
    message(FATAL_ERROR "The photo source root and consumer script are required")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE=${SOURCE_ROOT}" "-DPROJECT_BUILD=${SOURCE_ROOT}/resources"
        -DINSTALL_LIBDIR=lib -P "${CONSUMER_SCRIPT}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT error MATCHES "photo consumer build must be inside this project's build directory")
    message(FATAL_ERROR "The photo installed-consumer test did not reject an external build: ${output}${error}")
endif()
