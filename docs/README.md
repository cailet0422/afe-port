# 동물의 숲 e+ PC 포트 / 한국어 패치

afe-decomp를 기반으로 한 PC(Windows/Linux)·Android 포트와 한국어 패치 지원의 계획과 진행 상황을
정리한다. 이 저장소에는 포트 계층(`pc/`)과 문서만 있고, 디컴프(게임 코드와 그 안의 `TARGET_PC` 훅)는
`decomp/` 서브모듈([cailet0422/afe-decomp](https://github.com/cailet0422/afe-decomp))이다.

## 원칙

- **매칭 빌드는 절대 깨지지 않는다.** 디컴프 소스(`decomp/src/`, `decomp/include/`)의 PC 관련 수정은 전부
  `#ifdef TARGET_PC`(일부 `BUGFIXES`, `PC_ENHANCEMENTS`) 안에만 들어간다. 그 수정은 afe-decomp에
  커밋하고 이 저장소에서는 서브모듈 커밋을 올린다. 소스를 고친 뒤에는 `decomp/`에서 `ninja`로 매칭 빌드를
  다시 돌려 아래 SHA-1이 그대로인지 확인한다.

  | 파일 | SHA-1 |
  |---|---|
  | static.dol | `7574bec2692efc921c0975c52af41038f3fb7093` |
  | foresta.rel | `a3b987c9a0cc9764494ac1e6ec64dc6c5c16ead2` |
  | forestd.rel | `49fd5f046eb4d9ca9be98ab97c731c3553d9343c` |
  | foresti.rel | `98246b73d5ad3d14f2b61c4d8369d0ae52f14753` |
  | foresto.rel | `73ddcf0d1e979720c575f7ab0c5df2e9ff04eee0` |

- **게임 데이터는 저장소에 넣지 않는다.** 디스크 이미지, 디스크에서 추출한 에셋, 그것을
  포함한 빌드 결과물(`decomp/orig/*`, `decomp/build/`, `pc/build*/`)은 커밋/배포하지 않는다. 배포는 코드와
  패치(차분)만 하고, 사용자가 자기 디스크 이미지를 준비한다.
- PC 플랫폼 계층(`pc/`)은 [ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port)
  (MIT)의 것을 가져와 e+에 맞게 고쳤다. 라이선스 고지는 `pc/LICENSE`.

## 구조

```
게임 코드 (decomp/src/, 디컴프 그대로)  ── N64 디스플레이 리스트 ──▶ emu64 ──▶ GX ──▶ pc_gx (OpenGL 3.3)
        │                                                          SDL2: 창/입력/오디오
        ├─ Dolphin SDK / OS 호출 ─▶ pc_os, pc_dvd(디스크 이미지 직접 읽기), pc_card, pc_aram, pc_sdk_extra ...
        └─ 패미컴 ─▶ FixNES
```

- 4개의 REL 모듈(foresta/forestd/foresti/foresto)은 PC에서 실행 파일 하나로 정적 링크된다.
  foresti/foresto는 아직 C 소스가 없으므로 `pc/tools/gen_module_data.py`가 디컴프의
  `decomp/tools/converters/batch_assetdis.py`로 dtk 어셈블리에서 C 소스와 에셋을 생성한다.
- 에셋은 기본적으로 매칭 빌드가 추출한 `.inc`를 컴파일 시점에 포함한다. `-DPC_STRIP_ASSETS=ON`으로 빌드하면
  `.inc`(18,070개)와 시스템 글꼴을 같은 모양의 0으로 바꿔 넣고, 시작할 때 디스크의 main.dol·REL에서 전부 읽어
  채운다(`pc_rel_overlay.c`, 아래 "에셋 없는 빌드"). foresti/foresto를 dtk 어셈블리에서 만든 C 소스의 구조
  (디스플레이 리스트, 표)는 어느 쪽이든 그대로 들어간다.
- 소리는 보통 별도 스레드(`pc_audio.c`의 생산 스레드)가 만든다. 게임큐브에서 소리는 오디오 인터럽트로 돌아서
  게임 코드가 `OSDisableInterrupts`로 막은 구간에는 끼어들지 못했으므로, PC에서는 "인터럽트 끄기"를 생산
  스레드와 함께 쓰는 잠금으로 구현했다(`pc_os.c`). 스레드마다 켜짐/꺼짐 상태를 따로 두어 중첩된 호출은
  바깥쪽에서만 잠그고, 생산 스레드는 소리 한 프레임을 만드는 동안 같은 잠금을 잡는다. 인터럽트를 끈 채 소리
  진행을 기다리는 곳은 없다(게임 코드의 해당 구간은 모두 짧은 목록·변수 갱신). `--deterministic`에서는 소리를
  게임 스레드에서 프레임마다 만든다.
- 원래 빅엔디언 원시 바이트(`u8[]`)로 들어 있는 데이터를 다른 곳에서 `u16`/구조체로 읽는
  경우가 있어, `pc/tools/gen_data_fixups.py`가 그런 블롭을 찾아 부팅 시 한 번 바이트 순서를
  바꾸는 표(`pc_data_fixups.c`)를 만든다.
- 디컴프가 포인터를 `u32`로 다루는 곳이 많아 데스크톱은 **32비트(x86) 빌드**가 기본이다. 64비트
  (Android arm64용)는 `TARGET_PC` 안에서 포인터 크기를 따르도록 고쳐 리눅스 x86_64에서 새 게임 진행까지
  확인했다([64bit.md](64bit.md), CMake `-DPC_ALLOW_64BIT=ON`).

## 빌드 (Linux, 32비트)

필요: `gcc-multilib g++-multilib cmake ninja-build python3 libsdl2-dev:i386 libgl-dev:i386`

0. 서브모듈까지 받는다: `git clone --recursive <이 저장소>` (이미 받았으면 `git submodule update --init --recursive`).
   디컴프를 다른 곳에 두었다면 cmake에 `-DDECOMP_ROOT=<afe-decomp 경로>`.
1. 매칭 빌드 준비 (에셋 추출과 dtk split에 필요)
   ```sh
   # decomp/orig/GAEJ01_01/ 에 디스크에서 추출한 sys/main.dol 과 files/*.rel 을 둔다
   # (python3 pc/tools/gcdisc.py extract-orig 디스크.ciso decomp/orig/GAEJ01_01)
   cd decomp && python3 configure.py --version GAEJ01_01 && ninja && cd ..
   ```
2. PC 빌드
   ```sh
   cmake -S pc -B pc/build32 -G Ninja \
         -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake
   ninja -C pc/build32
   ```
   첫 configure는 foresti/foresto 생성 때문에 몇 분 걸린다. 32비트 x86은 `-msse2 -mfpmath=sse`로 빌드한다:
   x87로는 범위를 넘는 실수를 `s16`에 넣을 때 -32768이 되는데, 게임큐브는 32비트로 바꾼 뒤 아래 16비트를 남기고
   게임 코드가 그것에 기댄다(각도 보간 `cKF_SkeletonInfo_subRotInterpolation` — 구역 경계를 넘는 동안 카메라가
   땅높이에서 수평으로 보던 문제).
3. 실행: 디스크 이미지(`.ciso`/`.iso`/`.gcm`)를 `pc/build32/bin/rom/`에 두거나 `settings.ini`의
   `rom_dir`에 이미지가 있는 폴더(또는 이미지 파일)를 적고
   ```sh
   cd pc/build32/bin && ./DobutsuNoMoriEPlus --verbose
   ```

### Windows (32비트)

- 리눅스에서 교차 빌드(검증됨): `gcc-mingw-w64-i686 g++-mingw-w64-i686`와 SDL2 MinGW 개발 패키지
  (`SDL2-devel-2.30.x-mingw.tar.gz`, libsdl-org/SDL 릴리스)를 받아
  ```sh
  cmake -S pc -B pc/buildwin -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-mingw32.cmake \
        -DSDL2_DIR=<SDL2>/i686-w64-mingw32/lib/cmake/SDL2 -DPC_BUILD_TESTS=OFF
  ninja -C pc/buildwin
  ```
  `pc/buildwin/bin/`에 exe, `SDL2.dll`(자동 복사), `shaders/`, `.relmap`이 생긴다. 이 폴더째 Windows로 옮기고
  `rom/`에 디스크 이미지를 넣거나, 실행했을 때 뜨는 안내에서 "폴더 선택"으로 이미지가 있는 폴더를 고른다.
  필요한 DLL은 `SDL2.dll`뿐(그 밖은 Windows 기본).
- 디스크 이미지 위치: `settings.ini` `[Paths]`의 `rom_dir`(폴더 또는 이미지 파일, UTF-8, 한글 폴더 이름 가능)을
  먼저 보고, 없으면 게임 폴더와 `orig/`, `rom/`을 찾는다. Windows에서는 일시정지 메뉴(Esc) → Settings →
  Disc → Folder에서 탐색기 폴더 선택 창으로 바꿀 수 있다(다시 실행하면 적용, `pc_rom_dir.c`).
- MSYS2 **MINGW32** 셸: `pacman -S mingw-w64-i686-{gcc,cmake,ninja,SDL2,python}` 후 리눅스와 같은 순서(미검증).
- Wine으로 실행 확인: 오프닝·한글 이름 입력·새 게임 → 마을 경로가 리눅스 빌드와 같은 결과.
- Windows에서만 나던 것: emu64의 N64 형식 텍스처 변환 버퍼 끝을 다음 배열의 시작으로 잡고 있어(GC
  링커 배치 가정) MinGW 배치에서는 변환이 전부 실패했다(타이틀의 "Press Start Button!" 자리에 다른
  텍스처). 버퍼 끝을 배열 크기로 잡는다. exe는 `0x10000000`에 링크해 120MB가 넘는 이미지가 N64 세그먼트
  주소(`0x03000000`–`0x0FFFFFFF`)와 겹치지 않게 한다.

빌드하면 `bin/`에 `DobutsuNoMoriEPlus.relmap`이 함께 생긴다. 실행 파일은 이 표를 보고 main.dol·REL의 데이터를
실행 중인 디스크에서 다시 읽으므로(한글패치 디스크의 바뀐 데이터도 이렇게 들어온다) 두 파일을 같은 폴더에 둔다.

### 에셋 없는 빌드 (PC_STRIP_ASSETS)

```sh
cmake -S pc -B pc/build32strip -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake \
      -DPC_STRIP_ASSETS=ON
ninja -C pc/build32strip
```

- 실행 파일에는 에셋 자리만 0으로 남고(`.bss`라 파일도 그만큼 작다), 시작할 때 relmap 표를 따라 디스크에서
  에셋 17,693개를 채운다(글꼴처럼 게임이 시작하기 전에 쓰는 것이 있어 `pc_main.c`가 디스크를 연 직후에 한다).
  relmap이 없거나 디스크에서 다 채우지 못하면 오류 창을 띄우고 끝난다.
- 빌드할 때 `gen_rel_overlay.py --require-assets`가 실행 파일에 든 에셋이 모두 표에 있는지 보고, 하나라도
  빠지면 빌드가 실패한다. 같은 이름의 에셋이 여러 곳에 있으면 `config.yml`의 `extract`(디스크 위치)와
  foresti/foresto의 구간별 심볼로 짝을 정한다.
- 일반 빌드와 같은 결과다: `--deterministic` 스크립트로 비교해 한글 디스크 새 게임 → 저장(스크린샷 202장, 세이브
  파일), 원래 디스크 오프닝(52장)이 모두 같다. 에셋에서 고른 바이트 조각 400개 중 일반 실행 파일에서 찾히는
  261개가 이 실행 파일에서는 하나도 찾히지 않는다.
- 빌드에는 여전히 본인 디스크가 필요하다(매칭 빌드와 foresti/foresto 소스를 디스크에서 만든다).
  Android는 `gradle assembleDebug -PstripAssets`.
- `ninja -C pc/build32strip dist`가 게임 폴더를 `dist/DobutsuNoMoriEPlus-Linux.zip`(Windows 교차 빌드는
  `-Windows.zip`, `SDL2.dll` 포함)으로 묶는다: 실행 파일(디버그 정보 없이, 크래시 기록용 심볼 이름은 남김),
  relmap, `shaders/`, `LICENSE.txt`만 담는다(`bin/`에서 실행하면 생기는 `rom/`·`save/`·설정은 넣지 않는다).
  Linux 약 4MB, Windows 약 6MB. 받은 사람은 압축을 풀고 `rom/`에 자기 디스크 이미지를 넣거나 실행해서 폴더를
  고른다. 압축을 푼 Linux판과 Wine에서 실행한 Windows판 모두 원래 디스크 오프닝이 일반 빌드와 같은 화면이었다.

### 한국어(한글패치)로 실행

한글패치는 원 배포처(한식구, `https://cafe.naver.com/hansicgu`)에서 받는다. 이 저장소에는
패치 파일·번역문·글꼴 데이터를 넣지 않는다.

**게임 안에서 적용** (Windows·Android·Linux 공통, `pc/src/pc_ko_patch.c`):

1. 원래 디스크 이미지(일본판 Rev 1, `.iso`/`.ciso`/`.gcm`)로 게임을 실행한다.
2. 일시정지 메뉴(Esc, Android는 뒤로 버튼) → Settings → **Disc** 탭 → **Korean patch: Install**에서 Enter.
   - Windows: 탐색기 파일 선택 창이 뜬다. 받은 **zip을 그대로**(또는 그 안의 `.xdelta`) 고른다.
   - Android: 시스템 파일 선택 창(다운로드 폴더 등)에서 zip을 고른다.
   - 그 밖(Linux): 게임 폴더의 `patch/`에 zip을 넣어 두면 그것을 쓴다.
3. 진행 막대가 끝나면(1초 안팎) "Restart now"를 고른다. 다시 시작하면 한국어로 나온다.
   - 패치는 원래 이미지를 바꾸지 않고 게임 폴더의 `korean/`에 한글판 ISO(약 37MB)를 따로 만든다.
     `settings.ini` `[Korean]`의 `korean = 1`, `ko_image`가 그것을 가리킨다.
   - 결과는 패치의 `manifest.json`에 적힌 SHA-256과 맞아야 저장된다(zip 없이 `.xdelta`만 고르면 v0.9.2는
     내장 해시로, 그 밖은 패치 안의 창별 Adler-32로 확인). 원본이 Rev 1이 아니면 "not the one the patch is
     for"로 멈춘다.
   - 같은 탭의 **Korean: < On / Off >**(Apply 후 다시 시작)로 한국어판과 원래 디스크를 오갈 수 있다.

**직접 적용**: 패치에 들어 있는 도구(또는 `pc/tools/apply_ko_patch.py`)로 Rev 1 ISO에 적용한 뒤 그 ISO를
`rom/`에 넣어도 된다. 어느 쪽이든 실행 로그에 `Korean patch detected`가 나오면 한국어 모드다(글자 폭·한글
글리프·로고·이름표 등을 디스크에서 읽는다). 형식은 [korean_patch.md](korean_patch.md) 참고.

### 조작키 바꾸기

일시정지 메뉴 → Settings → **Controls** → **Buttons**에서 바꾼다(`pc_settings_menu.c`, `pc_keybindings.c`).

- 게임큐브 버튼 20개(A B X Y Start Z L R, 스틱·C스틱·십자키 네 방향)마다 **Keyboard**와 **Gamepad** 칸이
  있다. 위/아래로 줄, 왼쪽/오른쪽으로 칸을 고르고 Enter를 누른 뒤 원하는 키(마우스 버튼 포함)나 패드
  버튼(LT/RT 트리거 포함)을 누른다. Esc 또는 6초 동안 입력이 없으면 취소.
- 이미 다른 버튼에 쓰이던 키를 고르면 두 버튼의 키가 서로 바뀐다(겹치거나 빠지는 키가 없게).
- 패드의 왼쪽/오른쪽 스틱은 항상 메인 스틱/C스틱이다. 스틱 방향에 패드 버튼을 더 붙일 수도 있다.
- **Reset to defaults**로 처음 값. 바꾸면 바로 `keybindings.ini`에 저장된다(`[Keyboard]`, `[Gamepad]`,
  패드는 한 줄에 두 개까지: `L = leftshoulder, lefttrigger`). 예전 `keybindings.ini`(키보드만)도 그대로 읽는다.
- 일시정지 중에는 게임패드로도 메뉴를 움직인다(십자키, A = 결정, B/Start = 뒤로).

### 자동 테스트 입력

`--input-script 파일`로 프레임 단위 입력·스크린샷·종료를 재생할 수 있다
(형식은 `pc/include/pc_input_script.h`). `--no-framelimit`과 함께 쓰면 빠르게 돌릴 수 있다.
`KEY Escape`, `PAD dpdown`처럼 키·패드 이벤트도 보낼 수 있어 일시정지·설정 메뉴(키 설정, 한글패치
설치)를 시험할 수 있다. `POS`는 플레이어·주민 위치를 찍고, `SEEK`은 주민이나 지점까지 걷는다.
`--deterministic`과 함께 쓰면 같은 스크립트가 매번 같은 화면을 낸다.

## 마일스톤

| 단계 | 내용 | 상태 |
|---|---|---|
| M0 | `TARGET_PC` 훅 병합, 매칭 유지 검증, PC 빌드 링크 | 완료 |
| M1 | Linux/Windows에서 타이틀 화면까지 부팅 | 완료: Linux, Windows(리눅스에서 MinGW 교차 빌드, Wine으로 실행 확인) |
| M2 | 마을 플레이, 세이브(메모리 카드 파일), e+ 전용 기능(SD/e-Reader/GBA)은 우회 | 진행 중: 한글 디스크로 역 → 너굴 → 집 구경·선택 → 자유 이동 → 소지품 화면까지 크래시 없음(`pc/tools/make_newgame_script.py`). 저장·불러오기: 토용군(하니와)에게서 첫 저장(첫날은 주민 한 명에게 인사해야 받아 줌), 이어하기, 덮어쓰기 저장, 저장 없이 끈 뒤 도루묵씨까지 확인(`make_newgame_script.py --save`, 메모리 카드 계층 테스트 `pc/tests/test_pc_card.c`). 남은 것: 아르바이트 전체, 여러 날에 걸친 플레이 |
| M3 | 오디오 정상화, 패미컴(FixNES) | 예정 |
| M4 | 64비트 정리(포인터↔u32), Android(arm64, SDL2 + GLES3, 터치 입력) | 진행 중: 렌더러 OpenGL ES 3.0 지원(`-DPC_GLES=ON`, 리눅스 Mesa GLES로 오프닝·마을 확인), Android(armeabi-v7a) APK 빌드, 화면 게임패드(터치)·Android 키보드 한글 입력([android.md](android.md)). 64비트: clang 32비트 빌드와 `--deterministic` 비교로 오프닝(52장)·한국어 새 게임 경로(202장 중 201장)가 같은 화면, 이름 입력 창·편집기 버튼 수정, APK에 arm64-v8a 포함([64bit.md](64bit.md)). 남은 것: 건물 안·패미컴·저장의 64비트 점검, 실기기 확인 |
| M5 | 한국어 패치 | 사용자 한글패치 디스크 지원: 대사·로고·이름표·글자 폭 표시, 키보드 한글 입력(이름·마을 이름·영어 이름 확인, 편지는 코드만 연결), 사전 코드·말버릇·조사 자동 선택, 패치가 옮긴 문자열·표, 게시판/일기/알림판 줄 폭, 설정 메뉴에서 패치 zip을 골라 바로 적용(`korean/`에 한글판 이미지를 만들고 다시 시작). 게임패드·터치용 화면 키보드(QWERTY 두벌식 + 한/영). 남은 것: 시간·비밀번호 입력 등 UI 상수, 말소리, SD 경고 문구 ([korean_patch.md](korean_patch.md)) |
| M6 | 런타임 에셋 로더, 배포 패키징 | 진행 중: 에셋 없는 빌드(`-DPC_STRIP_ASSETS=ON`) — 실행 파일에 디스크에서 꺼낸 에셋 없이, 시작할 때 디스크에서 읽는다. 일반 빌드와 스크린샷·세이브가 같다. `dist` 타깃으로 Linux·Windows zip(Windows는 Wine에서 확인). 남은 것: 실제 Windows·Android 기기 확인, Android APK(`-PstripAssets`) 빌드 확인 |

## 한국어 패치 설계

### 문자 인코딩과 폰트

- 원본은 1바이트 문자표 + 폰트 뱅크 3개(약 768자)라서 완성형 한글 2,350자를 담을 수 없다.
- PC 빌드 전용으로 **2바이트 인코딩**을 추가한다: 선두 바이트를 미사용 영역에 예약하고 그 뒤
  1바이트로 한글 음절 인덱스를 지정한다. 기존 1바이트 문자(영문/기호/일본어)는 그대로 둔다.
- 글리프는 TTF(라이선스가 자유로운 한글 폰트)에서 부팅 시 텍스처 아틀라스로 렌더링하고,
  게임의 폰트 그리기(`mFont`) 경로에서 2바이트 코드를 만나면 아틀라스에서 그린다.
- 텍스트 너비 계산, 줄바꿈은 2바이트 코드를 한 글자로 센다.

### 텍스트 처리

- **조사 자동 선택**: 이름이 메시지에 삽입되는 곳(플레이어/주민/아이템 이름)을 위해
  `은/는`, `이/가`, `을/를`, `과/와`, `으로/로` 선택 제어 코드를 추가한다. 앞 글자의 받침
  유무(ㄹ 받침과 `으로/로` 예외 포함)로 런타임에 결정한다.
- **어절 단위 줄바꿈**: 일본어는 글자 단위 줄바꿈이지만 한국어는 띄어쓰기 단위로 끊는다.
- **이름 길이**: 세이브 데이터의 이름 필드 바이트 수는 그대로 두고, 2바이트 문자로 들어가는
  글자 수만큼만 입력을 허용한다(세이브 호환성 유지).
- **메시지 데이터(BMG 등)**: 추출 → 번역용 텍스트(제어 코드는 태그로 표기) → 재패킹 도구를
  `tools/`에 만든다. 번역문은 원문을 포함하지 않는 차분 형태로 관리한다.

### 입력기 (QWERTY, 한/영 전환)

- 코어: `pc/src/ko/ko_ime.c`, `pc/include/ko_ime.h`, 테스트 `pc/tests/test_ko_ime.c` (87개 통과).
- 배열: **두벌식 표준**, US QWERTY 키 위치 그대로 (`r`=ㄱ, `R`=ㄲ, `k`=ㅏ, `o`=ㅐ, `O`=ㅒ ...).
- 조합 규칙: 겹모음(ㅘ ㅙ ㅚ ㅝ ㅞ ㅟ ㅢ), 겹받침(ㄳ ㄵ ㄶ ㄺ ㄻ ㄼ ㄽ ㄾ ㄿ ㅀ ㅄ), 받침 뒤에
  모음이 오면 마지막 받침을 다음 글자 초성으로 넘김(`각`+`ㅏ` → `가가`, `값`+`ㅏ` → `갑사`).
- 백스페이스는 자모 단위로 지운다(`값` → `갑` → `가` → `ㄱ` → 없음).
- **한/영 전환**: 조합 중인 글자를 확정한 뒤 모드를 바꾼다. 영문 모드에서는 입력이 그대로 통과.
  - 실제 키보드: `한/영` 키, 오른쪽 Alt, `Shift+Space` 중 아무거나 (키 설정에서 변경 가능하게).
  - 화면 키보드: 문자 입력 화면의 휠 자리에 QWERTY 배치와 `한/영` 키를 그린다(아래 "편집기 연결").
    게임패드로는 X가 한/영 전환.
- 출력은 유니코드(완성형 U+AC00–U+D7A3, 호환 자모 U+3131–U+3163)이고, 한글패치 인코딩으로의 변환은
  `pc/src/ko/ko_charset.c`(KS X 1001 순번 → 2바이트, 테스트 `pc/tests/test_ko_charset.c`)가 한다.
  조합 중인 낱자모는 패치의 자모 글리프(초성 19 + 중성 21)로 보인다. KS X 1001에 없는 음절(예: 똠)은
  조합 중에는 초성으로 보이고 확정하면 버려진다(패치 글꼴에 글리프가 없음).

#### 편집기 연결 (`pc/src/pc_typing.c`, `decomp/src/game/m_editor_ovl.c`의 TARGET_PC)

- 편집기가 열리면 키 입력이 편집 동작 큐(1바이트 글자, 한글 2바이트 글자, 조합 중 글자 교체·확정,
  커서, 백스페이스, Enter)로 바뀌고, 편집기가 다음 프레임에 원래의 명령 경로(`mED_edit_func_*`)로
  실행한다. 조합 중인 글자도 버퍼에 들어가 있으므로 이름·편지·게시판 어디서나 그대로 보인다.
- 한글은 패치와 같이 버퍼 2칸(바이트 또는 u16 2개)을 쓴다. 커서 이동·백스페이스는 글자 단위, 줄 폭은
  공백·쉼표·마침표 6, 나머지 12(패치의 `mED_get_col_line_width`와 같음). 편지 머리말의 이름 위치
  (`header_back_start`)도 2칸씩 옮긴다.
- 한글패치 디스크에서는 편집기가 열리면 바로 키보드 입력이 켜진다(영어 마을 이름은 영문으로 시작).
  `Tab`으로 끄고 켠다(꺼져 있으면 키보드가 게임패드 역할). 한/영: `한/영` 키, 오른쪽 Alt,
  `Shift+Space`. `Enter`는 여러 줄이면 줄바꿈, 한 줄이면 결정(편지 머리말·맺음말에서는 다음 칸),
  `Ctrl+Enter`/`Shift+Enter`는 끝내기.
- 패치는 가나 휠 대신 자체 한글 입력 UI를 넣고 휠의 텍스처를 그 UI용으로 바꿨는데, 이 UI는 재현하지
  않았다. 한국어 모드에서는 휠 입력을 끄고 휠 자리에 **화면 키보드**(`pc/src/pc_ko_editor.c`, 배치와 커서는
  `pc/src/ko/ko_osk.c`, 테스트 `pc/tests/test_ko_osk.c`)를 그린다.
  - 배치: 숫자 줄 + QWERTY 세 줄(한글 모드에서는 두벌식 자모, 영문 모드에서는 글자를 표시) + `한/영`·공백·`<-`
    (지우기)·`확정`. 자판은 실제 키보드와 같은 내장 입력기(`ko_ime.c`)로 조합하므로 받침 넘김·겹받침·자모
    단위 지우기가 똑같다. `^`(Shift)는 다음 한 글자에만 걸린다(ㄲ ㄸ ㅃ ㅆ ㅉ ㅒ ㅖ, 대문자, `!@#…`).
  - 게임패드(편집기가 입력을 받는 동안): 십자키·스틱 = 키 이동(누르고 있으면 반복), A = 누르기, B = 지우기,
    X = 한/영, Y = Shift, Z = 확정(여러 줄이면 줄바꿈), L/R = 커서 왼쪽/오른쪽, START = 끝내기. 키보드가
    쓰는 버튼은 게임에 넘기지 않는다(`pc_ko_osk_pad`, `pc_pad.c`). 특수 키 모서리에 해당 버튼 글자를 적어 둔다.
  - 터치: 키를 손가락으로 누르면 그 키가 눌린다(`pc_ko_osk_touch`, `pc_touch.c`가 화면 패드보다 먼저 묻는다).
    창 좌표는 와이드 화면 보정을 거쳐 게임의 2D 좌표로 바꾼다(`pc_gx_window_to_ui`).
  - 시스템 입력기 설정(`text_input = 1`)에서도 화면 키보드는 내장 입력기로 조합한다. 실제 키보드 입력은
    이전과 같다.
- 원본 디스크에서는 ACGC처럼 `Tab`으로 켜며, 일본어 글꼴에 확실히 있는 영문·숫자·공백·`!?,.`만 받는다.
- **시스템 입력기** (`settings.ini` `[Input]`의 `text_input = 1`, Android 기본값): 내장 두벌식 대신
  OS 입력기(Android 화면 키보드, Windows IME 등)를 쓴다. 편집기가 열리면 `SDL_StartTextInput`으로
  화면 키보드를 띄우고, 확정된 글자(`SDL_TEXTINPUT`)는 그대로, 조합 중인 글자(`SDL_TEXTEDITING`)는
  "조합 중" 표시를 달아 버퍼에 넣었다가 바뀔 때 지우고 다시 넣는다(`PC_TYPING_CMD_PREEDIT_CLEAR`).
  칸이 차서 조합 중 글자 일부만 들어가도 들어간 만큼 정확히 지운다. SDL 2.30의 Android 입력 연결은
  조합을 "백스페이스 + 새 글자"로 보내므로 그 경로로도 같은 결과가 된다. 게임패드 명령으로 조합이
  확정되면 `SDL_ClearComposition`으로 입력기 쪽 조합도 비워 같은 글자가 두 번 들어가지 않게 한다.
- 자동 테스트: `--input-script`의 `TYPE`(내장 입력기 키), `EDIT`/`TEXT`(시스템 입력기의 조합 중·확정
  텍스트) 명령(`pc/src/pc_input_script.c`), `make_opening_script.py --korean`. 화면 키보드는 스크립트의
  버튼(`DOWN`, `RIGHT`, `A`, `X`, `Y`, `B`, `Z`)과 `TOUCH`로 시험한다: 이름 입력에서 패드로 "한국"을 치고
  Shift·한/영·지우기, 터치로 `?`를 넣어 "한국?"이 되는 것을 확인했다. `make_opening_script.py --korean`은
  편집기가 열려 있는 동안 A를 누르지 않는다(화면 키보드의 키가 눌리므로).

## 알려진 문제

- 정적(static)으로 선언된 데이터 3개(`int_iid_diary`, `int_ike_molcenter_locker`, `int_ike_molcenter_wall`)는
  부팅 시 바이트 순서 보정을 못 하고 있음(빌드할 때 `gen_data_fixups.py`가 경고).
- 세이브는 PC의 바이트 순서 그대로 `save/card_a/`에 파일로 저장된다. 게임큐브 메모리 카드에서 꺼낸 세이브(GCI)와는
  호환되지 않는다(`pc_m_card.c`·`pc_save_bswap.c`는 ACGC에서 온 변환 코드로, 아직 빌드에 들어가지 않음).
- SD 카드, e-Reader, GBA 연동은 "장치 없음"으로 응답.
