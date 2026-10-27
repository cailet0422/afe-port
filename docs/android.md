# Android 빌드

PC 포트를 SDL2 + OpenGL ES 3.0으로 Android에서 돌린다. 지금은 **32비트 ARM(armeabi-v7a)만** 빌드한다
(디컴프가 포인터를 32비트 정수로 다루기 때문, README의 M4). 최신 일부 기기(Snapdragon 8 Gen 3 이후,
Pixel 7 이후 등)는 32비트 앱을 실행하지 못한다.

APK에는 매칭 빌드가 **본인 디스크에서 추출한 에셋**이 컴파일되어 들어가므로, 직접 빌드해서 본인 기기에만
설치한다(배포하지 않는다).

## 준비

- JDK 17 이상, Android SDK(platform 34, build-tools 34), NDK `27.2.12479018`, CMake `3.22.1`
  (Android Studio의 SDK Manager 또는 `sdkmanager`), Gradle 8.7 이상(또는 Android Studio)
- Python 3 (네이티브 빌드가 PC 빌드와 같은 생성 스크립트를 돌린다)
- 매칭 빌드를 한 번 해 둔다(`python configure.py --version GAEJ01_01 && ninja`, README 참고)
- SDL2 소스(2.30.x): `https://github.com/libsdl-org/SDL/releases`의 `SDL2-2.30.9.tar.gz`를 풀어
  `pc/android/SDL`에 두거나 빌드할 때 `-PsdlDir=경로`로 알려 준다. SDL의 Java 코드(`SDLActivity`)와
  `libSDL2.so`가 여기서 나온다.

## 빌드

```sh
cd pc/android
echo "sdk.dir=/path/to/android-sdk" > local.properties
gradle assembleDebug -PsdlDir=/path/to/SDL2-2.30.9
# → app/build/outputs/apk/debug/app-debug.apk
```

Android Studio에서는 `pc/android`를 열면 된다. 첫 네이티브 빌드는 foresti/foresto 생성 때문에 몇 분 걸린다.
네이티브 빌드가 셰이더와 REL 오버레이 표(`DobutsuNoMoriEPlus.relmap`)를 APK의 assets에 넣는다.

## 실행

1. APK를 설치하고 한 번 실행하면 데이터 폴더가 생긴다:
   `내부 저장소/Android/data/org.afedecomp.dobutsunomori/files/`
2. 그 안의 `rom/`에 디스크 이미지(`.iso`/`.ciso`/`.gcm`, 한글패치 적용본 가능)를 넣는다. 최근 Android에서는
   일반 파일 관리자가 `Android/data`에 접근하지 못할 수 있으니 PC에 USB로 연결해서 복사한다.
3. 다시 실행한다. `settings.ini`, `save/`, `log.txt`(실행 로그)가 같은 폴더에 생긴다.

## 현재 상태

- 빌드(컴파일·링크·APK 패키징)까지 확인했다. 이 작업 환경에는 Android 기기·에뮬레이터가 없어 실제
  실행은 확인하지 못했다. 같은 렌더러는 리눅스에서 OpenGL ES 3.0 컨텍스트로 확인했다(dev_setup.md 7).
- 입력: 게임패드(블루투스 등)와 물리 키보드는 SDL이 처리한다. 터치 조작과 Android 키보드(IME) 한글
  입력은 아직 없다.
- Clang(NDK)과 GCC의 차이로 고친 것: `-fsigned-char`(ARM의 char는 unsigned), `operator new`의 `size_t`,
  `EOF`/`SEEK_*`가 stdio 매크로로 바뀌는 열거자(정수 → enum 변환), libc++ `<cmath>`의 C++ 링크,
  bionic의 `bcmp`, lld 링크 맵과 Clang의 함수 안 static 이름(`함수.변수`).
