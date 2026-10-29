# Society Photos 추출 기록

기준일: 2026-09-24. 위치: `Workspace/SDK/iiPhotoLibrary`.

## 보존한 것

Society 앱의 Photos/Gallery 구현, Android 어댑터, 테스트·참조 문서에서 필요한 파일을
복사해 독립시켰다. Society의 CMake와 원본 소스는 수정·삭제하지 않았다.
`source-manifest.json`에 기록한 원본 20개의 SHA-256을 다시 대조해 변경 없음이 확인되었다.
원본 Git revision과 실제 파일 해시를 함께 기록하므로 미커밋 소스도 구분할 수 있다.

저장 구조와 전송 프로토콜은 유지한다. 스캔·인덱스·원본 캐시·등록·삭제·전송 알고리즘도
추출 시점 구현을 유지한다. 제품 앱에 SDK 연결을 적용하는 단계는 이번 작업에 포함하지 않는다.

## 독립 패키지로 변경한 것

- C++ 네임스페이스: `society::photos` → `iiPhotoLibrary`.
- 공개 헤더와 구현: `src/Albums/`와 `src/Library/`, 플랫폼 구현: `src/Platform/`.
  umbrella 헤더는 `src/Library/iiPhotoLibrary.h`이며 기존 공개 include 이름은 유지한다.
- 독립 CMake 정적 라이브러리·설치·export·버전 config 제공.
- Android 클래스: `com.iisacc.iiphotolibrary.PhotoLibrary`; 호스트 Activity 콜백 계약 문서화.
- QML 모듈: `iiPhotoLibrary 1.0`, SDK 안의 LVRS 기반 Gallery 컴포넌트를 사용.
- 환경 변수: `IIPHOTOLIBRARY_DISABLE_PHOTOS`.
- 테스트·QML harness·Android fixture는 SDK 내부 파일만 사용.
- SDK 설치기의 자동 프로젝트 검색 및 의존 순서 확인.

## 검증과 증거

macOS arm64, AppleClang 21, C++23, Qt 6.8.3에서 빌드했다.
최종 화면 검증은 사용자 설치본 `~/.local/SDK/LVRS/platforms/macos`를 사용한다.
`~/.local/LVRS` 경로는 현재 존재하지 않는다.

- `build/libiiPhotoLibrary.a`: Apple PhotoKit 어댑터를 포함한 SDK 빌드.
- `build/standalone/build/libiiPhotoLibrary.a`: BUILD_TESTING=OFF 구성.
  LVRS 및 iiSocietyHelper Sync를 찾지 않고 Qt와 iiSocietyContainer만으로 빌드된다.
- `iiPhotoLibrary.Photos`: 원본 회귀 테스트 이식. 갤러리 영구 인덱스, 우선순위 변경,
  원본 보존, 취소, 해시 검증, 제한 권한·DB 교체의 삭제 방지, TLS 원본 전송 등을 검사한다.
- `iiPhotoLibrary.InstalledConsumer`: 별도 prefix에 설치 후 디렉터리를 옮겨
  외부 CMake 프로젝트가 패키지를 발견하고 링크·실행하는지 검증한다.
  네이티브 백엔드·컨트롤러 생성과 설치된 QML 모듈 로드도 포함한다.
- `build/extraction-checks.json`: 원본 보존·Society 앱 빌드 참조 없음·Android runner 구문 검사 결과.
- `build/Testing/Temporary/LastTest.log`: 최종 CTest 결과.
- `build/photos/`: 모바일/데스크톱 갤러리, 시간순 위치 유지, 정보창 렌더링 PNG.

최초 5초 준비 대기에서 한 차례 5.9초 후 조건이 충족되는 지연을 관측했다.
해당 검사는 성능 기준이 아닌 비동기 작업 완료·스크롤 상태 검사이므로, SDK 테스트의
최초 준비 대기만 15초로 변경했다. 검증 조건과 제품 동작은 바꾸지 않았다.

## 검증 경계

사진 원본은 테스트 fixture이며 실제 사용자 사진을 등록·삭제하지 않았다.
TLS 테스트는 localhost의 실제 TLS 전송과 주입된 사진 보관함을 조합한다.
Apple 네이티브 구현의 컴파일·링크·생성은 확인했지만 실제 PhotoKit 권한 승인·iCloud 다운로드·
사용자 보관함 등록/삭제는 이번 실행에서 검증하지 않았다.
Android/Windows/iOS 구현·통합 계약은 포함하되 타깃 빌드와 실기기 검증 완료로 간주하지 않는다.
Android SDK/JDK가 참조된 기존 위치에 없어 Android native fixture를 실행하지 않았다.

이 패키지는 기존 Qt 기반 구현을 독립시킨 0.1 추출본이다. Qt 의존성이 GUI 외 저장·동기화 및
공개 타입에도 남아 있으므로, 표준 C++만을 사용하는 iiSDK 코어로의 전환과는 구별한다.

## 최종 결과

2026-09-24 최종 실행에서 QtTest 34 passed / 0 failed / 0 skipped,
CTest 2/2 passed를 확인했다. Photos 스위트 76.77초,
설치·재배치 소비 테스트 6.99초, CTest 전체 84.77초이다.
배포 확인용 설치본은 `build/install/`이며 호스트 앱이나 기존 SDK 설치본을 교체하지 않는다.
