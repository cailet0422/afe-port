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

`-PstripAssets` 확인(리눅스, SDK는 `sdkmanager "platforms;android-34" "build-tools;34.0.0" "ndk;27.2.12479018"
"cmake;3.22.1"`, SDL2 2.30.9, Gradle 8.14, JDK 21): `gradle assembleDebug -PsdlDir=… -PstripAssets`가 약 7분에 끝나고
APK는 12MB(라이브러리 armeabi-v7a 9.6MB, arm64-v8a 15.5MB). 매칭 빌드가 추출한 에셋에서 고른 32바이트 조각 400개 중
데스크톱 일반 실행 파일에서는 399개가 찾히고, 두 ABI의 `libmain.so`에서는 1개(연속된 아이템 번호 목록이라 코드의 다른
표와 우연히 같은 것; 데스크톱 에셋 없는 빌드도 같은 1개)만 찾힌다. 그전에는 창 아이콘(`pc_window_icon_rgba`)을
디스크 에셋으로 잘못 보아 `--require-assets`가 링크를 실패시켰다(`bin2c.py --own`으로 고침). 기기에서 실행은 하지 못했다.

## 정식 서명(릴리스 APK)

`gradle assembleRelease`는 `pc/android/release-key/keystore.properties`가 있으면 그 키로 서명한다(없으면 예전처럼 디버그 키).
다른 곳의 파일은 `-PreleaseKeyProps=경로/keystore.properties`로 준다. `storeFile`은 그 파일이 있는 폴더 기준이다.

```properties
storeFile=afe-port-release.jks
storePassword=…
keyAlias=afe-port
keyPassword=…
```

키 만들기(한 번만): `keytool -genkeypair -keystore afe-port-release.jks -storetype PKCS12 -alias afe-port -keyalg RSA
-keysize 4096 -validity 36500`. `release-key/`, `keystore.properties`, `*.jks`는 `.gitignore`에 있어 커밋되지 않는다.
**키와 비밀번호는 저장소 밖에 안전하게 보관한다**: 다음 판의 APK도 같은 키로 서명해야 기존 설치 위에 업데이트되고,
키를 잃으면 사용자가 앱을 지우고(세이브를 옮긴 뒤) 다시 설치해야 한다.

```sh
cd pc/android
gradle assembleRelease -PsdlDir=/path/to/SDL2-2.30.9 -PstripAssets
# → app/build/outputs/apk/release/app-release.apk (디버그 불가, v1·v2 서명)
apksigner verify --print-certs app/build/outputs/apk/release/app-release.apk
```

릴리스 빌드도 디컴프 코드를 데스크톱처럼 `-O0`으로 만든다. Gradle 플러그인은 릴리스를 CMake `RelWithDebInfo`(`-O2`)로
빌드하는데, 최적화는 디컴프 코드의 정의되지 않은 동작을 드러내고 bionic의 FORTIFY 검사(최적화할 때만 켜짐)도 켠다.
그래서 `pc/CMakeLists.txt`가 빌드 타입의 `-O` 플래그를 지운다(SDL2는 그대로 `-O2`). v0.1.1의 APK가 이 `-O2`
빌드였다. 라이브러리는 16KB 페이지 기기(Android 15 이후)에서도 올라가게 16KB로 정렬한다
(`ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES`).

## 에뮬레이터로 확인

ABI를 `-Pabis=x86_64`로 바꿔 빌드하면 x86_64 에뮬레이터에서 돌릴 수 있다(기본은 `armeabi-v7a,arm64-v8a`).
KVM이 없는 리눅스(이 작업 환경)에서 한 방법:

```sh
sdkmanager "emulator" "system-images;android-34;default;x86_64"
avdmanager create avd -n x64 -k "system-images;android-34;default;x86_64" -d pixel_6
# config.ini: disk.dataPartition.size = 2147483648 (디스크가 모자라면 AVD를 /dev/shm에 두고 ANDROID_AVD_HOME 지정)
E=$ANDROID_SDK_ROOT/emulator
LD_LIBRARY_PATH=$E/lib64:$E/lib64/qt/lib:$E/lib64/gles_swiftshader \
  $E/qemu/linux-x86_64/qemu-system-x86_64-headless -avd x64 -no-window -no-boot-anim \
  -gpu swiftshader_indirect -feature -Vulkan -no-snapshot -accel off
```

`emulator` 런처는 KVM 없이 x86_64 이미지를 거부하므로 그 안의 qemu를 직접 띄운다(부팅 약 5분 반, 게임은 매우 느림).
ARM64 이미지는 x86 호스트용 qemu에 PCI 버스가 없어 부팅하지 못했다. 디스크 이미지는 `adb root` 뒤
`/data/media/0/Android/data/org.afedecomp.dobutsunomori/files/rom/`에 넣고 소유자를 앱으로 바꾼다. 확인은
`adb logcat`(SDL, `F libc`, `F DEBUG`)과 데이터 폴더의 `log.txt`·`crash.txt`, `adb exec-out screencap -p`로 한다.

릴리스 빌드 끝의 lint 검사가 Maven에서 파일을 받는다. 그 서버가 429(요청 과다)를 돌려주면 잠시 뒤 다시 실행한다
(네이티브 빌드는 이미 끝나 있어 금방 끝난다).

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

- 앱 아이콘: `res/mipmap-*/ic_launcher.png`(48–192px)와 적응형 아이콘(`mipmap-anydpi-v26`, 전경은 108dp 판의
  가운데 80dp, 배경색 `#5AAB49`). 원본은 `pc/res/icon.png`.
- 실행 확인: x86_64 에뮬레이터(Android 14, 아래 "에뮬레이터로 확인")에서 디스크 폴더 안내, 디스크 읽기(에셋 17736개),
  오프닝과 타이틀 로고까지 확인했다. 실제 기기에서는 아직 확인하지 못했다. 같은 렌더러는 리눅스에서 OpenGL ES 3.0
  컨텍스트로 확인했다(dev_setup.md 7).
- v0.1.0–v0.1.2의 APK는 누르자마자 닫혔다. 두 가지가 겹쳐 있었다.
  - `pc_platform.h`의 `SDL_MAIN_HANDLED` 때문에 `main()`이 `SDL_main`으로 바뀌지 않는다. 그래서 SDL이
    `libmain.so`에서 `SDL_main`을 찾지 못하고(`nativeRunMain(): Couldn't find function SDL_main`) 게임을 시작하지
    않은 채 끝났다. 크래시가 아니라서 아무 기록도 남지 않았다. `MainActivity.getMainFunction()`이 `"main"`을 돌려준다.
  - 그 뒤로는 부팅 중 `JUTResFont::getWidth`에서 죽었다(시스템 폰트가 비어 있음). Android 빌드는 생성 소스(`gen/`의
    foresti/foresto 데이터, 시스템 폰트)가 대상(`afe/`)의 빌드 폴더 밖에 있어 CMake가 오브젝트 이름을 `pc/` 기준
    경로로 짓는데, `gen_rel_overlay.py`가 그 소스를 찾지 못해 에셋 17736개 중 9962개만 표에 올렸다. 이제 `pc/` 기준
    경로도 찾는다(데스크톱 표는 바이트까지 그대로). 소스를 못 찾은 오브젝트에 0으로 채운 데이터가 있으면
    `--require-assets`가 빌드를 멈춘다.
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
- 크래시 리포트: 런처 아이콘은 `LaunchActivity`를 연다. 지난 실행이 비정상으로 끝났으면 게임(`MainActivity`, 네이티브
  라이브러리를 올림)을 띄우기 전에 그 내용을 보여 주고("복사", "게임 시작", "닫기"), 아니면 바로 게임을 띄운다.
  게임과 같은 프로세스에서 창을 띄우면 시작하자마자 다시 죽을 때 창도 같이 사라지기 때문이다(v0.1.2). 내용(`CrashReport.java`):
  - `crash.txt`: 게임의 네이티브 크래시(SIGSEGV·SIGABRT 등, `pc_android.c`; 시그널, 주소, `libmain.so+오프셋 (함수)`
    형식의 호출 스택. 쓴 뒤 시스템의 처리기로 넘긴다)나 자바 예외(`CrashReport.installHandler`).
  - 시스템의 종료 기록(`ApplicationExitInfo`, Android 11+): 크래시, ANR, 오류 상태의 `exit()`. 게임의 처리기가 서기
    전(라이브러리를 올리는 중 등)의 크래시도 남는다. Android 12+의 네이티브 크래시는 tombstone(protobuf)을 읽어
    시그널, 죽은 스레드의 호출 스택, 그 프로세스의 마지막 logcat 40줄을 붙인다(`Tombstone.java`).
  - 앱 버전·기기·Android 버전과 `log.txt`의 끝 40줄. 전체는 `crash_prev.txt`로 남고, 같은 크래시는 한 번만 보인다.
  `log.txt`는 줄 단위로 바로 쓴다.
- 그래픽 설정: Video 탭에는 VSync, MSAA, Textures만 있다. Android는 늘 화면 전체를 쓰므로 PC의 창 모드·해상도
  (Display, Resolution)는 숨기고 `settings.ini`에 있어도 쓰지 않는다. GPU가 고른 MSAA를 지원하지 않으면 창을 만들 때
  8x→4x→2x→끔으로 낮춰 다시 시도한다(`[PC] MSAA 8x is not available, using 4x`). 예전에는 Galaxy Z Flip6에서 MSAA 8x를
  고르면 `Couldn't find matching EGL config`로 창을 못 만들고, 설정이 저장돼 있어 매번 시작하자마자 끝났다.
- 종료: Android에서는 `exit()` 대신 `_exit()`로 끝낸다(`pc_exit`; Quit Game, 한글패치 뒤 다시 시작, 시작 실패).
  `exit()`는 C++ 정적 소멸자를 돌리는데, 그동안 시스템의 렌더 스레드(libhwui)가 이미 정리된 뮤텍스를 써서
  "FORTIFY: pthread_mutex_lock called on a destroyed mutex"로 abort했다. 시작 실패(`pc_fatal`)는 이유를 메시지 창으로
  보여 주고 `log.txt`에도 남긴다.
- 크래시 보호(`pc_crash_set_jmpbuf`, emu64가 디스플레이 리스트에서 잘못된 주소를 읽으면 그 프레임을 건너뜀)는
  `jmp_buf`를 건 스레드의 폴트에서만 `longjmp`한다. 예전에는 다른 스레드(Android UI 스레드, 오디오)의 SIGSEGV도
  게임 스레드의 스택으로 뛰어, 두 스레드가 게임 코드를 함께 돌다 "stack corruption detected"로 죽었다(에뮬레이터에서
  실행 중 `kill -SEGV`로 재현). 이제 그런 폴트는 그대로 크래시 리포트로 간다.
- 뒤로 버튼은 앱을 끄지 않고 일시정지 메뉴를 연다(`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`, Esc와 같음).
  일시정지 메뉴는 키 입력만 받으므로 화면 패드의 십자키/A/B(START)를 방향키/Enter/Esc로 바꿔 보낸다.
  글자 입력 중 화면 키보드를 내렸으면 패드가 없는 곳을 눌러 다시 띄운다. 터치는 마우스 클릭으로
  바꾸지 않고(`SDL_HINT_TOUCH_MOUSE_EVENTS`), 가속도계를 조이스틱으로 잡지 않는다.
- 자동 테스트: `--input-script`의 `TOUCH x,y[:x2,y2] [프레임]`(창 크기 대비 %, 다음 프레임에 x2,y2로
  이동)으로 데스크톱에서 `touch_controls = 1`을 켜고 시험한다(스틱 걷기, START 소지품, B 닫기 확인).
- Clang(NDK)과 GCC의 차이로 고친 것: `-fsigned-char`(ARM의 char는 unsigned), `operator new`의 `size_t`,
  `EOF`/`SEEK_*`가 stdio 매크로로 바뀌는 열거자(정수 → enum 변환), libc++ `<cmath>`의 C++ 링크,
  bionic의 `bcmp`, lld 링크 맵과 Clang의 함수 안 static 이름(`함수.변수`).
