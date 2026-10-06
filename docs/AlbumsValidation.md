# 앨범 관리자 검증 기록

검증일: 2026-10-02. 대상: macOS arm64, AppleClang 21, C++23, 전체 호환 계층은 Qt 6.8.3이다.

## 실행 결과

| 검증 | 결과 |
| --- | --- |
| 전체 SDK 빌드 | 성공, 네이티브 Apple 어댑터 및 새 앨범 어댑터 포함 |
| 전체 CTest | 6/6 통과, 40.49초 |
| `AlbumManager` | 41개 검사 통과 |
| `GooglePhotosAlbums` | 39개 검사 통과 |
| `NativeAlbumBridge` | 20개 검사 통과 |
| 기존 `Photos` QtTest | 34 passed / 0 failed / 0 skipped |
| `AlbumsInstalledConsumer` | 별도 prefix 설치·재배치 후 Qt/iiSocietyContainer 탐색을 금지한 외부 프로젝트 링크·실행 성공 |
| 기존 `InstalledConsumer` | 새 관리자·네이티브 어댑터 구성, 컨트롤러 및 설치된 QML 모듈 로드 성공 |
| 코어 전용 빌드 | 성공, Qt·iiSocietyContainer 의존성 없음 |
| 코어 전용 CTest | 3/3 통과, 1.17초, 재배치 설치 소비자 포함 |

전체 빌드는 기존 `build/` 구성에서 수행했다. 별도 코어 빌드는 다음과 같이 수행했다.

```sh
cmake -S . -B build
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure

cmake -S . -B build/albums-only/build \
  -DIIPHOTOLIBRARY_ALBUMS_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE \
  -DCMAKE_DISABLE_FIND_PACKAGE_iiSocietyContainer=TRUE
cmake --build build/albums-only/build --parallel 4
ctest --test-dir build/albums-only/build --output-on-failure
```

원본 로그는 `build/Testing/Temporary/LastTest.log` 및
`build/albums-only/build/Testing/Temporary/LastTest.log`에 있다.
주요 산출물은 `build/libiiPhotoLibraryAlbums.a`, `build/libiiPhotoLibrary.a`이다.
검증용 패키지는 `cmake --install build --prefix build/install`로 Workspace 내부에 설치한다.
전체 소비자와 코어 소비자는 각각 `build/consumer-relocated`, `build/albums-consumer-relocated`를 사용한다.

## 확인한 동작과 경계

서비스·계정·원격 앨범별 실제 객체 분리, 길이를 포함한 ID 충돌 방지, 로컬 목록 수정,
외부 수신, 갱신 시 객체 유지, 재연결 시 이전 객체 정리, 늦은 결과 거부, 동시 갱신 거부,
취소 전달, 권한 회수, 불완전 목록 병합, 완전 목록 교체, 한도 초과 시 목록 보존을 검사했다.
Google 검사는 현재 Picker 및 앱 생성 항목의 응답 fixture, 페이지네이션, URL 인코딩,
중복 ID·반복 토큰·본문/깊이/항목/페이지 한도, 잘못된 응답, HTTP 인증·권한·요청 제한,
토큰 헤더 검사와 upstream 비밀 정보의 오류 메시지 비노출을 포함한다.

실제 Apple/Google 계정 로그인·PhotoKit 권한 승인·iCloud 사진 다운로드·Google 미디어 다운로드는
이번 실행에서 검증하지 않았다. 로그인 UI, OAuth 토큰 관리, Picker 선택 UI, TLS 전송은 호스트가
제공하는 계약이다. Google 전체 개인 보관함 자동 조회를 지원한다고 간주하지 않는다.
iCloud 어댑터는 iCloud 동기화 항목을 포함할 수 있는 시스템 사진 보관함을 읽는다.
실제 사진 등록·삭제 작업은 수행하지 않았다. Android/Windows/iOS 타깃 빌드·기기 검증은 별도이다.
기존 `~/.local/SDK/iiPhotoLibrary` 설치본이나 제품 앱 실행본은 이 검증용 패키지와 구분한다.
