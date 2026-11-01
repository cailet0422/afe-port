[English](README.md) · **한국어**

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
- 소리 경로: 게임의 음악·효과음·말소리는 N64식 합성기(Nas, "NEOS")가 CPU에서 만들고(`rspsim.c`), JAudio의 DSP
  채널(부팅 징글)은 게임큐브 DSP 대신 `pc_dsp.c`가 서브프레임(80샘플)마다 섞는다(ARAM의 AFC/PCM 파형, 4탭 리샘플,
  볼륨 램프, 앞쪽 버스만; 효과 송출은 뺌). 둘을 `Jac_VframeWork`가 560샘플(32kHz 스테레오)씩 합쳐 `AIInitDMA`로
  넘긴다. JAudio DVD 스레드의 작업 큐(ARAM 적재와 콜백)는 소리 스레드가 소리 프레임마다 처리한다.
  패미컴 중에는 원래 `EmuSound`처럼 NES 소리(FixNES APU, 약 224kHz를 게임 프레임당 1/60초로 리샘플)가 믹서
  콜백(N64식 음악)을 대신한다(`pc_nes_fixnes.c`).
- 원래 빅엔디언 원시 바이트(`u8[]`)로 들어 있는 데이터를 다른 곳에서 `u16`/구조체로 읽는
  경우가 있어, `pc/tools/gen_data_fixups.py`가 그런 블롭을 찾아 부팅 시 한 번 바이트 순서를
  바꾸는 표(`pc_data_fixups.c`)를 만든다. `static`으로 선언된 블롭(리셋 센터 방·의자, 일기장 가구의 팔레트 6개)은
  밖에서 이름을 부를 수 없으므로, 그 소스 파일에 `-include`로 넣는 헤더(`gen/static_fixups/`)가 블롭을 미리
  선언하고 표의 항목을 그 파일 안에서 정의한다(소스와 오브젝트 파일 이름은 그대로).
- 디컴프가 포인터를 `u32`로 다루는 곳이 많아 데스크톱은 **32비트(x86) 빌드**가 기본이다. 64비트
  (Android arm64용)는 `TARGET_PC` 안에서 포인터 크기를 따르도록 고쳐 리눅스 x86_64에서 새 게임 진행까지
  확인했다([64bit.md](64bit.ko.md), CMake `-DPC_ALLOW_64BIT=ON`).

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

### Windows (64비트)

- 리눅스에서 교차 빌드: `gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64`와 같은 SDL2 MinGW 패키지의 `x86_64-w64-mingw32`
  ```sh
  cmake -S pc -B pc/buildwin64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-mingw64.cmake \
        -DSDL2_DIR=<SDL2>/x86_64-w64-mingw32/lib/cmake/SDL2 -DPC_BUILD_TESTS=OFF -DPC_ALLOW_64BIT=ON
  ninja -C pc/buildwin64
  ```
- Windows는 LLP64(`long`이 4바이트)라 리눅스·Android(LP64)와 다르지만, 타입 정의는 `__LP64__`일 때만 바꾸므로
  `u32`/`s32`는 그대로 4바이트다. Windows에서만 나던 것: 부팅 징글(JAudio)의 표 사본은 포인터가 이미 주소인데,
  `PTconvert`가 표 주소를 32비트로 자른 값과 비교해 그보다 작은 포인터에 기준을 또 더했다. 리눅스에서는 사본이 4GB 위에
  있어 우연히 맞았고, Windows에서는 4GB 아래라 부팅 중에 죽었다(`Jac_InitHeap`). 64비트에서는 변환하지 않는다(afe-decomp `TARGET_PC`).
- Wine(64비트)으로 확인: 원래 디스크 오프닝(8장), 한글패치 디스크의 새 게임(65장), 한 번에 일주일(99장), 새해 카운트다운
  (17장)이 32비트 Windows판과 스크린샷·세이브까지 같다. `--audio-dump`의 소리는 리눅스판과 일부 구간(연못가의 계속 나는
  소리, 폭죽)에서 파형이 조금 다르다(구간별 크기는 같음; Windows 32비트판도 마찬가지라 수학 라이브러리의 부동소수점 차이로 보임).

아이콘: `pc/res/icon.png`(흰 배경을 뺀 512px 원본)에서 만든 `icon.ico`(7가지 크기, Windows 실행 파일 리소스),
`icon64.rgba`(64×64 RGBA, 빌드 때 `bin2c.py`로 넣어 `SDL_SetWindowIcon`으로 창·작업 표시줄에 표시), Android
`res/mipmap-*`(런처 아이콘과 Android 8+ 적응형 아이콘, 배경은 테두리의 초록 `#5AAB49`)를 쓴다.

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

한글패치는 원 배포처로 가는 페이지([retrodb 한글패치 — 동물의 숲 e+](https://retrodb.info/krpatch/7428/%EB%8F%99%EB%AC%BC%EC%9D%98-%EC%88%B2-e))에서 받는다.
한글패치는 이 저장소와 관계없는 별도 제작자(역전맞고)의 작업이며, 이 저장소에는 패치 파일·번역문·글꼴 데이터를
넣지 않는다.

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
   - v0.9.2와 v0.9.3을 확인했다. v0.9.3에서 바뀐 것(GBA e카드 화면, 구역 경계 달리기)은 디스크의 GBA 프로그램과
     패치 코드라 PC에서는 보이지 않고, PC가 읽는 데이터는 같다([korean_patch.md](korean_patch.ko.md)).

**직접 적용**: 패치에 들어 있는 도구(또는 `pc/tools/apply_ko_patch.py`)로 Rev 1 ISO에 적용한 뒤 그 ISO를
`rom/`에 넣어도 된다. 어느 쪽이든 실행 로그에 `Korean patch detected`가 나오면 한국어 모드다(글자 폭·한글
글리프·로고·이름표 등을 디스크에서 읽는다). 형식은 [korean_patch.md](korean_patch.ko.md) 참고.

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
- 게임큐브 버튼 뒤에 포트 자체의 **Map**(`M`, 게임패드는 기본으로 없음)이 있다(아래). 그 전의
  `keybindings.ini`가 이미 `M`을 게임큐브 버튼에 쓰고 있으면 Map은 비어 있는 채로 시작한다.

### 미니맵과 지도 키

`pc/src/pc_minimap.c`(상태, 입력, 그리기)와 `pc/src/pc_minimap_core.c`(게임·GL 없이: 구획 그림, 배치;
테스트 `pc/tests/test_minimap.c`).

- **미니맵**: 마을 야외(`SCENE_FG`, FG 필드, 섬과 바다는 제외)에서 서브메뉴·메시지 창·데모(대화, 이벤트)·
  페이드·와이프가 없을 때, 창 오른쪽 아래에 5 × 6 구획을 그리고 플레이어(보는 방향 표시)와 주민 집(세이브의
  집 위치)을 표시한다. 구획 그림은 게임 지도의 것(`kan_tizu_*_TA_tex_txt`, 32 × 32 CI4, RGB5A3 팔레트 2개)을
  `m_map_ovl.c`와 같은 방법(`mMP_set_map_texture_pal`, 다리 포함)으로 고른다. 그 표들은 `m_map_ovl.c` 안의
  static이라 `pc/tools/gen_minimap_tables.py`가 `pc/src/pc_minimap_tables.inc`로 옮긴다(CI가 `--check`로 확인).
- `game_main` 다음에 게임 상태를 읽고(`pc_minimap_game_frame`, `graph.c`가 이미 그 자리에서 부르는
  `pc_pause_menu_draw`에서 호출), 터치 패드처럼 완성된 화면 위에 OpenGL로 그린다(`pc_vi.c`). 그래서 디컴프는
  바꾸지 않았다. `touch_controls = 1`이면 더 작게, 패드 버튼이 비워 둔 구석에 놓인다(테스트가 패드 배치와
  겹치지 않는지 확인). 게임의 시계(`m_banti.c`, 가만히 서 있으면 4:3 화면 오른쪽 아래에 나옴)도 가리지 않는다:
  넓은 창에서는 그 옆 구석에, 4:3 창에서는 시계 바로 위에 놓인다. `[Gameplay]`의 `minimap = 0`으로 끈다(Settings → Gameplay → Minimap).
- **지도 키**: Map(기본 `M`)은 게임의 지도(`mSM_OVL_MAP`, `data0 = 1`: X 버튼이 여는 플레이어의 지도)를
  `mSM_check_open_MAP`의 조건에서 `map_flag`만 빼고 연다. 그래서 파출소에서 지도를 받기 전에도, 실내에서도
  열린다(섬의 별장과 바다 위는 X처럼 안 됨). 한 번 더 누르면 B를 눌러 닫는다. 미니맵을 터치하면 열리고
  (Android, PC에서는 왼쪽 마우스 버튼이 게임 조작이 아닐 때 클릭), 지도가 열려 있을 때 화면 패드 버튼 밖을
  터치하면 닫힌다. 입력 스크립트 명령 `MAP`이 키와 같은 일을 한다.

### 업데이트 (릴리즈 빌드)

`pc/src/pc_update.c`, `pc/src/pc_update_core.c`(네트워크·파일 없이: 버전, GitHub 릴리즈 JSON,
`SHA256SUMS.txt`, zip; 테스트 `pc/tests/test_update.c`), Android는 `Updater.java`와 `UpdateProvider.java`.

- 버전은 `pc/VERSION`이다(CMake가 `PC_PORT_VERSION`을 정의하고, Android `build.gradle`이 `versionName`을
  여기서 가져와 `versionCode`를 major × 10000 + minor × 100 + patch로 만든다). **릴리즈마다 빌드 전에 올리고**,
  릴리즈 태그는 `v<VERSION>`으로 한다.
- 릴리즈 빌드만 업데이트한다: CMake `PC_UPDATER`는 `PC_STRIP_ASSETS`일 때 기본으로 켜지고(Android 제외),
  Android 앱은 정식 키로 서명됐을 때만. 파일은 이름 끝으로 고른다: `-Windows-x64.zip`, `-Windows-x86.zip`,
  `-Linux-x86_64.zip`, `-Android.apk`. 릴리즈 파일 이름을 계속 이렇게 짓고 `SHA256SUMS.txt`도 함께 올린다.
- 시작할 때(`--deterministic`이나 입력 스크립트일 때는 안 함) 스레드가
  `api.github.com/repos/cailet0422/afe-port/releases`를 읽어, 초안이 아니고 이 빌드보다 새로우며 이 플랫폼의
  파일이 있는 가장 새 릴리즈를 고른다. 사전 릴리즈도 포함한다(지금까지 릴리즈는 모두 미리보기). 그러면
  일시정지 메뉴가 업데이트 화면(설정의 `SUB_UPDATE`)으로 열린다(타이틀 메뉴, 패미컴 게임, 글자 입력 중에는
  기다림). Settings → Gameplay → Version으로 직접 확인할 수도 있다.
- Windows / Linux: zip을 받아(WinHTTP, Linux는 `curl`이나 `wget` 실행) `SHA256SUMS.txt`와 릴리즈 목록의
  크기로 확인하고, zip의 최상위 폴더 아래 파일을 각각 기존 파일 옆에 `<이름>.new`로 쓴다. 그다음 기존 파일을
  `<이름>.old`로 바꾸고(Windows는 실행 중인 프로그램과 DLL의 이름 바꾸기는 되지만 덮어쓰기는 안 된다) 새 파일이
  그 이름을 갖는다. 중간에 실패하면 모두 되돌린다. `.old` 파일은 `update_old.txt`에 적어 두고 다음 시작 때
  지운다. **Restart now**는 새 프로그램을 경로로 실행한다(`pc_update_restart_path`; Linux의 `/proc/self/exe`는
  이름이 바뀐 옛 프로그램을 가리킨다).
- Android: APK를 캐시 폴더에 받아 같은 방법으로 확인하고 시스템 설치 화면에 넘긴다(Android 7 이상은
  `UpdateProvider`의 `content://<패키지>.update/update.apk`, 그 전은 파일). `REQUEST_INSTALL_PACKAGES` 권한이
  필요하다. "이 버전 건너뛰기"는 Android에서는 앱 설정에, PC에서는 `update_skip`에 남는다.

### 자동 테스트 입력

`--input-script 파일`로 프레임 단위 입력·스크린샷·종료를 재생할 수 있다
(형식은 `pc/include/pc_input_script.h`). `--no-framelimit`과 함께 쓰면 빠르게 돌릴 수 있다.
`KEY Escape`, `PAD dpdown`처럼 키·패드 이벤트도 보낼 수 있어 일시정지·설정 메뉴(키 설정, 한글패치
설치)를 시험할 수 있다. `POS`는 플레이어·주민 위치를 찍고, `SEEK`은 주민이나 지점까지 걷는다.
`--deterministic`과 함께 쓰면 같은 스크립트가 매번 같은 화면을 낸다(`--days N`이면 N일 뒤에서 시작, `--time H[:M]`이면 그
시각; 2026-11-01은 일요일이라 `--days 6 --time 20`이 토요일 밤).
`--audio-dump 파일.wav`는 소리 출력을 WAV로 남긴다(`--deterministic`이면 같은 스크립트가 같은 WAV, 분석은
`pc/tools/audio_stats.py`, [dev_setup.md](dev_setup.ko.md)).
`MAP`은 지도 키처럼 마을 지도를 열고 닫는다.
가끔만 나오는 장면을 위한 디버그 명령: `GIVE`(소지품에 아이템), `LEDIT`(이름 입력 편집기 열기: 주민의 말버릇
등), `RESETS`(저장 없이 끈 횟수 — 도루묵씨가 온다), `CATCH`(주민의 말버릇 바이트 출력), `ITEM`(메뉴가 고른
아이템 출력), `JOBDONE`(아르바이트를 게임이 끝내는 것처럼 끝냄: 심부름을 지우고 HRA 안내를 기다리게 하고 시계를 다시 돌림),
`NOJOB`(아르바이트 이벤트만 끔), `CLOCK h[:m]`(시계를 그 시각까지 앞으로: 새로 실행하지 않고 자정을 넘겨 다음 날로),
`FG x,z 아이템`(땅의 그 칸에 아이템을 놓음: `008D` 도루묵씨 구멍에 걸어 들어가면
리셋 센터로 떨어진다).

## 마일스톤

| 단계 | 내용 | 상태 |
|---|---|---|
| M0 | `TARGET_PC` 훅 병합, 매칭 유지 검증, PC 빌드 링크 | 완료 |
| M1 | Linux/Windows에서 타이틀 화면까지 부팅 | 완료: Linux, Windows(리눅스에서 MinGW 교차 빌드, Wine으로 실행 확인) |
| M2 | 마을 플레이, 세이브(메모리 카드 파일), e+ 전용 기능(SD/e-Reader/GBA)은 우회 | 진행 중: 한글 디스크로 역 → 너굴 → 집 구경·선택 → 자유 이동 → 소지품 화면까지 크래시 없음(`pc/tools/make_newgame_script.py`). 저장·불러오기: 토용군(하니와)에게서 첫 저장(첫날은 주민 한 명에게 인사해야 받아 줌), 이어하기, 덮어쓰기 저장, 저장 없이 끈 뒤 도루묵씨까지 확인(`make_newgame_script.py --save`, 메모리 카드 계층 테스트 `pc/tests/test_pc_card.c`). 아르바이트 끝(`JOBDONE`)부터 여러 날: 편지 보내기와 다음 날 답장, 토요일 밤 K.K. 신청곡, 대출 갚기와 증축(중간·큰 집·지하·2층), 섬 구입과 갑돌이의 배로 섬 왕복, 사진 메뉴·카드e·GBA·역의 SD 확인이 "없음" 문구로 끝남(`make_days_script.py`, `make_device_script.py`). 아르바이트를 너굴에게서 받아 끝까지(작업복, 심기, 인사, 배달 셋, 편지, 알림판: `make_job_script.py`). 한 번 실행으로 일주일(`week`: 입력 스크립트 `CLOCK`으로 자정을 넘기며 월요일 편지 → 저장·이어하기 → 화요일 답장 → 토요일 K.K.; 보통 속도로 약 12분, 소리가 빠른 실행과 샘플까지 같음). 이벤트: 브로커 텐트(`broker`, 빈터에 텐트, 여욱, 텐트 안), 추수감사절(`harvest`, 안내·프랭클), 새해 카운트다운(`countdown`, 연못가에서 자정을 넘겨 HAPPY NEW YEAR·폭죽); 리셋 센터(`FG`로 도루묵씨 구멍). 남은 것: 텐트 안에서 사기, 다른 계절 이벤트(낚시 대회·불꽃놀이·할로윈 등) |
| M3 | 오디오 정상화, 패미컴(FixNES) | 진행 중: 소리 확인 수단(`--audio-dump` WAV, `pc/tools/audio_stats.py`, 같은 스크립트 → 같은 WAV). 고친 것: 모노 출력(본체 설정을 모노로 보고해 마을 소리가 좌우 같았음 → 스테레오), Z 버튼으로 멈추던 것(SD 확인이 "확인 중"을 무한히 기다림), 패미컴 소리(원래처럼 믹서 콜백으로, 게임 소리 생산이 멈추던 것), 패미컴 중 소리 시스템이 프레임마다 두 번 돌던 것, 부팅 징글(소프트웨어 DSP, DVD 작업 큐, JAudio 표·`SOUNDID_`·`MixConfig`의 엔디언, 결정적 시계로 음이 끊기던 것). 패미컴: 가구를 얻어 놓고 실행 → 플레이 → L+R+Z로 방에 돌아오는 경로(`make_famicom_script.py`, 한글 디스크 대사 확인), 보통 속도에서도 끊김 없음, 64비트도 clang 32비트와 같은 화면·소리. 시간대 음악 24곡이 모두 남(`BGM` 디버그 명령). 64비트 부팅 징글(JAudio 파형·뱅크 표를 64비트에서 호스트 구조체로 다시 만듦, 32비트와 같은 소리). 패미컴 15종(`--game 0`–`14`; 15–18은 게임 데이터상 얻을 수 없는 빈 가구라 놓이지 않음) 모두 실행·플레이·복귀, NES 소리 있음(골프는 플레이 중 원래 배경음이 없음). 패미컴 최고 점수 저장(전에는 새 `DobutsunomoriP_F_SAVE.sav`가 0으로 채워져 "공통 세이브 영역이 깨짐"으로 저장하지 않았음; 세이브의 최고 점수가 타이틀에 나오고 플레이 뒤에도 남음). DSP 효과(리버브): 게임이 효과 라인을 켜지 않는다(`DFX_SetFxLine`은 초기화에서 `NULL`로만 불림) — 게임큐브에서도 송출되는 효과가 없으므로 섞지 않는 것이 원래 동작. 실제 플레이로 최고 점수 갱신: 핀볼(`--game 4 --keys A,SL,A,SR --every 50 --hold 20 --play 6000`)에서 TOP 016860을 얻어 저장되고, 다음 실행의 핀볼이 TOP 016860·1UP 000000으로 시작한다(64비트도 같은 화면·세이브). 남은 것: 귀로 듣는 확인(새벽 곡 몇 개의 음 높이) |
| M4 | 64비트 정리(포인터↔u32), Android(arm64, SDL2 + GLES3, 터치 입력) | 진행 중: 렌더러 OpenGL ES 3.0 지원(`-DPC_GLES=ON`, 리눅스 Mesa GLES로 오프닝·마을 확인), Android(armeabi-v7a) APK 빌드, 화면 게임패드(터치)·Android 키보드 한글 입력([android.md](android.ko.md)). 64비트: clang 32비트 빌드와 `--deterministic` 비교로 오프닝(52장)·한국어 새 게임 경로(202장 중 201장)가 같은 화면, 이름 입력 창·편집기 버튼 수정, 한국어 편집기 시나리오(편지·말버릇·저장·이어하기, 134장)도 같은 화면, APK에 arm64-v8a 포함([64bit.md](64bit.ko.md)). 건물 안(상점·박물관·우체국·에이블·파출소·집·주민 집, 71장), 가구·구매·기증 뒤 저장(44장, 세이브 바이트까지 같음)과 32↔64비트 세이브 교차 불러오기, 타이틀 데모, 밤·눈·비, 패미컴, GLES 64비트도 같은 화면(64비트 NPC에게 건넨 아이템, 세이브의 쓰레기 바이트, 상점 메뉴의 스택 넘침 수정). 섬은 32비트로 왕복 확인(배는 선착장 북쪽에서 걸어 들어갈 때 놓인다). 아르바이트 전 과정·여러 날(편지·답장·K.K.)·증축·섬 왕복·2층/지하·장치 없음 경로·부팅 징글도 64비트가 clang 32비트와 같은 화면·세이브. 남은 것: 실기기 확인 |
| M5 | 한국어 패치 | 사용자 한글패치 디스크 지원: 대사·로고·이름표·글자 폭 표시, 키보드 한글 입력(이름·마을 이름·영어 이름, 편지의 머리말·본문·맺음말, 말버릇·도루묵씨 대답·K.K. 신청곡 확인), 사전 코드·말버릇·조사 자동 선택, 패치가 옮긴 문자열·표, 게시판/일기/알림판 줄 폭, 설정 메뉴에서 패치 zip을 골라 바로 적용(`korean/`에 한글판 이미지를 만들고 다시 시작). 게임패드·터치용 화면 키보드(QWERTY 두벌식 + 한/영, 십자키로 커서). 패치의 UI 문자열·상수(시간·알람·비밀번호 입력 제목, 하니와 알람 메시지 줄 나누기, 알림판 날짜, 소지품 경고 24종·사진 메뉴 문구), 이름 색(음절 단위), 한글 말소리(초성·중성으로 가나 말소리 선택, `VOICELOG`로 확인), 알림판·일기 한글 입력 확인(`make_ko_edit_script.py ui\|warning\|boards\|voice`), SD·GBA 실패 문구(`make_device_script.py`), 편지 보내기·주민 답장·K.K. 신청곡 대답(`make_days_script.py`). 남은 것: 한글 편지는 짧은 것에만 답장이 온다(패치가 일본어 낱말 표로 답장 여부를 정하는 코드를 그대로 둠, 게임큐브와 같음), 말소리를 소리 파일로 비교 ([korean_patch.md](korean_patch.ko.md)) |
| M6 | 런타임 에셋 로더, 배포 패키징 | 진행 중: 에셋 없는 빌드(`-DPC_STRIP_ASSETS=ON`) — 실행 파일에 디스크에서 꺼낸 에셋 없이, 시작할 때 디스크에서 읽는다. 일반 빌드와 스크린샷·세이브가 같다. `dist` 타깃으로 Linux·Windows zip(Windows는 Wine에서 확인). Android APK `-PstripAssets` 빌드(두 ABI, 라이브러리에 에셋 없음; 창 아이콘을 디스크 에셋으로 잘못 보아 에셋 없는 빌드가 링크에 실패하던 것을 고침; Android 빌드의 생성 소스 에셋 7774개가 디스크 표에서 빠져 부팅 중 죽던 것을 고침, [android.md](android.ko.md)). Android는 x86_64 에뮬레이터에서 디스크 읽기·오프닝·타이틀까지 확인. 남은 것: 실제 Windows·Android 기기 확인 |

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
  - 게임패드(편집기가 입력을 받는 동안): 스틱 = 키 이동, 십자키 = 글의 커서 이동(위아래 줄, 편지에서는 머리말과
    맺음말까지; 게임의 원래 입력 화면처럼 스틱은 휠, 십자키는 커서), 둘 다 누르고 있으면 반복. A = 누르기,
    B = 지우기, X = 한/영, Y = Shift, Z = 확정(여러 줄이면 줄바꿈), L/R = 커서 왼쪽/오른쪽, START = 끝내기.
    키보드가 쓰는 버튼은 게임에 넘기지 않는다(`pc_ko_osk_pad`, `pc_pad.c`). 특수 키 모서리에 해당 버튼 글자를
    적어 둔다.
  - 터치: 키를 손가락으로 누르면 그 키가 눌린다(`pc_ko_osk_touch`, `pc_touch.c`가 화면 패드보다 먼저 묻는다).
    창 좌표는 와이드 화면 보정을 거쳐 게임의 2D 좌표로 바꾼다(`pc_gx_window_to_ui`).
  - 시스템 입력기 설정(`text_input = 1`)에서도 화면 키보드는 내장 입력기로 조합한다. 실제 키보드 입력은
    이전과 같다.
- **편지**(`mED_TYPE_LETTER`, 게시판 `m_board_ovl.c`): 머리말(받는 이 이름 앞뒤), 본문 여러 줄, 맺음말을
  키보드·화면 키보드(스틱·터치)로 쓴다. 줄 폭을 넘으면 다음 줄로 넘어가고, 지우기는 자모 단위, 커서는 글자 단위,
  머리말 길이 제한에서는 글자가 반으로 잘리지 않는다. 맺음말은 오른쪽 정렬인데 원래 코드는 칸당 12픽셀로
  자리를 잡아 한글(2칸에 한 글자)이면 커서와 어긋났다 — 한국어 모드에서는 글자 폭으로 정렬한다
  (`mBD_set_writing_footer`). 확인 → 소지품의 편지 칸 → "다시쓰기"로 다시 열면 그대로이고, 저장 → 이어하기
  뒤에도 같다. 입력 화면이 편지 아래쪽을 가리는 것은 원래 휠과 같고, 커서가 내려가면 편지가 따라 올라간다.
- **이름 입력 편집기**(`m_ledit_ovl.c`): 말버릇·도루묵씨에게 하는 대답·K.K.에게 하는 신청곡은 패치처럼 더 긴
  버퍼(8·18·18바이트, `pc_ko_ledit_buf`)에 입력한다. 말버릇은 4바이트에 들어가면 그대로, 아니면 `0xFE` + 12비트
  값 4개로 주민의 4바이트 칸과 그 뒤 3바이트 여백에 넣는다(`ko_charset_pack_ending`, 저장 파일에도 그대로
  남는다). 신청곡과 대답은 사전 코드를 펼치고 공백을 모두 뺀 뒤 곡 이름·도루묵씨의 문구와 비교한다
  (`pc_ko_ledit_match`). 입력한 공백에 붙는 "SP" 표시는 한국어 모드의 공백이 반 폭이라 반 크기로 그린다.
  자세한 것은 [korean_patch.md](korean_patch.ko.md).
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
  편집기가 열려 있는 동안 A를 누르지 않는다(화면 키보드의 키가 눌리므로). 편지·말버릇·도루묵씨·신청곡은
  `make_ko_edit_script.py`(저장된 마을에서 시작, [dev_setup.md](dev_setup.ko.md))로 시험한다.

## 알려진 문제

- 세이브는 PC의 바이트 순서(리틀엔디언) 그대로 `save/card_a/`에 파일로 저장된다. 게임큐브 메모리 카드에서 꺼낸
  세이브(GCI)는 `afe_gci` 도구로 가져오고 내보낸다(마을 `DobutsunomoriE_MURA`, 패미컴 공통 세이브
  `DobutsunomoriE_F_SAVE`; 사용법은 [dev_setup.md](dev_setup.ko.md)). 필드 표는 e+ 헤더에서 생성하고
  (`pc/tools/gen_save_swap.py`), 체크섬은 바꾼 바이트 순서로 다시 계산한다. 확인한 것: 합성 데이터의 왕복·필드별
  값·체크섬(`pc/tests/test_save_swap.c`), 실제 마을 GCI 하나(저장소에 없음)를 PC 형식으로 바꿔 체크섬이 맞고
  값(마을 id, 주민, 지갑, 집 크기)이 그럴듯하며, 다시 GCI로 바꾸면 데이터가 원본과 바이트까지 같음(32·64비트 같음).
  게임에서도 확인: 일본판에서 만든 실제 마을 GCI를 가져와 이어하기(주민의 질문 → 플레이어 고르기 → 마을, 집 크기·섬·
  소지품·집 안 가구가 그대로) → 하니와에서 저장 → 다음 날 이어하기(바꾼 소지품·벨이 남음) → GCI로 내보내기(체크섬 맞음) →
  그 GCI를 다시 가져오면 PC 세이브와 바이트까지 같고 이어한 화면도 같음. 64비트 빌드도 같은 화면·세이브, `afe_gci`의
  변환 결과도 32비트와 같음. 일본판에서 만든 세이브를 한글패치 디스크로 띄우면 세이브에 든 이름(주민 "エックスエル" 등)이
  한글 글리프와 섞여 보인다(패치가 그 글자 코드에 한글을 넣었으므로 게임큐브에서도 같음; 원래 디스크로는 그대로).
  **확인하지 못한 것**: 내보낸 GCI를 게임큐브(또는 Dolphin)에서 불러오는 것, 실제 패미컴 세이브 GCI(합성 데이터로만 시험). 패미컴 세이브를 템플릿 없이 내보내면 코멘트만 있고
  배너·아이콘이 없다(PC는 그 그림을 갖고 있지 않음; `--template`으로 원래 GCI의 그림을 쓸 수 있음). 플레이어
  파일(`DobutsunomoriE_PL_*`, 외출 중), 편지·디자인 파일 등 다른 파일은 아직 변환하지 않는다.
- SD 카드, e-Reader, GBA 연동은 "장치 없음"으로 응답(`EXIProbeEx` -1, GBA 호출 실패). 게임은 각자의 "없음" 문구로
  끝난다: 사진 메뉴의 SD 저장(전에는 `EXIProbeEx`가 0, "확인 중"이라 `mCsd_sdcard_slot_chk`가 무한히 돌아 A를 누르면
  멈췄다 — Z 버튼 멈춤과 같은 원인으로 M3에서 함께 고쳐짐), 우체국 카드e 단말(읽기·리더 준비하기), 에이블의 GBA 디자인
  보내기, 역에서 외출할 때의 SD 확인(메모리 카드 슬롯 B로 넘어가 기차를 타고 타이틀로 돌아감). 확인 경로는
  `pc/tools/make_device_script.py`. SD 작업 스레드(`osCreateThread2`)는 PC에서 시작할 때 끝까지 동기로 돌고,
  `JUTGba`의 스레드는 돌지 않지만 `mGcgba_ConnectEnabled`가 정해진 횟수 뒤 오류를 돌려준다.
- 소리: JAudio DSP의 효과(리버브) 송출은 섞지 않는다. 게임이 효과 라인을 켜지 않아(`DFX_SetFxLine(i, NULL, NULL)`뿐)
  게임큐브에서도 효과로 가는 소리가 없으므로 원래와 같다. 시간대 음악 중 새벽 곡 몇 개(2·3·4시)는 평균율 기준 ±30~40센트로
  측정되는데, 음이 드문 곡이라 측정 탓인지 실제로 어긋난 것인지 귀로 확인하지 못했다.
