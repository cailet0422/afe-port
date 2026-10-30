# PC 포트 개발 환경 (Linux 컨테이너)

새 컨테이너에서 이어서 작업할 때의 순서. 게임 데이터는 저장소에 없으므로 매번 준비한다.

## 1. 패키지

```sh
dpkg --add-architecture i386 && apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y gcc-multilib g++-multilib libsdl2-dev:i386 \
    libgl-dev:i386 libgl1-mesa-dri:i386 xvfb mesa-utils x11-apps x11-utils imagemagick xdotool gdb unifdef \
    binutils-powerpc-linux-gnu ninja-build cmake
```

단위 테스트(한글 입력기·화면 키보드·문자표·패치 적용기·메모리 카드)는 게임 데이터 없이 돌아간다(CI와 같음, 메모리 카드 테스트만
`decomp/` 서브모듈의 헤더가 필요):

```sh
cmake -S pc -B pc/build-tests -G Ninja -DPC_TESTS_ONLY=ON && ninja -C pc/build-tests && ctest --test-dir pc/build-tests
```

## 2. 디스크 이미지

디스크 이미지(일본판 Rev 1, CISO 약 33MB)는 사용자가 세션에 직접 올려준다. 저장소·문서에 다운로드 링크를
두지 않는다. Claude Code 세션에 올린 파일은 `/root/.claude/uploads/<세션>/` 아래에 이름 앞에 접두어가 붙어
생긴다(`.iso`라면 `game.iso`로 복사한다; 7z로 압축해 올렸으면 `apt-get install -y 7zip` 후 빈 폴더에서
`7z x`로 풀어 안의 `.ciso`를 쓴다):

```sh
mkdir -p /tmp/disc && cp /root/.claude/uploads/*/*.ciso /tmp/disc/game.ciso
python3 pc/tools/gcdisc.py extract-orig /tmp/disc/game.ciso decomp/orig/GAEJ01_01
```

디스크 이미지·추출 파일은 커밋하지 않는다(`decomp/orig/*/*`, `pc/build*/`는 무시됨). 서브모듈이 비어 있으면
먼저 `git submodule update --init --recursive`.

## 3. 매칭 빌드 (에셋 추출 겸 검증)

```sh
cd decomp && python3 configure.py --version GAEJ01_01 && ninja
sha1sum build/GAEJ01_01/static.dol build/GAEJ01_01/*/*.rel && cd ..
```

반드시 다음과 같아야 한다(디컴프 소스를 고칠 때마다 확인):
`7574bec2…` static.dol, `a3b987c9…` foresta, `49fd5f04…` forestd, `98246b73…` foresti, `73ddcf0d…` foresto
(전체 값은 README.md). 고친 파일은 `unifdef -UTARGET_PC -UBUGFIXES`로 원래 텍스트와 같은지도 본다.

## 4. PC 빌드와 실행

```sh
cmake -S pc -B pc/build32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake
ninja -C pc/build32          # 첫 configure는 foresti/foresto 생성으로 몇 분 걸림
mkdir -p pc/build32/bin/rom && cp /tmp/disc/game.ciso pc/build32/bin/rom/
(Xvfb :99 -screen 0 1280x720x24 &) ; export DISPLAY=:99 SDL_AUDIODRIVER=dummy
cd pc/build32/bin && ./DobutsuNoMoriEPlus --verbose
```

- `settings.ini`의 `msaa = 0`이어야 Xvfb에서 GLX 비주얼을 찾는다(첫 실행 후 수정).
- 크래시 조사는 `gdb -batch -ex run -ex bt --args ./DobutsuNoMoriEPlus --verbose`.
- 자동 입력: `python3 pc/tools/make_opening_script.py 16000 > /tmp/opening.txt` 후
  `./DobutsuNoMoriEPlus --no-framelimit --input-script /tmp/opening.txt` → `shots/*.ppm`
  (`montage`로 묶어 보면 편하다). llvmpipe에서 약 30–60fps.
- `--script-fast`를 더하면 스크린샷을 찍는 프레임만 그려서 약 400fps로 돈다(오프닝 6,000프레임이 16초).
  메뉴가 뒤에 깔아 두는 장면 복사본은 검게 나온다.
- `--deterministic`: 게임 시계를 2026-11-01 정오(UTC)에서 시작해 프레임 수로만 진행시키고, 오디오도 별도
  스레드 대신 화면 프레임마다 1/60초 분량씩 만든다. 같은 스크립트를 돌리면 매번 같은 화면이 나온다(마을 배치,
  옷, 주민 위치, 스크린샷까지). `--time H`와 같이 쓰면 그 날의 H시.
- 새 게임 → 마을: `python3 pc/tools/make_newgame_script.py --korean [단계...] > /tmp/ng.txt` 후
  `./DobutsuNoMoriEPlus --no-framelimit --script-fast --deterministic --input-script /tmp/ng.txt`
  (약 1분 반). 단계 형식은 스크립트 설명 참고. 자유 이동은 약 29,000프레임부터. 길을 만들 때는 `--pos`로
  스크린샷마다 플레이어·주민 위치, 카메라(모드, 중심, 눈, 각도), 가게·박물관·우체국 등이 있는 구역과 근처 건물
  액터의 위치를 찍고(`POS`, `--verbose`일 때 나옴; 한 구역은 640),
  주민이나 지점까지는 `SEEK:E038:600`처럼 걷게 한다(막히면 옆으로 비켜 감).
- 보통 속도 확인: `--no-framelimit` 없이(필요하면 `--script-fast`만) 돌리면 소리가 별도 스레드에서 돈다.
  `--deterministic`이 아니면 마을이 매번 달라 `--save` 경로가 저장까지 가지 못할 수 있지만 끝까지 크래시·멈춤
  없이 도는지 보는 데 쓴다(llvmpipe에서 모든 프레임을 그리면 30fps 안팎이라 시간이 두 배쯤 걸린다).
- 저장·불러오기 확인: `make_newgame_script.py --korean --save`가 옆집 주민(밀크)에게 인사하고 하니와에서
  저장한다(첫날 하니와는 주민을 한 명도 만나지 않았으면 저장을 거절한다). `save/card_a/DobutsunomoriE_MURA`가
  생기면 `make_opening_script.py 5200`으로 다시 실행해 이어하기 — 밀크가 맞이하고 집 앞에서 시작한다.
  저장하지 않고 끝낸 세이브를 불러오면 도루묵씨(리셋씨)가 나온다(원래 동작).
- 한국어 편집기 확인(한글패치 디스크, `--save`로 만든 세이브에서 시작):
  `python3 pc/tools/make_ko_edit_script.py 시나리오 > /tmp/k.txt` 후
  `./DobutsuNoMoriEPlus --no-framelimit --script-fast --deterministic --verbose --input-script /tmp/k.txt`
  (각 1–3분, 스크린샷은 `shots/k_시나리오_번호_단계.ppm`).
  - `letter`: 편지지를 받아(`GIVE 2000`) 밀크에게 편지 — 본문은 `TYPE`, 화면 키보드는 스틱·A와 `TOUCH`, 긴 줄
    넘김·지우기·끼워 넣기, 십자키로 머리말(이름 앞뒤)과 맺음말. 확인 후 소지품에서 "다시쓰기"로 다시 열고 하니와에서
    저장한다. 이어서 `letter-check`가 불러온 뒤 그 편지를 다시 연다.
  - `ephrase`: 밀크의 말버릇을 "하나둘이"로(`LEDIT 2 E038`, 7바이트로 압축됨: `CATCH`가
    `FE 8A 01 23 20 66 0B`) 바꾸고 말을 걸어 대사 끝에 나오는지 본 뒤 저장. `ephrase-check`는 불러와서 다시 확인.
  - `request`: 신청곡 편집기(`LEDIT 4`)에 "K.K.보사노바"(디스크에선 사전 코드), "아일랜드의 노래"(띄어쓰기 있이·없이),
    없는 곡을 넣고 `ITEM`이 2A0A, 2A13, 2A13, 2A8C(없음)를 찍는지 본다. 저장하지 않는다.
  - `reset`: `RESETS 6`으로 도루묵씨의 세 번째 방문을 불러 틀린 대답(몰라) → 다시 묻기 → 그가 시키는 문구를 띄어
    써서 입력 → "참말이제?!"(맞음). 문구는 세이브마다 다르다(`--save`의 세이브에서는 "정말죄송합니다").
  - 시나리오마다 `--save`로 만든 세이브를 새로 복사해 쓴다(`letter`·`ephrase`는 저장하므로 `*-check`는 그 뒤에).
- 메모리 문제는 `-fsanitize=address -fsanitize-recover=address`로 별도 빌드 디렉터리를 만들어 본다
  (`ASAN_OPTIONS=detect_leaks=0:halt_on_error=0`). 원래 게임의 읽기 범위 초과
  (`mFM_LoadBGCommonTex` 등)는 무해한 것도 있다.

## 5. 한글패치 디스크

패치 zip(v0.9.2)은 사용자가 세션에 올려준다(원 배포처에서만 받을 수 있음, 저장소에 넣지 않음).

```sh
unzip -q 패치.zip -d /tmp/kopatch
python3 pc/tools/apply_ko_patch.py /tmp/disc/game.ciso /tmp/kopatch/Doubutsu-no-Mori-e-plus-Korean-v0.9.2 /tmp/disc/ko.iso
mkdir -p pc/build32/bin_ko/rom && cd pc/build32/bin_ko
ln -sfn ../bin/DobutsuNoMoriEPlus . && ln -sfn ../bin/shaders . && ln -sfn ../bin/DobutsuNoMoriEPlus.relmap .
cp ../bin/settings.ini ../bin/keybindings.ini . && ln -sfn /tmp/disc/ko.iso rom/game.iso
```

로그에 `Korean patch detected`, `loaded N data objects from the disc`가 나오면 된다.
한글 입력 확인: `python3 pc/tools/make_opening_script.py --korean 9000 > /tmp/op_ko.txt`로 만든 스크립트를
돌리면 `shots/name_*.ppm`, `town_*.ppm`, `eng_*.ppm`에 단계별 화면이 남는다.
패치의 코드 분석은 `pc/tools/reldis.py hooks|dis`(패치된 foresta.rel은 `gcdisc.py extract`로 꺼냄)와
[korean_patch.md](korean_patch.md) 참고.

## 6. Windows 빌드와 Wine 확인

```sh
apt-get install -y gcc-mingw-w64-i686 g++-mingw-w64-i686 wine32:i386 wine
mkdir -p /tmp/sdl2mingw && curl -sSL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz | tar xz -C /tmp/sdl2mingw
cmake -S pc -B pc/buildwin -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-mingw32.cmake \
      -DSDL2_DIR=/tmp/sdl2mingw/SDL2-2.30.9/i686-w64-mingw32/lib/cmake/SDL2 -DPC_BUILD_TESTS=OFF
ninja -C pc/buildwin
cd pc/buildwin/bin && mkdir -p rom && ln -sfn /tmp/disc/ko.iso rom/game.iso && cp ../../build32/bin/settings.ini .
WINEPREFIX=/tmp/wineprefix WINEDEBUG=-all wine DobutsuNoMoriEPlus.exe --no-framelimit --script-fast \
      --input-script 'Z:\tmp\op_fast.txt'
```

- MinGW 차이: 비트필드 배치(`-mno-ms-bitfields`로 맞춤), PE 링커는 버려질 함수의 미정의 참조도 오류로 봄
  (`pc_stubs.c`), 링크 맵 형식(`.data$이름`, 4바이트 단위 크기; `gen_rel_overlay.py`가 둘 다 읽음).
- Wine에는 사운드 장치가 없어서 오디오가 SDL `dummy`로 대체된다(실제 Windows에서 출력 장치가 없을 때도 같음).
- Xvfb가 꺼지면 Wine은 "no driver could be loaded"로 창을 못 만든다.

## 7. OpenGL ES 경로 (Android용) 데스크톱 확인

`apt-get install -y libgles-dev:i386 libegl-dev:i386` 후 `-DPC_GLES=ON`으로 따로 빌드한다
(`pc/build32gles`). SDL이 ES 3.0 컨텍스트를 만들고, 셰이더의 `#version 330 core`는 `pc_gl.h`가
`#version 300 es`로 바꾼다. 로그에 `OpenGL OpenGL ES 3.x Mesa`가 나오면 된다.
