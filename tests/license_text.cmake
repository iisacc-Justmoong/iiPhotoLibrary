if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

function(iiphoto_require_text path expression description)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing ${description}: ${path}")
    endif()
    file(READ "${path}" text)
    if(NOT text MATCHES "${expression}")
        message(FATAL_ERROR "${description} does not match ${expression}: ${path}")
    endif()
endfunction()

iiphoto_require_text("${SOURCE_ROOT}/LICENSE" "Apache License" "first-party license")
iiphoto_require_text("${SOURCE_ROOT}/LICENSE" "Version 2\\.0, January 2004" "Apache-2.0 license heading")
iiphoto_require_text("${SOURCE_ROOT}/LICENSE" "Grant of Patent License" "Apache-2.0 patent grant")
iiphoto_require_text("${SOURCE_ROOT}/LICENSE" "END OF TERMS AND CONDITIONS" "Apache-2.0 license ending")
file(READ "${SOURCE_ROOT}/LICENSE" _iiphoto_license)
if(_iiphoto_license MATCHES "MIT License")
    message(FATAL_ERROR "First-party LICENSE still claims the MIT License")
endif()

iiphoto_require_text("${SOURCE_ROOT}/NOTICE" "Copyright \\(c\\) 2026 iisacc" "copyright notice")
iiphoto_require_text("${SOURCE_ROOT}/NOTICE" "Apache License" "NOTICE license statement")
iiphoto_require_text("${SOURCE_ROOT}/NOTICE" "Version 2\\.0" "NOTICE license version")
iiphoto_require_text("${SOURCE_ROOT}/NOTICE" "AGPL-3\\.0-or-later" "product license statement")

iiphoto_require_text("${SOURCE_ROOT}/README.md" "SPDX-License-Identifier: Apache-2\\.0" "README SPDX identifier")
iiphoto_require_text("${SOURCE_ROOT}/docs/License.md" "SPDX-License-Identifier: Apache-2\\.0" "Korean license SPDX identifier")
iiphoto_require_text("${SOURCE_ROOT}/AGENT.md" "Apache License 2\\.0" "agent license statement")

foreach(package_config IN ITEMS
        cmake/iiPhotoLibraryConfig.cmake.in
        cmake/iiPhotoLibraryPresentationConfig.cmake.in
        cmake/iiPhotoLibraryPresentationBridgeConfig.cmake.in
        src/Presentation/QtCompatibility/iiPhotoLibraryQtPresentationBridgeConfig.cmake.in)
    get_filename_component(_config_name "${package_config}" NAME_WE)
    string(REGEX REPLACE "Config$" "" _package "${_config_name}")
    iiphoto_require_text("${SOURCE_ROOT}/${package_config}"
        "set\\(${_package}_SPDX_LICENSE \"Apache-2\\.0\"\\)"
        "${_package} package SPDX export")
endforeach()

# Vendored nlohmann/json stays MIT. This assertion fails if that third-party grant is rewritten.
iiphoto_require_text("${SOURCE_ROOT}/ThirdParty/nlohmann/LICENSE.MIT" "MIT License" "nlohmann/json license")
iiphoto_require_text("${SOURCE_ROOT}/ThirdParty/nlohmann/LICENSE.MIT" "Niels Lohmann" "nlohmann/json copyright")
iiphoto_require_text("${SOURCE_ROOT}/ThirdParty/nlohmann/json.hpp" "SPDX-License-Identifier: MIT" "nlohmann/json SPDX header")
