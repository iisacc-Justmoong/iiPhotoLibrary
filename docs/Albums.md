# 앨범 관리자와 외부 사진 연결

`AlbumManager`는 로컬 앨범과 연결된 사진 서비스를 각각 독립된 `std::shared_ptr<Album>`으로
인스턴스화한다. iCloud 사진과 Google Photos에 연결하면 두 개의 별도 앨범 객체가 생긴다.
동일 제공자의 계정이 다르거나 외부에서 받은 앨범 ID가 다르면 역시 별도 객체이다.

## 객체와 수명

| 객체 | 역할 |
| --- | --- |
| `AlbumManager` | 생성·연결·조회·갱신·외부 수신·연결 해제 |
| `Album` | 연결별 수명, `snapshot()`으로 동기화된 데이터 복사본 제공 |
| `AlbumManager::allLibrary()` | 전체 앨범의 현재 사진을 모아 반환하는 읽기 전용 `All Library` 앨범 |
| `AlbumManager::smartAlbum()` | 스크린샷·가져온 사진·중복·숨김·이미지·비디오·Favorite·Deleted의 읽기 전용 앨범 |
| `AlbumPhotoTraits` / `AlbumPhotoTraitsUpdate` | 사진의 분류 정보 및 변경할 필드만 담은 갱신 값 |
| `DuplicatePhotoGroup` | 동일 콘텐츠 해시를 가진 서로 다른 사진들의 묶음 |
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

## All Library 전체 사진 앨범

관리자는 생성될 때 표시 이름이 `All Library`인 읽기 전용 `Album` 객체를 하나 만든다.
`allLibrary()`는 항상 같은 객체를 반환하며 `album(all->id())`로도 조회할 수 있다.
`AlbumOrigin::AllLibrary`와 `AlbumCoverage::AllLibrary`로 일반 앨범과 구분한다.
기존 `albums()`는 일반 앨범 목록을 유지한다. UI에서 전체 사진 항목이 필요하면
`allLibrary()` 객체를 그 목록 앞에 배치한다.

```cpp
#include <iiPhotoLibrary/Album.h>
using namespace iiPhotoLibrary;
AlbumManager albums;
auto all = albums.allLibrary();
auto travel = albums.createLocal("travel", "Travel");
auto work = albums.createLocal("work", "Work");
AlbumPhoto photo;
photo.sourcePhotoId = "photo-1";
photo.name = "travel.jpg";
albums.setLocalPhotos(travel, {photo});
albums.setLocalPhotos(work, {photo});
auto contents = all->snapshot(); // 동일 사진은 한 번만 반환한다.
albums.disconnect(travel);
auto remaining = all->snapshot(); // Work에 남은 사진은 계속 반환한다.
```

`snapshot()`은 관리자의 현재 일반 앨범 목록과 사진 상태를 한 번의 관리자 잠금 아래에서
읽어 새 소유 복사본을 반환한다. 앨범 생성·목록 변경·갱신·재연결·연결 해제는 다음 조회에
즉시 반영되며 이전에 받은 복사본은 유지된다. 네트워크 요청이나 OS 사진 검색을 실행하지
않으므로 외부 앨범은 호스트가 기존 `refresh()` 또는 `publish()`로 불러온 사진만 포함한다.
각 소스에 선언된 접근 범위와 권한을 합치며, 미조회 사진이나 접근할 수 없는 사진을 포함한다고
간주하지 않는다. 사진과 영상 모두 기존 `AlbumPhoto` 항목으로 반환한다.
삭제 표시된 항목은 `Deleted`에서 조회한다. 그 밖의 숨김 항목은 `Hidden`에서 조회하며,
두 상태 모두 `All Library`에서 제외한다.

중복 판단은 `providerId`, `accountId`, `sourcePhotoId`로 한다. 같은 계정의 동일 사진이
여러 원격 앨범에 있어도 한 번만 반환하고, 다른 서비스·계정의 동일 원본 ID는 구분한다.
로컬 앨범들은 같은 `local` 제공자와 빈 계정 ID를 사용하므로 동일 `sourcePhotoId`를 공유하면
같은 사진으로 취급한다. 전체 사진 ID는 이 세 항목의 길이를 포함해 생성하므로 중복 사진이
포함된 앨범 하나를 제거해도 나머지 앨범에 남은 사진 ID는 유지된다.
동일 사진의 스크린샷·가져옴·숨김·삭제·즐겨찾기 표시는 각 참조의 양수 표시를 합친다. 명시적인 분류 API로
변경한 필드는 모든 참조에 반영한다. 그 밖의 메타데이터는 앨범 ID의 사전순으로 가장 앞선
앨범에서 가져오며, 결과 목록은
제공자·계정·원본 사진 ID 순서이다. 날짜순 표시가 필요하면 소비자가 정렬한다.

결과 사진의 `properties`에는 원래 항목을 다시 조회할 수 있도록 예약된 네 키를 기록한다.
원본 메타데이터에 같은 키가 있어도 관리자가 확인한 참조로 덮어쓴다.

| 키 | 값 |
| --- | --- |
| `allLibrary.providerId` | 원래 제공자 ID |
| `allLibrary.accountId` | 원래 계정 ID |
| `allLibrary.albumId` | 메타데이터를 제공한 일반 앨범의 ID |
| `allLibrary.photoId` | 그 일반 앨범에서 부여한 사진 ID |

`All Library`의 `Ready` 상태는 현재 모은 목록을 읽을 수 있다는 뜻이다.
`contents.complete`는 모든 일반 앨범이 `Ready`이며 각 목록도 완전할 때만 `true`이다.
앨범이 없으면 빈 목록이 완전하다. 조회 중·미조회·실패·권한 회수·부분 목록이 하나라도 있으면
`false`이며, 각 앨범에 남아 있는 사진은 계속 반환한다. 개별 오류는 원래 앨범에서 조회한다.
집계 `revision`은 비어 있으며 원격 제공자의 리비전을 대신하지 않는다.

`publish()`, `setLocalPhotos()`, `refresh()`로 이 객체를 변경하려 하면 `ReadOnly` 오류이다.
원래 앨범을 갱신한 뒤 `all->snapshot()`을 호출한다. `disconnect(all)`은 `false`를 반환하며
객체를 제거하지 않는다. 관리자가 소멸하면 보유 중인 전체 앨범도 `Disconnected`가 되고
빈 불완전 목록을 반환한다. 객체는 관리자의 수명을 연장하지 않는다.
이 가상 앨범은 `maximumAlbums`에 포함되지 않으며 전체 결과를 `maximumPhotos`로 자르지 않는다.
기존 한도는 일반 앨범마다 적용되고 집계는 조회할 때만 계산한다.

## 스마트 앨범과 분류 API

관리자는 여덟 개의 고정된 읽기 전용 `Album` 객체를 함께 만든다.
`smartAlbum(kind)`와 `smartAlbums()`는 기존 객체를 반환하며 `album(id)` 조회도 지원한다.
일반 `albums()` 목록 및 앨범 개수 한도에는 포함하지 않는다.
`AlbumData::smartKind`, `AlbumOrigin::Smart`, `AlbumCoverage::SmartLibrary`로 구분한다.

| 종류 | 표시 이름 | 포함 조건 | 분류 API |
| --- | --- | --- | --- |
| `SmartAlbumKind::Screenshots` | Screenshots | `traits.screenshot=true`인 사진 | `setScreenshot(photoId, bool)` |
| `SmartAlbumKind::Imported` | Imported Photos | `traits.imported=true`인 사진·영상 | `setImported(photoId, bool)` |
| `SmartAlbumKind::Duplicates` | Duplicates | 같은 콘텐츠 SHA-256을 가진 서로 다른 사진·영상이 두 개 이상 | `setContentHash(photoId, sha256)`, `duplicateGroups()` |
| `SmartAlbumKind::Hidden` | Hidden | `traits.hidden=true`인 사진·영상 | `setHidden(photoId, bool)` |
| `SmartAlbumKind::Images` | Images | `mediaType="photo"`인 항목 | `setLocalPhotos()` / `publish()` / `refresh()`의 미디어 메타데이터 |
| `SmartAlbumKind::Videos` | Videos | `mediaType="video"`인 항목 | `setLocalPhotos()` / `publish()` / `refresh()`의 미디어 메타데이터 |
| `SmartAlbumKind::Favorite` | Favorite | 기존 `favorite=true`인 사진·영상 | `setFavorite(photoId, bool)`, `isFavorite(photoId)` |
| `SmartAlbumKind::Deleted` | Deleted | `traits.deleted=true`인 사진·영상 | `setDeleted(photoId, bool)`, `photoTraits(photoId).deleted` |

삭제 표시된 항목은 `Deleted`에만 포함하며, 숨김 표시도 있으면 삭제 보기를 우선한다.
그 밖의 숨김 항목은 `Hidden`에만 포함한다. 두 상태는 `All Library` 및 나머지 스마트 앨범과
중복 그룹에서 제외한다. 일반 앨범의 `snapshot()`은 기존 구성원을 유지하며
각 항목의 `traits.hidden`, `traits.deleted`, 기존 `favorite` 값을 제공한다.
스마트 앨범들의 사진 ID는 전체 보기의 제공자·계정·원본 ID 기반 식별자를 공유한다.
분류 API는 일반 앨범 또는 가상 앨범에서 받은 `AlbumPhoto::id`를 모두 처리하며,
`sourcePhotoId`만 전달하지 않는다. 알려지지 않은 사진 ID는 `NotFound` 오류이다.

```cpp
auto screenshots = albums.smartAlbum(SmartAlbumKind::Screenshots);
auto imported = albums.smartAlbum(SmartAlbumKind::Imported);
auto duplicates = albums.smartAlbum(SmartAlbumKind::Duplicates);
auto hidden = albums.smartAlbum(SmartAlbumKind::Hidden);
auto images = albums.smartAlbum(SmartAlbumKind::Images);
auto videos = albums.smartAlbum(SmartAlbumKind::Videos);
auto favorite = albums.smartAlbum(SmartAlbumKind::Favorite);
auto deleted = albums.smartAlbum(SmartAlbumKind::Deleted);
// photoId는 일반 앨범 또는 가상 앨범의 snapshot()에서 받은 사진의 id이다.
albums.setScreenshot(photoId, true);
albums.setImported(photoId, true);
albums.setHidden(photoId, true);
auto hiddenItems = hidden->snapshot();
albums.setHidden(hiddenItems.contents.photos.front().id, false);
// 신뢰할 수 있는 원본 전체 바이트의 SHA-256을 호스트가 제공한다.
albums.setContentHash(photoId, originalSha256);
auto groups = albums.duplicateGroups();
auto traits = albums.photoTraits(photoId);
albums.setPhotoTraits(photoId, {.screenshot = false, .imported = true});
albums.setFavorite(photoId, true);
bool marked = albums.isFavorite(photoId);
albums.setDeleted(photoId, true);
auto deletedItems = deleted->snapshot();
albums.setDeleted(deletedItems.contents.photos.front().id, false); // 기존 분류에 따라 복원한다.
albums.setPhotoTraits(photoId, {.favorite = false, .deleted = false});
```

`AlbumPhoto::traits`를 초기 목록에 포함해 분류를 공급할 수도 있다. 기본 표시는 모두 `false`,
콘텐츠 해시는 빈 문자열이다. `setPhotoTraits()`는 지정한 필드만 한 번의 관리자 잠금에서
검증하고 변경한다. 비디오를 스크린샷으로 표시하거나 잘못된 해시를 제공하면
`InvalidSnapshot` 오류이며 기존 분류를 유지한다. 동시에 갱신 중인 캐시 사진의 분류도
변경할 수 있고, 뒤늦게 도착한 성공 응답에 해당 변경을 다시 적용한다.

이미지·비디오 분류는 기존 미디어 종류를 읽으며 파일 형식을 바꾸는 API를 추가하지 않는다.
소스가 새 목록에서 종류를 갱신하면 두 보기의 소속도 반영된다.
Favorite은 `AlbumPhoto::favorite`를 계속 사용한다. `setPhotoTraits()`의 선택적 `favorite`
필드로 다른 분류와 동시에 변경할 수 있으며, `AlbumPhotoTraits`에 별도 즐겨찾기 값을 복제하지 않는다.
즐겨찾기 지정·해제는 모든 참조와 후속 소스 갱신에 반영한다.

Deleted는 SDK의 논리 삭제 보기이다. `setDeleted(id, true)`는 목록에 남은 항목에 삭제 표시를
설정하고, `setDeleted(id, false)`는 이를 해제한다. 기존 즐겨찾기·숨김·스크린샷·가져옴·해시를
보존하므로 복원하면 해당 상태에 맞는 보기에 다시 나타난다. 숨긴 항목을 복원하면 계속 Hidden에
남는다. 원본 파일 삭제, OS/원격 휴지통 이동·복원 또는 기간에 따른 자동 제거는 수행하지 않는다.
소스가 아직 제공하지 않은 OS/원격 휴지통 항목을 자동으로 가져오는 기능도 포함하지 않는다.

명시적으로 변경한 분류는 같은 제공자·계정·원본 사진 ID를 가진 모든 참조에 적용되며
일반 앨범의 후속 `refresh()`에서도 유지된다. 지정하지 않은 필드는 소스 값을 따른다.
불완전 목록에서 기존 항목이 유지되면 분류도 유지한다. 사진의 마지막 참조가 완전한 목록
교체·연결 해제·권한 회수로 제거되면 관련 변경 정보도 지운다. 관리자 소멸 이후에는
각 스마트 앨범이 `Disconnected`와 빈 불완전 목록을 반환한다. SDK 분류 정보의 보관은
이 관리자 수명 내이며, 앱 재시작 시의 지속 보관은 호스트가 맡는다.
분류 API는 OS 사진의 숨김·즐겨찾기·삭제 상태, 원본 파일 및 원격 서비스 데이터를 변경하지 않는다.

중복은 전체 원본 바이트의 SHA-256을 기준으로 하는 정확한 콘텐츠 일치이다.
같은 사진이 여러 일반 앨범에 속하는 것은 한 사진으로 취급한다. 다른 제공자·계정의 사진은
별도 객체로 유지하되 호스트가 제공한 해시가 같으면 같은 중복 그룹에 들어간다.
해시는 64자리 16진수만 허용하며 소문자로 정규화한다. 빈 문자열로 해시를 지울 수 있다.
동일 사진의 소스 참조끼리 해시가 충돌하면 집계 해시는 미확정 상태로 비우고 중복 후보에서
제외한다. 명시적인 해시 API로 일관된 값을 지정할 수 있다.
`duplicateGroups()`는 숨김과 삭제 표시를 제외한 확인된 중복 그룹만 반환한다. 중복 앨범의 `complete`는
일반 앨범들이 완전하고 모든 표시 가능한 사진의 해시가 알려졌을 때만 `true`이다.
다른 스마트 앨범은 소스 목록의 완전성을 따른다. 유사 이미지 판정은 수행하지 않는다.

앨범 코어는 파일을 읽거나 해시를 계산하지 않는다. 전체 SDK를 쓰는 호스트는 기존
`PhotoStore::digestFile(path)` 결과를 `setContentHash()`에 제공할 수 있다. 원본 한 파일의
해시를 사용하는 계약이며 여러 리소스의 조합을 원본 전체의 해시로 오인하지 않는다.
Apple 어댑터는 [PhotoKit 미디어 하위 종류](https://developer.apple.com/documentation/photos/phassetmediasubtype)와
[숨김 메타데이터](https://developer.apple.com/documentation/photos/phasset/ishidden)를 읽는다.
숨김 항목의 실제 접근 범위는 OS 권한과 숨김 앨범 잠금 설정에 따른다.
네이티브 브리지는 `Asset`의 스크린샷·가져옴·숨김·삭제 표시와 기존 즐겨찾기 값,
SDK가 생성한 `Society-...` 리소스
이름의 가져옴 표시를 전달한다. 다른 제공자의 분류는 호스트가 목록 또는 분류 API로 공급한다.
스마트 앨범 조회는 새로운 OS 검색·권한 요청·네트워크 요청·원본 다운로드를 실행하지 않는다.

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

`AllLibrary`는 중복 제거, 계정·서비스 격리, 실시간 목록 반영, 완전성·오류·취소·객체 수명·동시 소멸과
읽기 전용 계약을 검증한다. `AlbumManager`, `GooglePhotosAlbums`, `NativeAlbumBridge` 테스트는 객체 분리, 외부 수신,
불완전 목록 보존, 재연결·동시 실행·취소, 권한 회수, Google 응답·페이지·오류 처리를 검증한다.
`AlbumsInstalledConsumer`는 설치 prefix를 옮긴 뒤 Qt와 iiSocietyContainer 탐색을 금지한
외부 프로젝트에서 코어를 링크·실행한다. `InstalledConsumer`는 네이티브 어댑터 구성과
설치된 QML 로딩을 검증한다. 실제 사용자 계정 로그인·iCloud 다운로드·Google 사진 다운로드는
fixture 기반 검증에 포함하지 않는다. 실제 사진을 등록하거나 삭제하지 않는다.
`SmartAlbums`는 분류·숨김/해제·해시 그룹·충돌·갱신 보존·분류 정리·동시 변경을 검증하며,
미디어 분류, 즐겨찾기 지정·해제, 삭제·복원과 상태 유지도 검사한다.
설치 소비자는 여덟 스마트 앨범과 분류 API를 실제 설치 패키지에서 링크·실행한다.

최종 빌드·실행 결과는 [앨범 관리자 검증 기록](AlbumsValidation.md)에 기록한다.
