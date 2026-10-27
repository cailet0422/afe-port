# 동물의 숲 e+ PC 포트 / 한국어 패치

이 디렉터리는 afe-decomp를 기반으로 한 PC(Windows/Linux) 포트와, 이후 Android 포트 및
한국어 패치의 계획과 진행 상황을 정리한다.

## 원칙

- **매칭 빌드는 절대 깨지지 않는다.** 디컴프 소스(`src/`, `include/`)의 PC 관련 수정은 전부
  `#ifdef TARGET_PC`(일부 `BUGFIXES`, `PC_ENHANCEMENTS`) 안에만 들어간다. 소스를 고친 뒤에는
  `ninja`로 매칭 빌드를 다시 돌려 아래 SHA-1이 그대로인지 확인한다.

  | 파일 | SHA-1 |
  |---|---|
  | static.dol | `7574bec2692efc921c0975c52af41038f3fb7093` |
  | foresta.rel | `a3b987c9a0cc9764494ac1e6ec64dc6c5c16ead2` |
  | forestd.rel | `49fd5f046eb4d9ca9be98ab97c731c3553d9343c` |
  | foresti.rel | `98246b73d5ad3d14f2b61c4d8369d0ae52f14753` |
  | foresto.rel | `73ddcf0d1e979720c575f7ab0c5df2e9ff04eee0` |

- **게임 데이터는 저장소에 넣지 않는다.** 디스크 이미지, 디스크에서 추출한 에셋, 그것을
  포함한 빌드 결과물(`orig/*`, `build/`, `pc/build*/`)은 커밋/배포하지 않는다. 배포는 코드와
  패치(차분)만 하고, 사용자가 자기 디스크 이미지를 준비한다.
- PC 플랫폼 계층(`pc/`)은 [ACGC-PC-Port](https://github.com/FlyingMeta/ACGC-PC-Port)
  (MIT)의 것을 가져와 e+에 맞게 고쳤다. 라이선스 고지는 `pc/LICENSE`.

## 구조

```
게임 코드 (src/, 디컴프 그대로)  ── N64 디스플레이 리스트 ──▶ emu64 ──▶ GX ──▶ pc_gx (OpenGL 3.3)
        │                                                          SDL2: 창/입력/오디오
        ├─ Dolphin SDK / OS 호출 ─▶ pc_os, pc_dvd(디스크 이미지 직접 읽기), pc_card, pc_aram, pc_sdk_extra ...
        └─ 패미컴 ─▶ FixNES
```

- 4개의 REL 모듈(foresta/forestd/foresti/foresto)은 PC에서 실행 파일 하나로 정적 링크된다.
  foresti/foresto는 아직 C 소스가 없으므로 `pc/tools/gen_module_data.py`가 디컴프의
  `tools/converters/batch_assetdis.py`로 dtk 어셈블리에서 C 소스와 에셋을 생성한다.
- 에셋은 지금은 매칭 빌드가 추출한 `.inc`를 컴파일 시점에 포함한다(그래서 실행 파일 자체가
  배포 불가). 배포 전에 런타임 에셋 로더(디스크 이미지에서 읽기)로 바꿔야 한다.
- 원래 빅엔디언 원시 바이트(`u8[]`)로 들어 있는 데이터를 다른 곳에서 `u16`/구조체로 읽는
  경우가 있어, `pc/tools/gen_data_fixups.py`가 그런 블롭을 찾아 부팅 시 한 번 바이트 순서를
  바꾸는 표(`pc_data_fixups.c`)를 만든다.
- 디컴프가 포인터를 `u32`로 다루는 곳이 많아 **32비트(x86) 빌드만** 지원한다. 64비트
  (Android arm64 포함)는 M4에서 따로 다룬다.

## 빌드 (Linux, 32비트)

필요: `gcc-multilib g++-multilib cmake ninja-build python3 libsdl2-dev:i386 libgl-dev:i386`

1. 매칭 빌드 준비 (에셋 추출과 dtk split에 필요)
   ```sh
   # orig/GAEJ01_01/ 에 디스크에서 추출한 sys/main.dol 과 files/*.rel 을 둔다
   python3 configure.py --version GAEJ01_01
   ninja
   ```
2. PC 빌드
   ```sh
   cmake -S pc -B pc/build32 -G Ninja \
         -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake
   ninja -C pc/build32
   ```
   첫 configure는 foresti/foresto 생성 때문에 몇 분 걸린다.
3. 실행: 디스크 이미지(`.ciso`/`.iso`/`.gcm`)를 `pc/build32/bin/rom/`에 두고
   ```sh
   cd pc/build32/bin && ./DobutsuNoMoriEPlus --verbose
   ```

Windows는 MSYS2 **MINGW32** 셸에서 같은 순서로 빌드하는 것을 목표로 한다(아직 미검증).

## 마일스톤

| 단계 | 내용 | 상태 |
|---|---|---|
| M0 | `TARGET_PC` 훅 병합, 매칭 유지 검증, PC 빌드 링크 | 완료 |
| M1 | Linux/Windows에서 타이틀 화면까지 부팅 | 진행 중 (Linux에서 타이틀의 "Press Start Button!" 표시까지 확인, 3D 배경·로고는 아직 검게 나옴) |
| M2 | 마을 플레이, 세이브(메모리 카드 파일), e+ 전용 기능(SD/e-Reader/GBA)은 우회 | 예정 |
| M3 | 오디오 정상화, 패미컴(FixNES) | 예정 |
| M4 | 64비트 정리(포인터↔u32), Android(arm64, SDL2 + GLES3, 터치 입력) | 예정 |
| M5 | 한국어 패치(아래) | 입력기 코어 완료 |
| M6 | 런타임 에셋 로더, 배포 패키징 | 예정 |

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
  - 화면 키보드: 게임의 문자 입력 화면을 QWERTY 배치로 바꾸고 `한/영` 버튼을 추가한다.
    게임패드로는 버튼 하나를 한/영 전환에 할당한다.
- 출력은 유니코드(완성형 U+AC00–U+D7A3, 호환 자모 U+3131–U+3163)이고, 게임 인코딩으로의 변환은
  별도 계층에서 한다. 완성되지 않은 낱자모는 폰트에 호환 자모 글리프를 넣어 표시한다.
- 연결 지점: 편지/이름 입력 화면(`m_editor_ovl.c`, `m_ledit_ovl.c`)과 ACGC의 키보드 입력
  (`KEYBOARD_TYPING`) 경로.

## 알려진 문제 (M1 기준)

- 타이틀 데모의 3D 배경과 로고가 그려지지 않음(검은 화면에 글자만 보임).
- 정적(static)으로 선언된 TLUT 블롭 3개는 부팅 시 바이트 순서 보정을 못 하고 있음.
- e+ 세이브 경로는 아직 ACGC의 `pc_card.c` 위에서 그대로 컴파일만 되는 상태.
- SD 카드, e-Reader, GBA 연동은 "장치 없음"으로 응답.
- 향상 메뉴(ACGC의 설정/일시정지 메뉴) 글자는 e+ 폰트 문자표와 맞지 않음.
