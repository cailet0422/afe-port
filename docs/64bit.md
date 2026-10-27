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

`PC_ALLOW_64BIT`가 없으면 CMake는 여전히 64비트 구성을 거부한다(아직 실행이 끝까지 되지 않음).

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

## 현재 상태

- 컴파일·링크 완료, 리눅스 x86_64에서 부팅해 아카이브 마운트·REL 데이터 오버레이·게임 루프까지 들어간다.
- 남은 것: 오디오(jaudio)는 디스크의 뱅크/파형 표를 그 자리에서 포인터로 재배치하는 구조라 64비트용
  변환이 필요하다(게임이 오디오 준비를 기다리므로 이게 끝나야 화면이 나온다). 액터를 가리키는 `u32`
  라벨(플레이어 그물·문·요청 구조체 등)을 넓혀야 한다. 그 뒤 arm64 APK.
