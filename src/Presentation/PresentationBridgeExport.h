#pragma once

#if defined(_WIN32) && !defined(IIPHOTO_BRIDGE_STATIC)
# if defined(IIPHOTO_BRIDGE_BUILD)
#  define IIPHOTO_BRIDGE_EXPORT __declspec(dllexport)
# else
#  define IIPHOTO_BRIDGE_EXPORT __declspec(dllimport)
# endif
#elif defined(__GNUC__) && !defined(IIPHOTO_BRIDGE_STATIC)
# define IIPHOTO_BRIDGE_EXPORT __attribute__((visibility("default")))
#else
# define IIPHOTO_BRIDGE_EXPORT
#endif
