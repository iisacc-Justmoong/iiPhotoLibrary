if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(EXISTS "${SOURCE_ROOT}/src/iiPhotoLibrary")
    message(FATAL_ERROR "The source tree must not repeat the project name")
endif()
foreach(_file Albums/Album.h Albums/Album.cpp Albums/GooglePhotosAlbumSource.h
        Albums/GooglePhotosSource.cpp Library/PhotoController.h Library/PhotoController.cpp
        Library/PhotoStore.h Library/PhotoStore.cpp Library/PhotoLibrary.h
        Library/PhotoLibraryAlbumSource.h Library/PhotoLibrarySource.cpp Library/iiPhotoLibrary.h
        Platform/PhotoLibraryApple.mm Platform/PhotoLibraryAndroid.cpp
        Platform/PhotoLibraryWindows.cpp Platform/PhotoLibraryUnsupported.cpp)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing source in its responsibility directory: ${_file}")
    endif()
endforeach()
