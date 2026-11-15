#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace iiPhotoLibrary::presentation::detail {
[[nodiscard]] std::string formatField(std::string_view pattern,std::string_view value);
[[nodiscard]] std::string formatSize(std::uint64_t bytes,std::string_view bytesPattern,
    std::string_view kilobytesPattern,std::string_view megabytesPattern,std::string_view gigabytesPattern);
[[nodiscard]] std::string formatTimestamp(std::string_view timestamp);
}
