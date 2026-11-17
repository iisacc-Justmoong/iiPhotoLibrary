# The Qt FileDevice facade has changed its object layout without every older
# static consumer changing its package version. Record the exact public header
# used by this library, so a consumer cannot silently link a different layout.
function(iiphoto_provider_device_hash target output)
    set(result "")
    set(selected_directory "")
    if(TARGET ${target})
        get_target_property(directories ${target} INTERFACE_INCLUDE_DIRECTORIES)
        foreach(directory IN LISTS directories)
            if(EXISTS "${directory}/iiFileProviderDevice.h")
                file(SHA256 "${directory}/iiFileProviderDevice.h" result)
                set(selected_directory "${directory}")
                break()
            endif()
        endforeach()
    endif()
    set(${output} "${result}" PARENT_SCOPE)
    if(ARGC GREATER 2)
        set(${ARGV2} "${selected_directory}" PARENT_SCOPE)
    endif()
endfunction()

function(iiphoto_provider_abi_matches expected actual output)
    string(LENGTH "${expected}" expected_length)
    string(LENGTH "${actual}" actual_length)
    if(expected_length EQUAL 64 AND actual_length EQUAL 64
            AND "${expected}" MATCHES "^[0-9a-f]+$"
            AND "${actual}" MATCHES "^[0-9a-f]+$"
            AND "${expected}" STREQUAL "${actual}")
        set(${output} TRUE PARENT_SCOPE)
    else()
        set(${output} FALSE PARENT_SCOPE)
    endif()
endfunction()
