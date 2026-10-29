#pragma once
#include "Album.h"
#include "PhotoLibrary.h"

namespace iiPhotoLibrary {
// Compatibility adapter to existing PhotoKit/MediaStore/Windows/native PhotoLibrary.
// One source belongs to one active connection. Cancellation retires its native library;
// create a new library/source when reconnecting. Do not share it with an independently
// running PhotoStore worker. Authorization remains a GUI-thread host action.
std::shared_ptr<AlbumSource> photoLibraryAlbumSource(std::shared_ptr<PhotoLibrary> library,
                                                    std::string providerId = "device");
}
