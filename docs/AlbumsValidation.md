# 앨범 관리자 검증 기록

## 2026-10-12: Images·Videos·Favorite·Deleted 추가

대상: macOS arm64, Release, C++23. 기존 스마트 앨범 네 종류를 유지하면서
이미지·비디오·Favorite·Deleted를 추가해 전체 여덟 종류를 검증했다.

| 검증 | 결과 |
| --- | --- |
| 전체 SDK 빌드 / CTest | 성공, 8/8 통과, 41.46초 |
| `SmartAlbums` | 171개 검사 통과 |
| 기존 `AllLibrary` | 62개 검사 통과 |
| 기존 `AlbumManager` / `GooglePhotosAlbums` | 각각 41개 / 39개 검사 통과 |
| `NativeAlbumBridge` | 33개 검사 통과, 미디어·즐겨찾기·삭제 표시 전달 포함 |
| 기존 `Photos` QtTest | 43 passed / 0 failed / 0 skipped |
| `AlbumsInstalledConsumer` | 재배치한 설치 패키지에서 여덟 스마트 앨범과 Favorite·Deleted API 링크·실행 성공, Qt 탐색 금지 |
| 전체 `InstalledConsumer` | 이미지·비디오 조회, Favorite 지정, Deleted 표시·복원과 기존 QML 실행 성공 |
| 코어 전용 빌드 / CTest | 성공, 5/5 통과, 2.48초 |

기존 네 종류의 열거형 값을 유지하고 새 네 종류를 뒤에 추가했다. 각 객체의 동일 인스턴스,
목록·ID 조회·읽기 전용 동작·수명·한도 제외를 여덟 객체 모두에 대해 검사했다.
미디어 메타데이터 갱신에 따른 이미지·비디오 소속 변경, 기존 `favorite` 필드의 활용,
즐겨찾기 지정·해제·조회와 서로 다른 앨범 참조의 일치를 검사했다.
논리 삭제 시 전체 보기 및 다른 스마트 앨범·중복 그룹에서의 제외, 숨김/삭제 겹침 시
Deleted 우선, 복원 후 기존 즐겨찾기·숨김·스크린샷·가져옴 상태의 유지도 검사했다.
Favorite·Deleted의 갱신 보존, 갱신 중 변경, 전송 실패 보존, 권한 회수 및 완전한 목록
제거 후 정보 정리, 여러 필드의 원자적 변경과 동시 조회를 확인했다.

구현 전 새 계약 테스트는 네 종류와 Favorite·Deleted API의 부재로 컴파일에 실패했고,
구현 후 전체 빌드와 회귀 테스트를 통과했다. Favorite 값과 삭제 표시는 native `Asset`
fixture에서 브리지로 전달했고, 설치 소비자에서 실제 SDK API를 호출했다.
Deleted는 SDK가 가진 항목의 논리 삭제 상태이며, 이 실행은 OS/원격 휴지통 조회·이동·복원이나
원본 파일 삭제를 수행하지 않는다. 앱 UI 및 실제 기기/사용자 계정 실행도 검증하지 않는다.

명령과 로그 위치는 아래 All Library 기록과 같다. 최신 패키지는
`cmake --install build --prefix build/install`로 Workspace 내부에 반영한다.
기존 `~/.local/SDK/iiPhotoLibrary` 및 실행 중인 제품 앱은 갱신하지 않는다.

## 2026-10-12: 스마트 앨범과 분류 API

대상: macOS arm64, Release, C++23. 전체 네이티브 계층은 Qt 6.8.3이며,
앨범 코어는 Qt와 iiSocietyContainer 탐색을 비활성화한 별도 구성에서도 검증했다.

| 검증 | 결과 |
| --- | --- |
| 전체 SDK 빌드 / CTest | 성공, 8/8 통과, 39.17초 |
| `SmartAlbums` | 93개 검사 통과 |
| 기존 `AllLibrary` | 62개 검사 통과 |
| 기존 `AlbumManager` / `GooglePhotosAlbums` | 각각 41개 / 39개 검사 통과 |
| `NativeAlbumBridge` | 26개 검사 통과, 분류 전달 및 SDK 리소스 이름의 가져옴 판정 포함 |
| 기존 `Photos` QtTest | 43 passed / 0 failed / 0 skipped |
| `AlbumsInstalledConsumer` | 재배치한 패키지에서 네 스마트 앨범·분류 API·중복 그룹·숨김/해제 링크 및 실행 성공, Qt 탐색 금지 |
| 전체 `InstalledConsumer` | 가져온 사진·숨김·All Library와 기존 네이티브/컨트롤러/QML 실행 성공 |
| 코어 전용 빌드 / CTest | 성공, 5/5 통과, 1.84초 |

스크린샷·가져온 사진·중복·숨김 객체의 동일 인스턴스 유지, 조회·읽기 전용 계약,
분류 지정/해제와 여러 필드의 원자적 변경, 숨김 항목의 가상 보기 제외 및 숨김 보기 조회,
콘텐츠 해시의 검증·정규화·해제와 중복 그룹 갱신을 검사했다.
동일 사진의 여러 앨범 소속을 중복으로 세지 않는 계약, 계정별 사진 객체 유지,
소스 해시 충돌의 미확정 처리, 미해시 사진의 불완전성, 갱신 중 분류 변경 및 후속 응답 반영,
부분 목록 보존, 전송 실패 보존, 권한 회수·완전한 제거·연결 해제 후 분류 정리,
동시 분류 변경과 객체 소멸 이후 조회를 검사했다.
구현 전 계약 테스트는 스마트 앨범·분류 타입/API의 부재로 컴파일에 실패했고,
구현 후 전체 빌드와 회귀 테스트가 통과했다.

Apple 네이티브 어댑터의 PhotoKit 스크린샷·숨김 메타데이터 읽기는 호스트 빌드에서 컴파일했다.
브리지의 분류 전달은 native `Asset` fixture와 SDK 가져옴 리소스 이름으로 실행 검증했다.
중복 해시는 fixture에서 공급한 SHA-256이며, 코어가 원본 파일을 검색·다운로드·해시 계산한다고
간주하지 않는다. 호스트는 기존 `PhotoStore::digestFile()` 등으로 확인한 원본 해시를 제공한다.
스마트 보기와 분류 API는 실제 OS 파일·사진 속성·원격 서비스를 변경하지 않는다.

빌드·테스트 명령과 로그 위치는 아래 All Library 기록과 동일하다.
최신 패키지는 `cmake --install build --prefix build/install`로 Workspace 내부에 반영한다.
실제 계정 인증·OS 숨김 앨범 접근·기기 실행·앱 UI 연결은 이번 검증에 포함하지 않으며,
기존 `~/.local/SDK/iiPhotoLibrary` 설치본 및 실행 중인 제품 앱은 갱신하지 않는다.

## 2026-10-12: All Library 전체 사진 앨범

대상: macOS arm64, Release, C++23. 전체 호환 계층은 Qt 6.8.3이며,
별도 앨범 코어 구성은 Qt와 iiSocietyContainer 탐색을 비활성화했다.

| 검증 | 결과 |
| --- | --- |
| 전체 SDK 빌드 | 성공 |
| 전체 CTest | 7/7 통과, 41.01초 |
| `AllLibrary` | 62개 검사 통과 |
| 기존 `AlbumManager` / `GooglePhotosAlbums` | 각각 41개 / 39개 검사 통과 |
| 기존 `NativeAlbumBridge` | 20개 검사 통과 |
| 기존 `Photos` QtTest | 43 passed / 0 failed / 0 skipped |
| `AlbumsInstalledConsumer` | 재배치한 패키지에서 All Library 조회·연결 해제 반영 성공, Qt/iiSocietyContainer 탐색 금지 |
| 기존 `InstalledConsumer` | All Library와 네이티브 어댑터·컨트롤러·설치된 QML 로드 성공 |
| 코어 전용 빌드 / CTest | 성공, 4/4 통과, 2.23초 |

`allLibrary()` 객체의 유지, 표시 이름, ID 조회, 빈 목록, 로컬·외부 앨범 합산,
같은 제공자·계정의 중복 제거, 다른 제공자·계정의 ID 격리, 원래 항목의 참조 메타데이터,
원본 사진 ID의 안정성, 목록 변경·재연결·연결 해제 반영을 검사했다.
미조회·부분 목록·조회 중·전송 실패·권한 회수에 따른 완전성, 캐시 보존, 취소 후 늦은 결과의
거부, 읽기 전용 오류, 가상 앨범의 한도 제외, 관리자 소멸 후 보유 핸들 및 동시 조회도 검사했다.
구현 전 새 테스트는 `allLibrary()`와 새 열거형의 부재로 컴파일에 실패했고,
구현 후 전체 빌드와 회귀 테스트를 통과했다.

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --build build/albums-only/build --parallel 4
ctest --test-dir build/albums-only/build --output-on-failure
cmake --install build --prefix build/install
```

로그는 `build/Testing/Temporary/LastTest.log` 및
`build/albums-only/build/Testing/Temporary/LastTest.log`에 있다.
전체 코어 소비자는 `build/albums-consumer-relocated`, 코어 전용 소비자는
`build/albums-only/build/albums-consumer-relocated` 패키지로 링크·실행했다.
전체 호환 소비자는 `build/consumer-relocated` 패키지를 사용했다.

집계는 관리자에 등록되어 이미 불러온 사진·영상의 현재 목록이다. 이 조회로 실제 OS 사진
검색이나 네트워크 요청을 실행하지 않는 계약을 fixture로 검증했다. 실제 계정 인증·동기화,
원본 다운로드와 제품 앱의 All Library UI는 이번 검증에 포함되지 않는다.
설치 검증과 패키지 반영은 Workspace의 `build/` 내부에서 수행하며,
`~/.local/SDK/iiPhotoLibrary`와 실행 중인 제품 앱은 갱신하지 않는다.

## 2026-10-02: 기존 앨범 관리자

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
