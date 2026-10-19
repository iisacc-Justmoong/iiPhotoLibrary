# iiPhotoLibrary

[Windows 빌드 및 실행](docs/WindowsBuildAndRun.md)

Package release: **0.1.1** (2026-10-19). This patch packages the current
source snapshot; build, test and installation evidence is recorded in the SDK
patch-release report. Earlier versioned feature notes remain historical.


C++23 is an SDK extracted as an independent package from the implementation of Photos and device photo album synchronization in Society. In the initial extraction, the source and build connection of Society were maintained. Subsequently, Society was switched to consume this package, and the existing internal implementation within the app is preserved for reference. The SDK does not reference Society app source or binaries and provides the `iiPhotoLibrary` namespace and `iiPhotoLibrary::iiPhotoLibrary` CMake target.

<a id="범위와-의존성"></a>

## Scope and dependencies

- `AlbumManager` / `Album`: independently instantiated local, iCloud Photos, Google Photos and received external albums, with per-connection identity, snapshots and lifetime management.
- `AlbumManager::allLibrary()`: a stable, read-only album named **All Library** that returns the union of currently loaded, visible photos across those albums. Photos are deduplicated by provider/account/source-photo identity, with source references retained in metadata. Reads reflect album updates and disconnections without performing I/O.
- `AlbumManager::smartAlbum()` / `smartAlbums()`: eight live **Screenshots**, **Imported Photos**, **Duplicates**, **Hidden**, **Images**, **Videos**, **Favorite** and **Deleted** views. Media views use the existing photo/video type; favorites use the existing `AlbumPhoto::favorite` field. Typed traits and `setScreenshot()`, `setImported()`, `setHidden()`, `setContentHash()`, `setFavorite()` and `setDeleted()` drive classification; `setPhotoTraits()` updates several fields atomically. Exact duplicates use supplied SHA-256 digests and are available through `duplicateGroups()`. Deleted items appear only in Deleted, and the remaining hidden items appear only in Hidden. `setDeleted(id, false)` restores the item's previous classifications. These changes affect SDK metadata; originals and native/remote trash are handled by their owning host. Apple scans forward PhotoKit screenshot/hidden/favorite metadata; hosts provide hashes and import classifications.
- `AlbumSource` / `GooglePhotosAlbumSource`: replaceable sources and current Google Photos Picker/app-created-item REST readers. OAuth, Picker UI and HTTPS transport are supplied by the host.
- `PhotoLibrary`: Low-level native access, preview, extraction and explicitly requested OS registration/trash interface.
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

## Photo storage directory lifetime

Opening an empty `PhotoStore`, publishing its startup catalog, and reading its gallery, aliases or resource paths do not create `.society-photos/<container-id>/` or `Photos/.previews/`. The store validates these locations even when they are absent. Existing photo records and native references retain their format and identity.

An initial refresh with no stored photo state or Photos payload succeeds without writing an operation lock, index or startup catalog, even when the OS library contains photos. Ordinary refresh does not scan that library. This also applies to empty directories prepared by an older session. Existing records, references, tombstones and unknown files retain the normal refresh path. An explicitly requested nonempty native scan is reused by the writer instead of scanning twice; if another process committed photo history meanwhile, the store scans again under its operation lock.

Each write prepares only its required parent: gallery and reference JSON files create their respective directories, preview writes create `Photos/.previews/`, and native exports or incoming chunks create `incoming/`. Committing or explicitly preparing an original creates `originals/`; gallery discovery alone leaves that cache absent. Viewing and sharing exports create `viewing/` on demand. Storage capacity checks use the existing container root so that the first transfer can begin before the private directory exists. Empty staging/cache directories removed between completed operations are recreated by the next writer, with path confinement checks retained.

The `iiPhotoLibrary.Photos` regression suite covers repeated empty opens/reads/refreshes, container connection without OS scans or permission prompts, explicit library reads, older empty scaffolding, preservation of unknown and user-supplied files, discovery before original staging, recreation of unused write parents, and the first received transfer without an initial refresh. `iiPhotoLibrary` is a static library: consumers such as the Society GUI and daemon must be relinked to use this behavior. A process already running the previous implementation can still depend on directories prepared when its session opened. Do not remove those session directories while preserving that process; installing a new bundle does not replace the code in its existing session.

## Container-owned photo operations

`PhotoStore::refresh()` manages only files, aliases, existing references and caches in the selected `.societycontainer`. It does not enumerate the OS library, request permission, register an OS asset or delete one. Images supplied in `Photos/` remain at their original container paths. Their portable aliases and verified resource caches are stored inside the same container. `consume()` validates complete cached resources without exporting them to Apple Photos, Android MediaStore or Windows Pictures. `trash()` records a container tombstone; both local and remote tombstones leave native assets unchanged.

An application can explicitly call `PhotoStore::importLibrary()` to read the permitted OS library into container aliases, references and previews. `PhotoController::connectLibrary()` performs that one read after an explicit permission request when required. Configuring a container, entering the foreground and periodic refresh do not initiate native discovery. Later native assets require another explicit connection/import; viewing or transferring an existing reference can read that selected original into the container. The independent `PhotoLibrary::import()` and `PhotoLibrary::trash()` APIs remain available for other consumers that intentionally mutate an OS library.

Container-owned duplicates and received resource revisions keep their verified originals when transient native exports are released or pruned. Shared hashes are retained while any live alias lacks a matching available native reference. A regular refresh can repair a damaged stored gallery preview from its existing canonical container preview without a native scan or export.

<a id="호환성검증"></a>

## Compatibility·Validation

`.societyphoto`, `.society-photos`, `society.photos` and `Society-<id>-<index>-<name>` remain compatible with existing data and transfers. The low-level native import adapter retains its Pictures/Society destination for explicit OS-library consumers; PhotoStore does not invoke it. C++ namespace and Android class are independent to avoid conflicts with the app. Do not enable the old implementation and SDK controller simultaneously in the same container.

The original SHA-256 and extraction criteria Git revision are at [source-manifest.json](docs/source-manifest.json). Existing Society uncommitted changes are not changed regardless of inclusion or exclusion.
[original operation contract](docs/SocietyReference.md)is the reference document at the time of extraction, and the existing product's platform verification written in that document does not mean the new verification of this SDK.
Refer to [platform integration](docs/Platforms.md), [extraction and verification results](docs/Extraction.md).
