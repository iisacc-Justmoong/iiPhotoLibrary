# File creation ownership in iiPhotoLibrary

Photo previews, originals and local staging use provider devices. The Albums target and Albums-only installed consumers remain independent of Qt and file-device adapters.

The common [ownership contract](../../iiFileProvider/docs/FILE_OWNERSHIP.md) defines stream, staging, control-file and
installed-package rules. Source regression tests remain with this module; the
workspace creation audit also checks these boundaries. Build/deployment and target
runtime validation are separate evidence.

Changed boundaries:


- `platform/android/src/com/iisacc/iiphotolibrary/PhotoLibrary.java`
- `src/PhotoLibraryApple.mm`
- `src/PhotoLibraryWindows.cpp`
- `src/PhotoStore.cpp`

The `Albums` target stays independent of Qt and provider adapters. Only the
legacy photo/UI target imports Qt file-device adapters.
