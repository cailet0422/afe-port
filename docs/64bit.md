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
- **원시 바이트 디스플레이 리스트**: `u8 x[] = { #include "....inc" }`로 추출되어 `gsSPDisplayList`로 불리는
  리스트 11개(이름 입력 창의 `nam_win_modeT`, 편집기 결합기 `lat_sousa_*_combine`, 달력·일기·편지 등)는 디스크의
  8바이트 명령 그대로라 64비트 `Gfx`(16바이트)로 읽으면 두 명령씩 섞인다(이름 입력 창과 연필, 편집기의 A/B
  버튼 글자가 흰 덩어리로 나왔다). `gen_data_fixups.py`가 이들을 `PC_FIX_GFX`로 표시하고, 부팅 때 바이트 순서를
  바꾼 뒤 64비트에서는 호스트 배치의 사본을 만들어 emu64의 `G_DL`이 그 사본을 실행한다(`pc_raw_dl_host`).

- **공용체를 다른 멤버로 쓰고 읽는 곳**: 소지품에서 NPC에게 건네는 요청(`mPlib_request_main_give_from_submenu`)은
  공용체의 `give`로 쓰고 `give_from_submenu`로 읽는다. 32비트에서는 앞의 포인터+int 둘과 `xyz_t`가 같은 12바이트라
  맞았지만 64비트에서는 아이템이 0으로 읽혀 건넨 물건이 NPC 손에 그려지지 않았다. 읽는 쪽 멤버로 쓴다.
- **초기화하지 않은 버퍼가 세이브에 들어가는 곳**: 게시판 자동 글(`mNtc_set_auto_nwrite`)은 스택의 글 구조체를
  일부만 채워 저장한다. 게임에는 보이지 않지만 32·64비트 세이브가 달라지므로 0으로 비우고 쓴다.

## 점검 방법

32비트 기본 빌드(gcc)와 64비트 빌드(clang)는 같은 `--deterministic` 스크립트로도 화면이 다르다. 두 컴파일러가
함수 인자를 계산하는 순서가 달라서(gcc는 오른쪽부터, clang은 왼쪽부터), 인자 둘이 난수를 부르는 곳
(`aNPC_set_tex_anime(..., 32 + RANDOM(16), 1 + RANDOM_F(3))` 등)에서 난수가 다른 곳에 쓰이고, 마을 배치·옷·주민
움직임이 갈라진다. 그래서 64비트는 **clang 32비트 빌드와** 비교한다.

```
sed 's/gcc)/clang)/; s/g++)/clang++)/' pc/cmake/Toolchain-multilib32.cmake > /tmp/Toolchain-clang32.cmake
cmake -S pc -B pc/build32clang -G Ninja -DCMAKE_TOOLCHAIN_FILE=/tmp/Toolchain-clang32.cmake -DPC_BUILD_TESTS=OFF \
      -DCMAKE_CXX_FLAGS="-m32 -mlong-double-64 -fno-aligned-new"
```

(`-malign-double`과 libstdc++의 `long double` 표, 32비트 clang의 정렬 `new`를 피하는 플래그. 비교용 빌드에만 쓴다.)
각 빌드 폴더의 relmap을 그 실행 파일 옆에 두고 돌린 뒤 스크린샷을 `compare -metric AE`로 비교한다.

세이브가 필요한 경로는 gcc 32비트 빌드가 `make_newgame_script.py --korean --save`로 만든 첫날 세이브에서 시작한다
(clang 빌드의 마을은 그 경로가 저장까지 가지 않는다). 건물 안은 `make_interior_script.py`(`--days 1`로 돌린다,
[dev_setup.md](dev_setup.md)), 세이브는 `cmp`로 바이트를 비교한다(파일 안에 같은 세이브가 두 벌, 0x10000과
0x3E000에서 시작).

## 현재 상태

리눅스 x86_64에서 clang 32비트와 같은 `--deterministic` 스크립트로 비교한 결과(장 수는 스크린샷):

| 경로 | 결과 |
|---|---|
| 원래 디스크 오프닝 | 52장 같음 |
| 한국어 새 게임 → 자유 이동 → 동쪽 구역 주민 | 202장 중 201장 같음(1장은 플레이어 팔 자세가 한 프레임 다름, 다음 장면부터 같음; 32·64비트 libm 실수 오차로 보임) |
| 한국어 편집기 6개 시나리오(`make_ko_edit_script.py`: 편지·저장·다시 열기, 말버릇·저장, 신청곡, 도루묵씨) | 134장 같음 |
| 건물 안(`make_interior_script.py all`: 너굴 상점, 박물관 홀·부엉 방·물고기 방, 우체국, 에이블, 파출소, 플레이어 집, 주민 집 둘) | 71장 같음 |
| 저장 경로(`state`: 집에 가구 놓기, 박물관 기증, 상점에서 사기, 하니와에서 저장) | 44장 같음, 세이브 파일 바이트까지 같음 |
| 불러오기(`state-check`: 32비트 세이브를 64비트가, 64비트 세이브를 32비트가 불러와 같은 방을 돌고 다시 저장) | 35장 같음, 다시 저장한 파일도 같음. 가구·산 물건(SOLD OUT, 지갑)·기증(박물관 표시 비트)이 남아 있음 |
| 타이틀 데모(원래 디스크·한글패치, `make_opening_script.py --idle 12000`) | 38장씩 같음 |
| 다른 날·시간(`town`: 첫날 밤, 11/4 아침, 눈 오는 12/21, 2/9 저녁, `town-rain`: 큰비) | 13장씩 같음 |
| 패미컴(`famicom`: 집에 놓고 켜기, 크루크루랜드) | 14장 같음 |
| GLES 64비트(`-DPC_GLES=ON`, Mesa GLES 3.2) | 건물 안 71장·타이틀 데모 38장이 데스크톱 GL 64비트와 같음 |

gcc 32비트 빌드도 위 경로를 모두 크래시 없이 돈다(마을이 달라 화면은 비교하지 않음). 64비트 크래시 없음.

- 고친 것: 이름 입력 창·편집기 버튼(원시 바이트 디스플레이 리스트, 위). 소지품의 아이템 메뉴 낱말
  (`mTG_tag_word_c`, 앞 8바이트만 디스크에서 덮어쓰는 구조체)이 64비트에서는 구조체가 커서 REL 오버레이에서
  빠져 일본어로 나오던 것. 박물관에 기증한 물고기가 부엉 손에 그려지지 않던 것(공용체, 위). 32·64비트 세이브가
  게시판 자동 글의 쓰레기 바이트만큼 다르던 것(위).
- 32비트에서도 있던 것: 너굴 상점의 여섯 선택지 메뉴(아르바이트가 끝난 뒤)에서 gcc 32비트가 스택 보호로 죽었다.
  선택지 문자열을 72바이트 읽어 66바이트 칸에 넣는 원래 코드라 마지막 칸이 호출한 쪽 배열 밖을 덮었다
  (`mChoice_Load_ChoseStringFromRom`, `TARGET_PC`에서 72바이트 버퍼에 읽어 맞는 만큼 복사).
- Android APK에 arm64-v8a 라이브러리를 함께 넣는다([android.md](android.md)). arm64 실행은 기기가 없어
  확인하지 못했다(같은 LP64 모델인 x86_64로 확인). 이번 점검으로 Android 빌드 설정은 바뀌지 않았다.
- 남은 것:
  - 섬: 첫날 세이브(아르바이트 중)와 그다음 날 모두 부두에 배(뱃사공 Kapp'n)가 오지 않아 가지 못했다. `WARP`로 섬 블록에 바로
    옮기면 섬 데이터가 준비되지 않아 32·64비트 모두 죽는다(배를 타야 섬 집·주민이 설정됨). 배가 오는 조건을 찾아
    경로를 만들어야 한다.
  - 2층·지하(집 증축 뒤), 역(들어가는 건물이 아님), 브로커 텐트·이벤트 장면은 경로가 없다.
  - arm64 실기기.
