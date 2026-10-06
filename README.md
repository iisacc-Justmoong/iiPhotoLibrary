# iiPhotoLibrary

C++23 is an SDK extracted as an independent package from the implementation of Photos and device photo album synchronization in Society. In the initial extraction, the source and build connection of Society were maintained. Subsequently, Society was switched to consume this package, and the existing internal implementation within the app is preserved for reference. The SDK does not reference Society app source or binaries and provides the `iiPhotoLibrary` namespace and `iiPhotoLibrary::iiPhotoLibrary` CMake target.

<a id="범위와-의존성"></a>

## Scope and dependencies

- `AlbumManager` / `Album`: independently instantiated local, iCloud Photos, Google Photos and received external albums, with per-connection identity, snapshots and lifetime management.
- `AlbumSource` / `GooglePhotosAlbumSource`: replaceable sources and current Google Photos Picker/app-created-item REST readers. OAuth, Picker UI and HTTPS transport are supplied by the host.
- `PhotoLibrary`: Native access permissions, view, preview, extraction, registration, trash interface.
- `PhotoStore` : permanent gallery index, visible region priority preview, alias, original cache/lease, chunk handover/SHA-256 validation, prevent deletion of limited access and incomplete query.
- `PhotoController` : QML model, work thread/cancel, authentication peer and container check, upload/selective original download. Inject transport layer via `Sender` callback.
- `PhotosView`: LVRS based screens and GalleryTile / GalleryInfo / GalleryZoom.
- Apple PhotoKit, Android MediaStore, Windows Shell adapter and unsupported platform storage-only adapter.

This 0.1 extraction preserves the existing Qt implementation. **The transition to a standard C++ SDK without Qt is not complete.** Qt 6.8.3 Core/Gui/Concurrent and iiSocietyContainer 0.14 Gui are required, and Qt types remain in the public API. Existing Qt dependencies are present in storage and synchronization code as well as GUI image processing and QML models. This is a compatibility package separated without rewriting its features; these dependencies must not be mistaken for a pure C++ core. Using QML requires LVRS and Qt Quick/QuickControls2/Dialogs. `iiSocietySync` and `iiServerHost` are required only for integration tests and are not direct dependencies of the library.

<a id="빌드테스트설치"></a>

## Build·Test·Install

The new `iiPhotoLibrary::Albums` target is a standard C++23 core without Qt. The existing native photo/storage/controller layer retains its Qt dependencies. Use `find_package(iiPhotoLibrary CONFIG REQUIRED COMPONENTS Albums)` and include `Album.h` or `GooglePhotosAlbumSource.h` for the core alone. `-DIIPHOTOLIBRARY_ALBUMS_ONLY=ON` builds only that core. See [album objects and external service contracts](docs/Albums.md) for iCloud System Photo Library coverage, Google API limits, host integration and validation.

All artifacts are placed under `build/`. The path of the existing SDK installation is added to `CMAKE_PREFIX_PATH`. If the LVRS installation location differs from the default, pass `-DLVRS_DIR=<installation>/lib/cmake/LVRS`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="<Qt>;<iiSocietyContainer>;<iiSocietySync>;<iiServerHost>;<LVRS>" \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/install"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build
```

Product consumption builds can omit test `-DBUILD_TESTING=OFF` LVRS · iiSocietySync search. SDK is the target for CMake automatic search of the integrated installer and does not require separate central list registration.

```cmake
find_package(iiPhotoLibrary 0.1 CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE iiPhotoLibrary::iiPhotoLibrary)
# Pass iiPhotoLibrary_QML_IMPORT_PATH to QQmlEngine::addImportPath().
```

```cpp
#include <iiPhotoLibrary/iiPhotoLibrary.h>
iiPhotoLibrary::PhotoController photos(sender, parent);
// Pass only peers verified by the existing authentication layer.
photos.configure(containerPath, true, authorizedPeers, hostPeers);
photos.setApplicationActive(true);
// society.photos request -> photos.handle(authenticatedPeer, packet)
// Transfer response -> photos.receive(requestId, reply)
```

```qml
import iiPhotoLibrary 1.0
PhotosView { controller: photoController }
```

The caller provides the prepared iiSocietyContainer container and the authenticated transfer path. File synchronization of metadata and preview is handled by a separate layer such as iiSocietySync. It delivers app foreground/background changes and job ownership release, and waits for termination after `waitForDone()` deactivation. `IIPHOTOLIBRARY_DISABLE_PHOTOS=1` disables this SDK's controller.

<a id="호환성검증"></a>

## Compatibility·Validation

`.societyphoto`, `.society-photos`, `society.photos`, `Society-<id>-<index>-<name>`, and the Pictures/Society path on Android are kept for existing data and transfer compatibility. C++ namespace and Android class are made independent to avoid conflicts with the app. Do not enable the old implementation and SDK controller simultaneously in the same container.

The original SHA-256 and extraction criteria Git revision are at [source-manifest.json](docs/source-manifest.json). Existing Society uncommitted changes are not changed regardless of inclusion or exclusion.
[original operation contract](docs/SocietyReference.md)is the reference document at the time of extraction, and the existing product's platform verification written in that document does not mean the new verification of this SDK.
Refer to [platform integration](docs/Platforms.md), [extraction and verification results](docs/Extraction.md).
