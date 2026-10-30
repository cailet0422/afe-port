# Android 빌드

PC 포트를 SDL2 + OpenGL ES 3.0으로 Android에서 돌린다. APK 하나에 **32비트 ARM(armeabi-v7a)과
64비트 ARM(arm64-v8a)** 라이브러리가 함께 들어가고, 기기가 맞는 쪽을 고른다. 최신 일부 기기(Snapdragon
8 Gen 3 이후, Pixel 7 이후 등)는 32비트 앱을 실행하지 못하므로 64비트 쪽이 쓰인다(64비트 포팅은
`64bit.md`).

APK에는 매칭 빌드가 **본인 디스크에서 추출한 에셋**이 컴파일되어 들어가므로, 직접 빌드해서 본인 기기에만
설치한다(배포하지 않는다). `-PstripAssets`로 빌드하면 에셋을 빼고 시작할 때 디스크에서 읽는다(README의
"에셋 없는 빌드").

## 준비

- JDK 17 이상, Android SDK(platform 34, build-tools 34), NDK `27.2.12479018`, CMake `3.22.1`
  (Android Studio의 SDK Manager 또는 `sdkmanager`), Gradle 8.7 이상(또는 Android Studio)
- Python 3 (네이티브 빌드가 PC 빌드와 같은 생성 스크립트를 돌린다)
- 매칭 빌드를 한 번 해 둔다(`decomp/`에서 `python configure.py --version GAEJ01_01 && ninja`, README 참고)
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

Android Studio에서는 `pc/android`를 열면 된다. 첫 네이티브 빌드는 foresti/foresto 생성 때문에 몇 분 걸린다
(ABI마다 한 번씩). 네이티브 빌드가 셰이더와 REL 오버레이 표를 APK의 assets에 넣는다. 오버레이 표는
라이브러리의 배치에 따라 다르므로 ABI마다 따로 만든다(`DobutsuNoMoriEPlus-arm64-v8a.relmap` 등).
한쪽만 필요하면 `app/build.gradle`의 `abiFilters`를 줄인다.

## 실행

1. 디스크 이미지(`.iso`/`.ciso`/`.gcm`, 한글패치 적용본 가능)를 폰의 아무 폴더에나 둔다(예: `Download/동물의숲`).
2. APK를 설치하고 실행하면 디스크 이미지를 찾지 못했다는 안내가 뜬다. "폴더 선택"을 누르고 시스템 폴더
   선택 화면에서 이미지가 있는 폴더를 고른 뒤 "이 폴더 사용"(허용)을 누른다. 고른 폴더는 기억되어 다음
   실행부터 바로 시작한다. Android는 저장소 최상위와 `Download` 자체는 고르지 못하게 하므로 그 안에 폴더를
   만들어 둔다. 클라우드(구글 드라이브 등)가 아닌 기기 안의 폴더여야 한다.
3. 폴더는 일시정지 메뉴(뒤로 버튼) → Settings → Disc → Folder에서 바꾼다(다시 실행하면 적용).
4. 예전처럼 앱 데이터 폴더 `내부 저장소/Android/data/org.afedecomp.dobutsunomori/files/rom/`에 넣어도 된다
   (고른 폴더에 이미지가 없을 때 찾는다). `settings.ini`, `save/`, `log.txt`(실행 로그)는 이 데이터 폴더에 생긴다.

앱은 고른 폴더를 시스템 폴더 선택(SAF)으로 받은 권한으로 읽는다(`MainActivity.java`, `pc_rom_dir.c`). 다른
저장소 권한은 요청하지 않는다.

### 한글패치 적용 (게임 안에서)

1. 원래 디스크 이미지(일본판 Rev 1)로 실행한 뒤 뒤로 버튼 → Settings → Disc → **Korean patch: Install**.
2. 시스템 파일 선택 화면에서 받은 한글패치 **zip**(또는 그 안의 `.xdelta`)을 고른다. 다운로드 폴더 그대로 둬도 된다.
3. 진행이 끝나면 **Restart now**. 앱이 스스로 다시 시작해 한국어로 나온다.

한글판 이미지(약 37MB)는 앱 데이터 폴더의 `korean/`에 만들어지고 원래 이미지는 그대로다. Disc 탭의
**Korean < On / Off >**로 원래 디스크와 오갈 수 있다(Apply 후 다시 시작). 파일은 시스템 파일 선택(SAF)으로
받은 권한으로 한 번 읽는다(`MainActivity.pickPatchFile`). 다시 시작은 따로 된 프로세스의
`RestartActivity`(`android:process=":restart"`)가 게임 프로세스를 끝내고 새로 연다 — 네이티브 라이브러리의
정적 변수가 같은 프로세스에서는 초기화되지 않기 때문이다.

### 조작키

Settings → Controls → Buttons에서 블루투스 게임패드의 버튼(과 물리 키보드 키)을 바꿀 수 있다
(README의 "조작키 바꾸기"). 일시정지 중에는 게임패드 십자키·A·B로 메뉴를 움직인다.

## 현재 상태

- 빌드(컴파일·링크·APK 패키징)까지 확인했다. 이 작업 환경에는 Android 기기·에뮬레이터가 없어 실제
  실행은 확인하지 못했다. 같은 렌더러는 리눅스에서 OpenGL ES 3.0 컨텍스트로 확인했다(dev_setup.md 7).
- 입력: 게임패드(블루투스 등)와 물리 키보드는 SDL이 처리한다. 글자 입력 화면(이름·편지 등)이 열리면
  Android 화면 키보드가 뜨고, 그 키보드로 한글을 입력한다(`settings.ini`의 `text_input = 1`,
  README의 "시스템 입력기"). 한글패치 디스크에서는 게임 화면 아래의 화면 키보드(QWERTY 두벌식)를 손가락이나
  게임패드로 눌러도 된다(README의 "편집기 연결").
- 터치 조작(`pc/src/pc_touch.c`, `settings.ini`의 `touch_controls = 1`, Android 기본값): 게임 화면 위에
  게임큐브 패드 모양의 화면 패드를 그린다. 왼쪽 아래 절반에 엄지를 대면 그 자리가 스틱 중심이 되고
  (떠 있는 스틱), 오른쪽에 A(크게)/B/X/Y, 위쪽에 L·START·R·Z, 왼쪽 위에 작은 십자키가 있다. 버튼 사이를
  미끄러지면 손가락 아래 버튼이 눌린다. 크기는 화면의 짧은 변 기준이라 4:3·20:9 모두 가장자리에 붙는다.
  실제 게임패드 버튼을 누르면 숨고, 다시 화면을 만지면 나타난다. 그리기는 프레임이 끝난 뒤 자체
  셰이더로 하고 GX 상태를 다시 적용하게 표시한다(`pc_gx_restore_after_nes`).
- 뒤로 버튼은 앱을 끄지 않고 일시정지 메뉴를 연다(`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`, Esc와 같음).
  일시정지 메뉴는 키 입력만 받으므로 화면 패드의 십자키/A/B(START)를 방향키/Enter/Esc로 바꿔 보낸다.
  글자 입력 중 화면 키보드를 내렸으면 패드가 없는 곳을 눌러 다시 띄운다. 터치는 마우스 클릭으로
  바꾸지 않고(`SDL_HINT_TOUCH_MOUSE_EVENTS`), 가속도계를 조이스틱으로 잡지 않는다.
- 자동 테스트: `--input-script`의 `TOUCH x,y[:x2,y2] [프레임]`(창 크기 대비 %, 다음 프레임에 x2,y2로
  이동)으로 데스크톱에서 `touch_controls = 1`을 켜고 시험한다(스틱 걷기, START 소지품, B 닫기 확인).
- Clang(NDK)과 GCC의 차이로 고친 것: `-fsigned-char`(ARM의 char는 unsigned), `operator new`의 `size_t`,
  `EOF`/`SEEK_*`가 stdio 매크로로 바뀌는 열거자(정수 → enum 변환), libc++ `<cmath>`의 C++ 링크,
  bionic의 `bcmp`, lld 링크 맵과 Clang의 함수 안 static 이름(`함수.변수`).
