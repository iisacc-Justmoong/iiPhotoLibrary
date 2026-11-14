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
foreach(_view PhotosView GalleryTile GalleryInfo GalleryZoom)
    if(NOT EXISTS "${SOURCE_ROOT}/src/Legacy/qml/iiPhotoLibrary/${_view}.qml")
        message(FATAL_ERROR "Missing isolated legacy photo view: ${_view}")
    endif()
endforeach()
if(EXISTS "${SOURCE_ROOT}/qml/iiPhotoLibrary/PhotosView.qml" OR
   NOT EXISTS "${SOURCE_ROOT}/src/Views/PhotosView.lvui")
    message(FATAL_ERROR "The active view must be LVUI and QML must be isolated as legacy")
endif()
if(EXISTS "${SOURCE_ROOT}/tests/PhotosHarness.qml" OR
   NOT EXISTS "${SOURCE_ROOT}/tests/Legacy/PhotosHarness.qml")
    message(FATAL_ERROR "The QML compatibility harness must be isolated as legacy")
endif()
if(NOT EXISTS "${SOURCE_ROOT}/src/Presentation/PresentationBridgeExport.h")
    message(FATAL_ERROR "The optional bridge needs its own shared-library export contract")
endif()
