# Dependency components

Albums is the existing Qt-free C++23 library. Its identifiers, album registry,
smart collections and provider-independent values do not depend on the legacy
photo controller.

The legacy photo controller and native photo-library adapters still use Qt Core,
Gui and Concurrent. Clipboard/sharing actions now consume
`iiSocietyContainer::Sharing` rather than the complete `Gui` target. Indexing,
original transfer, and preview processing therefore no longer pull in the
container's Qt Quick image provider or application/window helpers.

The public photo controller API, photo store format and native system sharing
behavior remain unchanged. This component split does not claim removal of the
remaining Qt image-codec and event-loop adapters.
