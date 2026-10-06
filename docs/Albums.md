# 앨범 관리자와 외부 사진 연결

`AlbumManager`는 로컬 앨범과 연결된 사진 서비스를 각각 독립된 `std::shared_ptr<Album>`으로
인스턴스화한다. iCloud 사진과 Google Photos에 연결하면 두 개의 별도 앨범 객체가 생긴다.
동일 제공자의 계정이 다르거나 외부에서 받은 앨범 ID가 다르면 역시 별도 객체이다.

## 객체와 수명

| 객체 | 역할 |
| --- | --- |
| `AlbumManager` | 생성·연결·조회·갱신·외부 수신·연결 해제 |
| `Album` | 연결별 수명, `snapshot()`으로 동기화된 데이터 복사본 제공 |
| `AlbumConnection` | 제공자·계정·원격 앨범 ID·표시 이름·종류 |
| `AlbumSnapshot` | 사진 메타데이터·완전성·리비전 |
| `AlbumSource` | 인증된 사진 연결을 읽는 교체 가능한 인터페이스 |
| `GooglePhotosAlbumSource` | 현재 Google Photos Picker 또는 앱 생성 항목 REST 조회 |
| `photoLibraryAlbumSource()` | 기존 네이티브 `PhotoLibrary`를 앨범 소스로 연결 |

앨범 식별자는 `providerId`, `accountId`, `remoteAlbumId`의 길이를 포함하여 구성한다.
표시 이름이 같아도 연결이 다르면 식별자와 객체가 다르다. 계정 ID에는 인증 계층에서 확인한
안정된 비밀이 아닌 ID를 사용한다. 토큰·비밀번호를 계정 ID나 메타데이터에 넣지 않는다.
기본 `remoteAlbumId`인 `library`는 연결의 루트 앨범이다. `AlbumPhoto::sourcePhotoId`는
제공자의 원본 ID이고, 관리자가 부여하는 `id`는 앨범 범위를 포함하므로 서로 충돌하지 않는다.

`connect()`는 객체만 만들며 권한·네트워크 요청을 수행하지 않는다.
호스트가 로그인·권한을 확보한 뒤 작업 스레드에서 `refresh()`를 호출한다.
갱신은 같은 인스턴스를 유지한다. 동일 연결을 재연결하면 새 객체를 만들고 이전 객체는
`Disconnected`가 되며 목록이 비워진다. `disconnect()`와 관리자 소멸도 목록을 정리한다.
이전 핸들로 반영하는 수신 데이터나 늦게 끝난 조회는 새 연결을 덮어쓰지 못한다.

## 네이티브 사진과 외부 수신

```cpp
#include <iiPhotoLibrary/iiPhotoLibrary.h>
using namespace iiPhotoLibrary;
AlbumManager albums;
auto local = albums.createLocal("personal", "Personal");
auto native = nativePhotoLibrary();
// Apple에서는 GUI 스레드에서 authorize()를 호출하고 권한 완료 후 연결한다.
auto iCloud = albums.connect(
    {"icloud", "system-photo-library", "iCloud Photos", "library", AlbumOrigin::ICloud},
    photoLibraryAlbumSource(native, "icloud"));
albums.refresh(iCloud); // 작업 스레드
auto visiblePhotos = iCloud->snapshot();
```

Apple 어댑터는 OS에 로그인된 계정의 **시스템 사진 보관함**을 읽는다.
[Apple PhotoKit 문서](https://developer.apple.com/documentation/photokit)의 범위에 따라 로컬 항목과 iCloud
동기화 항목이 함께 보일 수 있으므로 `SystemPhotoLibrary` 범위를 사용한다. PhotoKit은
임의의 Apple 계정에 별도 웹 로그인하는 인터페이스가 아니다. iCloud 동기화 사진의 실제
접근은 OS 계정·권한에 달려 있다. Android/Windows 시스템 사진 연결에는 `device` 제공자를 쓴다.

기존 `PhotoLibrary`는 프리뷰·원본 추출·등록·삭제를 계속 소유한다. 앨범은 메타데이터와
참조만 관리하며 원본을 복사하지 않는다. 네이티브 원본 작업에는 `sourcePhotoId`를 전달한다.
리소스 토큰은 앨범 메타데이터로 내보내지 않고 느린 `resolveIdentifiers()`를 추가 호출하지 않는다.
같은 네이티브 인스턴스를 별도의 활성 `PhotoStore` 작업자와 공유하지 않는다.

외부에서 받은 앨범에도 동일한 객체 구조를 적용한다.

```cpp
auto received = albums.connect({"remote", "peer-account", "Travel", "album-42"});
AlbumPhoto photo;
photo.sourcePhotoId = "remote-photo-1";
photo.name = "travel.jpg";
albums.publish(received, {{photo}, true, "revision-1"});
// 다른 remoteAlbumId는 별도 connect()로 독립 인스턴스를 만든다.
albums.setLocalPhotos(local, {photo});
```

## Google Photos와 호스트 계약

Google Photos는 2025-03-31부터 Library API의 전체 개인 보관함 조회 범위를 제거했다.
공식 [API 변경 사항](https://developers.google.com/photos/support/updates)에 따라 두 모드를 지원한다.

| 모드 | 조회 범위 | OAuth scope |
| --- | --- | --- |
| `PickerSelection` (기본) | 사용자가 Picker에서 선택한 사진·영상 | `https://www.googleapis.com/auth/photospicker.mediaitems.readonly` |
| `AppCreatedItems` | 해당 앱이 Google Photos에 생성한 항목 | `https://www.googleapis.com/auth/photoslibrary.readonly.appcreateddata` |

```cpp
#include <iiPhotoLibrary/GooglePhotosAlbumSource.h>
// 아래 세 콜백과 authenticatedAccountId는 호스트가 제공한다.
auto source = std::make_shared<GooglePhotosAlbumSource>(
    currentAccessToken, httpsGet, completedPickerSessionId);
auto google = albums.connect(
    {"googlephotos", authenticatedAccountId, "Google Photos", "library", AlbumOrigin::GooglePhotos}, source);
albums.refresh(google); // 작업 스레드
```

호스트는 OAuth UI·토큰 저장/갱신·HTTPS GET과 Picker 세션 생성·사용자 선택을 담당한다.
SDK는 세션 완료 여부와 페이지별 목록을 조회한다. 선택이 완료되지 않으면 `SelectionRequired`이다.
로그인만으로 전체 보관함을 자동 조회하지 않는다. 토큰 공급자는 요청마다 호출한다.

`AlbumHttpGet`은 요청 URL·헤더·`stop_token`을 처리하며 TLS 인증서/호스트를 검증해야 한다.
Authorization을 로그에 남기거나 리디렉션으로 다른 호스트에 전달하지 않는다. SDK의 API 주소는
고정된 Google HTTPS 주소이다. 전송 계층은 수신 중 본문을 16 MiB 이하로 제한해야 한다.
SDK는 반환된 본문의 크기를 다시 검사하고 upstream 본문/예외를 오류 메시지로 노출하지 않는다.

[`sessions.get`](https://developers.google.com/photos/picker/reference/rest/v1/sessions/get),
[`Picker mediaItems.list`](https://developers.google.com/photos/picker/reference/rest/v1/mediaItems/list),
[`PickedMediaItem`](https://developers.google.com/photos/picker/reference/rest/v1/mediaItems),
[`Library mediaItems.list`](https://developers.google.com/photos/library/reference/rest/v1/mediaItems/list)
응답 형식을 사용한다. `contentUrl`은 일시적인 URL이며 원본 다운로드 완료나 영구 소유를
뜻하지 않는다. 실제 콘텐츠 접근은 호스트가 수행하고 새 조회로 URL을 갱신한다.

## 완전성·오류·동시 실행

- `complete=true`이면 선언된 범위의 목록을 교체한다. 앱 생성 항목의 완전한 목록도
  전체 Google Photos 보관함의 완전한 목록을 뜻하지 않는다.
- `complete=false`이면 ID별로 병합한다. 제한된 PhotoKit 권한·새 Picker 선택에서
  보이지 않는 사진을 삭제로 해석하지 않는다. Picker 선택은 여러 세션의 선택을 누적한다.
- 전송 실패·요청 제한·잘못된 응답·한도 초과에서는 기존 목록을 유지한다.
  인증/권한 거부에서는 해당 앨범만 비우고 `AuthenticationRequired` 상태로 전환한다.
- 한 앨범의 동시 갱신과 갱신 중 `publish()`는 `Busy`로 거부한다.
  다른 앨범은 독립적으로 갱신할 수 있다. 가변 소스/콜백은 연결별로 소유하며 여러 연결에서
  같은 소스나 네이티브 인스턴스를 동시 공유하지 않는다. 재연결 시 새 소스를 생성한다.
- 재연결·연결 해제는 이전 작업에 취소를 요청한다. 네이티브 어댑터는 `cancel()`을 호출하고
  HTTPS 계층은 `stop_token`을 처리한다. 늦은 결과는 `StaleConnection`으로 거부한다.
  관리자 소멸 전에 진행 중인 `refresh()`를 종료·join한다.
- 기본 한도는 관리자당 앨범 1,000개, 앨범당 사진 100,000개이다. Google 조회는 최대
  1,000페이지, 응답당 16 MiB, JSON 깊이 32이다. 페이지/사진 한도는 설정할 수 있다.
  중복 사진 ID·반복 페이지 토큰·잘못된 메타데이터는 부분 반영하지 않는다.

## Qt 없는 코어와 검증

관리자와 Google 소스는 C++23 표준 타입만 공개하며 `iiPhotoLibrary::Albums`에 포함한다.
네이티브 어댑터·기존 사진 저장/동기화·QML 계층은 기존 Qt 의존성을 유지한다.

```cmake
find_package(iiPhotoLibrary 0.1 CONFIG REQUIRED COMPONENTS Albums)
target_link_libraries(MyApp PRIVATE iiPhotoLibrary::Albums)
```

코어만 구성하려면 `cmake -S . -B build -DIIPHOTOLIBRARY_ALBUMS_ONLY=ON`을 사용한다.
전체 SDK 설치에서도 `COMPONENTS Albums` 소비자는 Qt/iiSocietyContainer를 찾지 않는다.
코어 전용 설치에는 `Album.h`와 `GooglePhotosAlbumSource.h`를 사용한다.
기존 umbrella와 네이티브 어댑터는 기본 전체 패키지를 사용한다.

`AlbumManager`, `GooglePhotosAlbums`, `NativeAlbumBridge` 테스트는 객체 분리, 외부 수신,
불완전 목록 보존, 재연결·동시 실행·취소, 권한 회수, Google 응답·페이지·오류 처리를 검증한다.
`AlbumsInstalledConsumer`는 설치 prefix를 옮긴 뒤 Qt와 iiSocietyContainer 탐색을 금지한
외부 프로젝트에서 코어를 링크·실행한다. `InstalledConsumer`는 네이티브 어댑터 구성과
설치된 QML 로딩을 검증한다. 실제 사용자 계정 로그인·iCloud 다운로드·Google 사진 다운로드는
fixture 기반 검증에 포함하지 않는다. 실제 사진을 등록하거나 삭제하지 않는다.

최종 빌드·실행 결과는 [앨범 관리자 검증 기록](AlbumsValidation.md)에 기록한다.
