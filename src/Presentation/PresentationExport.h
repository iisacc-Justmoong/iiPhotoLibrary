#pragma once

#if defined(_WIN32) && !defined(IIPHOTO_PRESENTATION_STATIC)
# if defined(IIPHOTO_PRESENTATION_BUILD)
#  define IIPHOTO_PRESENTATION_EXPORT __declspec(dllexport)
# else
#  define IIPHOTO_PRESENTATION_EXPORT __declspec(dllimport)
# endif
#elif defined(__GNUC__) && !defined(IIPHOTO_PRESENTATION_STATIC)
# define IIPHOTO_PRESENTATION_EXPORT __attribute__((visibility("default")))
#else
# define IIPHOTO_PRESENTATION_EXPORT
#endif
