# 64비트 빌드 (진행 중)

최신 Android 기기 상당수(예: Pixel 7 이후, 2024년 이후 플래그십)는 32비트 앱을 실행하지 못하므로
arm64-v8a 빌드가 필요하다. 디컴프 코드는 포인터를 `u32`에 담는 곳이 많아, 32비트 빌드와 매칭 빌드는
그대로 두고 `#ifdef TARGET_PC` 안에서 포인터 크기를 따르도록 고친다.

## 빌드

리눅스 x86_64에서 먼저 시험한다(같은 문제가 arm64에도 그대로 나타나고 gdb로 보기 쉽다).

```
CC=clang CXX=clang++ cmake -G Ninja -S pc -B pc/build64 -DPC_ALLOW_64BIT=ON \
    -DSDL2_DIR=<64비트 SDL2>/lib/cmake/SDL2 -DPC_BUILD_TESTS=OFF
```

`PC_ALLOW_64BIT`가 없으면 CMake는 여전히 64비트 구성을 거부한다(아직 모든 장면을 점검하지 않음).

## 방침

- **타입**: LP64(리눅스·안드로이드 64비트)에서는 `long`이 8바이트라 `u32`/`s32`(원래 `unsigned long`/
  `long`)를 `__LP64__`일 때만 `int`로 정의한다(`types.h`, `dolphin/types.h`, `PR/ultratypes.h`,
  `msl_types.h`, `pc_types.h`). `long`으로 선언된 SDK 함수 중 `s32` 선언과 겹치는 것(CARD, SI,
  JKR*의 `long` 인자)은 PC 쪽 선언을 `s32`로 맞춘다.
- **디스플레이 리스트**: `Gfx`의 두 번째 워드 `w1`을 `uintptr_t`로 넓힌다(64비트에서 `Gfx`는 16바이트).
  주소를 담는 GBI 매크로(`gDma*p`, `gSPVertex`, `gImmp1/21`, `gSetImage`, `gSPLoadUcodeEx`, Dolphin
  확장의 TLUT/텍스처 이미지)는 `gbi.h`/`gbi_extensions.h` 끝의 `TARGET_PC` 블록에서 캐스트만 넓혀 다시
  정의한다. 비트필드 구조체(`Gdma`, `Gsetimg2`, `Gmovemem` …)는 첫 워드 뒤에 `GBI_PC_W1_PAD`를 넣어
  두 번째 워드가 `w1` 자리(8바이트 뒤)에 오게 하고, 주소 필드는 `uintptr_t`로 한다. emu64의 세그먼트
  표·DL 스택·`seg2k0`도 `uintptr_t`.
- **디스크 구조체 안의 포인터**: 디스크 데이터에 그대로 겹쳐 읽는 구조체에 런타임 포인터 칸(디스크에선 0)이
  있으면 크기를 바꿀 수 없다. RARC 파일 엔트리의 `mData`는 `PCPtr32`(`pc/include/pc_ptr32.h`, 64비트에서는
  포인터 표의 인덱스, 32비트에서는 포인터 그대로)로 바꿨다.
- **ARAM·DMA·콜백**: 메인 메모리 주소를 `u32`로 받던 `ARStartDMA`/`ARQPostRequest`/`JKRAramPiece`/
  `JKRAramPcs`와 포인터를 `u32`로 넘기던 콜백(`ARQCallback`, JKR 명령 콜백)을 `uintptr_t`로 바꿨다.
- **PC 계층**: `DVDFileInfo`를 고정 오프셋 대신 같은 배치의 구조체로 읽고, `GXTexObj`/`GXTlutObj` 안의
  포인터는 두 칸에 memcpy로 담으며, 텍스처 캐시·EFB 캡처의 키를 `uintptr_t`로 한다.
- **포인터→정수 캐스트**: clang 경고(`-Wpointer-to-int-cast` 등, 디컴프 소스는 평소 `-w`)가 알려 주는 열의
  캐스트만 `uintptr_t`/`intptr_t`로 바꿔 원래 줄을 `#else`에 남기는 도구로 일괄 처리했다(약 250줄). 결과가
  다시 `u32` 변수·필드·인자에 들어가는 곳은 `-Wshorten-64-to-32`로 찾아 그 선언을 넓힌다(메뉴 인자
  `data3`/`param3`, 플레이어 옷 텍스처 주소, 구조물·곤충 액터의 작업 칸 등).
- **`long`**: libultra의 `Mtx_t`(`long[4][4]`)는 LP64에서 두 배가 되어 emu64가 읽는 행렬이 깨지므로
  `s32`로 한다.
- **`Gfx`를 `u64`로 다루는 곳**: emu64가 결합기(`G_SETCOMBINE*`)·`G_TEXTURE`·삼각형 명령을 `u64`
  하나로 복사·비교하던 곳은 64비트에서 `w1`을 잃으므로 `Gfx` 전체를 복사·비교한다.
- **메시지 큐**: `OSMesg`는 포인터 크기라 4바이트 변수로 받으면 스택을 덮어쓴다. 받는 곳은 `OSMesg`로
  받은 뒤 `u32`로 줄인다(오디오 스레드, 초기 메뉴, NES 사운드 이벤트).
- **오디오**: N64식 오디오(Nas)의 뱅크 표는 64비트에서 디스크 데이터를 그 자리에서 고치지 않고 호스트
  배치의 구조체로 새로 만든다(`Nas_BankOfsToAddr64`). 오디오 명령(Acmd)의 주소 칸, 콜백·멜로디 데이터
  같은 포인터 인자(`Nap_SetPtr`), DVD 작업 큐의 콜백 복사 크기를 포인터 크기에 맞췄다. 부팅 징글
  (JAudio)은 PC에서 원래 소리가 나지 않으므로 64비트에서는 등록하지 않는다.
- **액터 라벨**: 플레이어의 그물·문·눈덩이·모기 요청 등 액터를 가리키는 `u32` 라벨, 구조물의
  `arg1`/`arg3`를 `uintptr_t`/`intptr_t`로 넓혔다. 비교만 하는 ID(효과음 위치, 셔터 라벨)는 그대로 둔다.
- **고정 크기 풀**: NPC 액터 풀의 칸(0xBA0)은 64비트에서 NPC 구조체가 커지므로 0xE00으로 한다.

## 현재 상태

- 리눅스 x86_64에서 타이틀·오프닝과 새 게임 진행(이름 입력, 역 도착, 너굴과 집 고르기, 자유 이동)이
  32비트와 같은 화면으로 동작한다.
- 남은 것: arm64 APK, 그 뒤 여러 장면(가게, 박물관, 패미컴 등)의 64비트 점검.
