# 플랫폼 통합 계약

## Apple

PhotoKit 권한 요청 전 호스트 앱의 Info.plist에 `NSPhotoLibraryUsageDescription` 및
`NSPhotoLibraryAddUsageDescription`을 앱 용도에 맞게 설정한다.
macOS Sandbox 사용 시 `com.apple.security.personal-information.photos-library` entitlement가 필요하다.
CMake는 Photos/Foundation/UniformTypeIdentifiers와 macOS AppKit 또는 iOS UIKit을 연결한다.
권한 요청은 GUI 스레드에서, 조회·프리뷰·원본 추출·등록·휴지통 작업은 작업 스레드에서 호출한다.
실제 PhotoKit 동작은 서명된 호스트 앱의 권한과 시스템 보관함에 따라 달라진다.

## Android

`iiPhotoLibrary_ANDROID_SOURCE_DIR`의 Java 소스를 호스트 Android 패키지에 포함한다.
클래스는 `com.iisacc.iiphotolibrary.PhotoLibrary`이며 SocietyActivity에 의존하지 않는다.
호스트 Activity는 다음 콜백을 전달해야 한다.

```java
@Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
    super.onRequestPermissionsResult(request, permissions, results);
    if (request == com.iisacc.iiphotolibrary.PhotoLibrary.PERMISSION_REQUEST)
        com.iisacc.iiphotolibrary.PhotoLibrary.photoAccessFinished();
}
@Override protected void onActivityResult(int request, int result, android.content.Intent data) {
    super.onActivityResult(request, result, data);
    if (request == com.iisacc.iiphotolibrary.PhotoLibrary.TRASH_REQUEST)
        com.iisacc.iiphotolibrary.PhotoLibrary.consentFinished(result);
}
```

Manifest 권한: READ_MEDIA_IMAGES, READ_MEDIA_VIDEO, READ_MEDIA_VISUAL_USER_SELECTED,
ACCESS_MEDIA_LOCATION 및 구 OS의 READ_EXTERNAL_STORAGE(maxSdkVersion=32),
WRITE_EXTERNAL_STORAGE(maxSdkVersion=28). compileSdk 34 이상이 필요하다.
요청 코드 9076/9077은 호스트의 다른 기능과 중복 사용하지 않는다.
Java 코드를 패키징하지 않거나 콜백을 전달하지 않으면 권한·휴지통 기능이 동작하지 않는다.
최종 APK의 MediaStore·동의 UI 동작은 실제 기기에서 별도로 검증해야 한다.

## Windows 및 기타

Windows는 Shell 라이브러리/썸네일/Recycle Bin을 사용하며 shell32/ole32/gdi32/uuid를 연결한다.
별도 클라우드 웹 전용 항목이나 Photos 비공개 DB를 읽지 않는다.
기타 플랫폼은 Access::Unsupported를 반환하고 컨테이너 원본 저장소 기능만 제공한다.

Android 네이티브 fixture는 `tests/android/`에 포함한다. SDK 36/build-tools 36.0.0,
JDK 및 ffmpeg가 준비되어 있고 에뮬레이터가 실행 중이면 다음으로 실행한다.
테스트는 자신의 패키지와 생성한 미디어만 정리하며 물리 기기를 대상으로 실행하지 않는다.

```sh
ANDROID_SDK_ROOT=<Android-SDK> JAVA_HOME=<JDK> \
  IIPHOTOLIBRARY_TEST_DEVICE=emulator-5554 python3 tools/test_android_photos.py
```
