#pragma once
#if defined(_WIN32) && !defined(IIPHOTOLIBRARY_STATIC)
# if defined(IIPHOTOLIBRARY_BUILDING_LIBRARY)
#  define IIPHOTOLIBRARY_EXPORT __declspec(dllexport)
# else
#  define IIPHOTOLIBRARY_EXPORT __declspec(dllimport)
# endif
#else
# define IIPHOTOLIBRARY_EXPORT
#endif
