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

The default test configuration keeps the photo service, album, gallery-model,
and installed-library consumer checks, without requiring Qt Quick or LVRS.
The two historical QML gallery checks and their relocated QML consumer are
isolated behind `IIPHOTOLIBRARY_TEST_LEGACY_QML=ON`; enabling this adds those
checks without removing the default service tests. Their existing time limits
remain unchanged. On native macOS, the opt-in legacy tests prefer the Qt LVRS
package in the SDK macOS platform directory when `LVRS_DIR` is unspecified.
The opt-in test inventory compares both executables and confirms that the 40
service test functions are still present and only the two legacy view functions
are separated. The LVUI presentation's independent consumer tests remain a
separate integration check.

The installed-consumer test puts its disposable install inputs under
`resources/` and compilation output below `build/`. It redirects the private
installation manifest, preserving the project's existing install manifest,
and removes its own test workspaces after success or a reported command failure.

On 2026-11-15 the macOS default configuration built with LVRS and Qt Quick
package discovery disabled and passed all 21 tests. The opt-in legacy build
passed six focused tests, including both original gallery checks and the
relocated QML consumer. A missing iiSharedCanvas runtime search path initially
prevented the service test from starting; the test target now supplies the
selected shared-library directory, and the full default suite passed on retry.
No user application or system installation was run.
