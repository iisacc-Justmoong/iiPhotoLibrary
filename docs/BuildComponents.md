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

The presentation bridge and its installed-package consumer use a CTest-run
C++ test executable with Qt Core event processing, without linking Qt Test.
The suite retains the four bridge lifetime and model-update cases; the test
support and the dependency contract are checked separately. All migrated photo
test targets run without Qt Test.

The separate Qt compatibility bridge and its installed consumer also use the
CTest test executable without linking Qt Test. The three existing cases retain
their bounded event-loop waits and preview-readiness checks.

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

The installed photo library records the SHA-256 fingerprint of the public
`iiFileProviderDevice.h` used to compile it, and places that provider's include
directory before transitive SDK includes. A consumer selecting a different
header is rejected during package discovery; rebuild and reinstall the photo
library with that consumer's iiFileProvider package to restore compatibility.
The Albums-only component does not use this file device and remains independent.

갤러리 모델 CTest는 단일 열·비지연 목록에 대해 인덱스, 삽입·삭제
신호와 행 수, 정렬 전후 영속 인덱스의 정체성, 초기화와 데이터 변경
범위를 독립적으로 확인한다. 잘못된 삽입·삭제와 영속 인덱스 갱신 누락을
주입한 테스트도 포함한다. 이는 일반 트리 모델과 지연 로딩을 포함하는
Qt 모델 검사기 전체의 동등한 대체는 아니며, 행 이동은 미지원 오류로
표시한다.

사진 검사 실행기는 여러 사례 이름과 데이터 행 선택, 기존 `-functions`
목록 조회, 신호 인자 기록 및 지연 삭제 처리를 지원한다. 이 동작은 Qt Test
없이 실행되는 작은 지원 검사에서 확인한다.

사진 서비스 40개 사례와 선택형 레거시 뷰 두 사례를 CTest 실행기로
유지한다. 연대순 갤러리의 모바일·데스크톱 데이터 행과 여러 이름을 한 번에
선택하는 기존 사용법도 보존한다. 터치 이벤트는 QtGui의 테스트 지원 경로를
사용하며, 해당 헤더의 `QTest` 이름공간은 남지만 Qt Test 라이브러리는
필요하지 않다. 레거시 연대순 검사는 배치가 반영된 끝 위치와 실제 스크롤
거리를 함께 확인한다. TLS와 실제 GUI를 사용하는 통합 사례는 별도 요청 시 실행한다.
