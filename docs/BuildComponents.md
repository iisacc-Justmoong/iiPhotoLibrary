# Dependency components

Albums is the existing Qt-free C++23 library. Its identifiers, album registry,
smart collections and provider-independent values do not depend on the legacy
photo controller.

The legacy photo controller and native photo-library adapters still use Qt Core
and Gui. The serialized photo worker uses a native thread rather than Qt
Concurrent. Clipboard/sharing actions consume
`iiSocietyContainer::Sharing` rather than the complete `Gui` target. Indexing,
original transfer, and preview processing therefore no longer pull in the
container's Qt Quick image provider or application/window helpers.

The photo store format and native system sharing behavior remain unchanged.
`PhotoController::shutdownAsync()` is an explicit final-shutdown operation:
it cancels queued requests and invalidates callbacks without waiting for an
active platform call. The controller cannot be reused after retirement. Active
operations retain their store until they return; ordinary destruction and
`waitForDone()` retain their synchronous completion contract for callers that
need a durable handoff. This component split does not claim removal of the
remaining Qt image-codec and event-loop adapters.

Photo test containers are created beneath `resources/` and removed by the test fixture. Installed-consumer checks use unique temporary build directories and remove them after completion or a reported command failure.

On native macOS, if `LVRS_DIR` is not specified, the photo tests prefer the Qt LVRS package in the SDK macOS platform directory. Set `LVRS_DIR` explicitly for a different Qt LVRS installation.
