# iiPhotoLibrary

Society의 Photos 및 기기 사진 보관함 동기화 구현을 독립 패키지로 추출한 C++23 SDK이다.
최초 추출에서는 Society의 소스·빌드 연결을 유지했다. 이후 Society는 이 패키지를 소비하도록 전환했으며 기존 앱 내부 구현은 참조용으로 보존한다. SDK는 Society 앱 소스나 바이너리를 참조하지 않으며
`iiPhotoLibrary` 네임스페이스와 `iiPhotoLibrary::iiPhotoLibrary` CMake 타깃을 제공한다.

## 범위와 의존성

- `PhotoLibrary`: 네이티브 접근 권한, 조회, 프리뷰, 원본 추출, 등록, 휴지통 인터페이스.
- `PhotoStore`: 영구 갤러리 인덱스, 가시 영역 우선 프리뷰, alias, 원본 캐시·임대,
  청크 이어받기·SHA-256 검증, 제한된 접근 및 불완전 조회의 삭제 방지.
- `PhotoController`: QML 모델, 작업 스레드·취소, 인증 피어 및 컨테이너 검사,
  업로드·선택적 원본 다운로드. `Sender` 콜백으로 전송 계층을 주입한다.
- `PhotosView`: LVRS 기반 화면과 GalleryTile/GalleryInfo/GalleryZoom.
- Apple PhotoKit, Android MediaStore, Windows Shell 어댑터 및 미지원 플랫폼의 저장소 전용 어댑터.

이번 0.1 추출은 기존 Qt 구현을 보존한다. **Qt 없는 표준 C++ SDK로의 전환은 완료하지 않았다.**
Qt 6.8.3 Core/Gui/Concurrent 및 iiSocietyContainer 0.14 Gui가 필요하며, Qt 타입이 공개 API에도
남아 있다. GUI의 이미지 처리·QML 모델뿐 아니라 저장·동기화 코드에도 기존 Qt 의존성이 있다.
기능 재작성 없이 분리한 호환 패키지이며, 이 의존성을 순수 C++ 코어로 오인해서는 안 된다.
QML을 사용할 때 LVRS, Qt Quick/QuickControls2/Dialogs가 필요하다.
`iiSocietySync`와 `iiServerHost`는 통합 테스트에서만 필요하며 라이브러리의 직접 의존성이 아니다.

## 빌드·테스트·설치

모든 생성물은 `build/` 아래에 둔다. 기존 SDK 설치본의 경로를 `CMAKE_PREFIX_PATH`에 추가한다.
LVRS 설치 위치가 기본값과 다르면 `-DLVRS_DIR=<설치본>/lib/cmake/LVRS`를 전달한다.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="<Qt>;<iiSocietyContainer>;<iiSocietySync>;<iiServerHost>;<LVRS>" \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/install"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build
```

제품 소비 빌드는 `-DBUILD_TESTING=OFF`로 테스트용 LVRS·iiSocietySync 검색을 생략할 수 있다.
SDK 통합 설치기의 CMake 자동 검색 대상이며 별도 중앙 목록 등록이 필요하지 않다.

```cmake
find_package(iiPhotoLibrary 0.1 CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE iiPhotoLibrary::iiPhotoLibrary)
# QQmlEngine::addImportPath()에 iiPhotoLibrary_QML_IMPORT_PATH를 전달한다.
```

```cpp
#include <iiPhotoLibrary/iiPhotoLibrary.h>
iiPhotoLibrary::PhotoController photos(sender, parent);
// 기존 인증 계층에서 검증한 피어만 전달한다.
photos.configure(containerPath, true, authorizedPeers, hostPeers);
photos.setApplicationActive(true);
// society.photos 요청 -> photos.handle(authenticatedPeer, packet)
// 전송 응답 -> photos.receive(requestId, reply)
```

```qml
import iiPhotoLibrary 1.0
PhotosView { controller: photoController }
```

호출자는 준비된 iiSocietyContainer 컨테이너와 인증된 전송 경로를 제공한다.
메타데이터·프리뷰의 파일 동기화는 iiSocietySync 등 별도 계층이 담당한다.
앱 전경/백그라운드 변화와 작업 소유권 해제를 전달하고, 비활성화 후 `waitForDone()`으로 종료를 기다린다.
`IIPHOTOLIBRARY_DISABLE_PHOTOS=1`은 이 SDK의 컨트롤러를 비활성화한다.

## 호환성·검증

`.societyphoto`, `.society-photos`, `society.photos`, `Society-<id>-<index>-<name>` 및
Android의 Pictures/Society 경로는 기존 데이터·전송 호환성을 위해 유지한다.
C++ 네임스페이스와 Android 클래스는 앱과 충돌하지 않도록 독립시켰다.
동일 컨테이너에 구 구현과 SDK 컨트롤러를 동시에 활성화하지 않는다.

원본 SHA-256과 추출 기준 Git revision은 [source-manifest.json](docs/source-manifest.json)에 있다.
Society의 기존 미커밋 변경은 포함 여부와 관계없이 변경하지 않는다.
[원본 동작 계약](docs/SocietyReference.md)은 추출 시점의 참조 문서이며 그 문서에 적힌
기존 제품의 플랫폼 검증은 이 SDK의 새 검증을 뜻하지 않는다.
[플랫폼 통합](docs/Platforms.md), [추출·검증 결과](docs/Extraction.md)를 참고한다.
