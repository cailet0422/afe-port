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
  게임이 가끔만 여는 화면은 `MENU <이름> [인자0] [인자1]`(`ALARMIN`, `TIMEIN`, `HBOARD`, `PASSWORDMAKE`, `PASSWORDCHK`,
  `NOTICE`, `WARNING`, `CALENDAR`, `FILM` …, `pc/include/pc_input_script.h`)로 연다.
- 보통 속도 확인: `--no-framelimit` 없이(필요하면 `--script-fast`만) 돌리면 소리가 별도 스레드에서 돈다.
  `--deterministic`이 아니면 마을이 매번 달라 `--save` 경로가 저장까지 가지 못할 수 있지만 끝까지 크래시·멈춤
  없이 도는지 보는 데 쓴다(llvmpipe에서 모든 프레임을 그리면 30fps 안팎이라 시간이 두 배쯤 걸린다).
- 소리 확인: `--audio-dump 파일.wav`를 더하면 소리 출력(32kHz 스테레오 16비트, 볼륨 설정을 적용하기 전)을 WAV로
  남긴다. `SDL_AUDIODRIVER=dummy`에서도 된다. `--deterministic`이면 소리를 프레임마다 1/60초씩 만들므로 같은 스크립트가
  샘플 하나까지 같은 WAV를 낸다(프레임 F는 대략 샘플 F×32000/60). `python3 pc/tools/audio_stats.py 파일.wav
  --seg 이름:F1-F2 ... --gaps 200`이 구간(프레임)마다 길이·음량(RMS/피크 dBFS)·무음 비율·가장 긴 무음·튐·클리핑·
  DC·좌우 상관·가장 센 주파수를 보여 주고, `--gaps`는 소리 사이의 끊김, `--every 초`는 시간별 음량,
  `--compare 다른.wav`는 두 덤프가 같은지 본다.
- 소리 확인에서 본 것(한글 디스크, `--deterministic`): 오디오 덤프의 첫 0.7초가 부팅 징글(JAudio, `pc_dsp.c`),
  오프닝 음악의 피크는 A440 평균율 기준 평균 약 −1센트. 첫날 세이브(아르바이트 중)의 마을 음악은 시간과 무관한
  `BGM_INTRO_FIND_SHOP`이 원래 동작이라, 시간대 음악은 `BGM <id>`(입력 스크립트 디버그 명령, `audio_defs.h`의 번호:
  01–18이 0–23시)로 틀어 본다. 보통 속도에서도 `--audio-dump`가 실제 시간 길이로 생기고 `audio_stats.py --gaps`로
  끊김을 본다.
- 패미컴: `python3 pc/tools/make_famicom_script.py > /tmp/fc.txt`(옵션 `--game N`: 1DE8+4N의 패미컴 가구,
  `--play F`)를 `--save` 세이브로 돌리면 가구를 받아 집에 들어가 놓고(소지품 → 배치하기), 마주 보고 A → "놀자!" →
  종료 방법 안내 → 쿠루쿠루랜드 타이틀에서 START → 플레이 → L+R+Z → 방, 단계마다 `shots/fc_번호_단계.ppm`(약 1분).
  NES 소리는 대략 5400프레임부터 종료까지(`audio_stats.py --seg nes:5600-6700`). 가구는 플레이어 앞 칸(위·오른쪽)에
  놓이므로 반 칸 오른쪽으로 비켜 아래에서 마주 본다(맞닿고 정면이 아니면 A가 밀기·당기기가 된다).
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
  - `ui`: 디버그 명령 `MENU`로 시간 입력("시간을 맞춰보자"), 하니와 알람 두 가지("분 뒤에"/"분에"), 비밀번호 화면("마을"/"이름"),
    하니와 알람 메시지(한글을 넣어 폭으로 줄이 나뉘는지), 알림판(날짜 "2026/11/01")을 차례로 연다.
  - `warning`: 소지품 메뉴 경고 24종(`MENU WARNING n`)을 차례로 띄운다(패치가 옮긴 긴 문구).
  - `boards`: 알림판과 일기(달력 → 오늘 → 일기 쓰기)에 한글을 써서 올리고, 비밀번호 확인 화면("암호 입력")과 사진 메뉴
    ("사진을, SD카드에 저장?")를 연다. 사진 메뉴에서 A를 누르면 SD 저장을 시도하다 멈추므로(PC에 SD 없음) B로 닫는다.
  - `voice`: `VOICELOG 1`을 켜고 밀크에게 말을 건다. 로그의 `[VOICE] msg 055C 1BDC U+C557 voice 1`은 메시지 번호, 바이트,
    글자, 고른 말소리 번호(가나 말소리 표의 번호, 둘째 바이트는 나오지 않음).
  - 시나리오마다 `--save`로 만든 세이브를 새로 복사해 쓴다(`letter`·`ephrase`는 저장하므로 `*-check`는 그 뒤에).
- 건물 안 확인(한글패치 디스크, `--save`로 만든 세이브에서 시작, 다음 날로 돌린다):
  `python3 pc/tools/make_interior_script.py 건물 > /tmp/i.txt` 후
  `./DobutsuNoMoriEPlus --no-framelimit --script-fast --deterministic --days 1 --verbose --input-script /tmp/i.txt`
  (건물 하나에 1–3분, 스크린샷은 `shots/i_건물_번호_단계.ppm`, `POS`가 장면 번호를 찍는다).
  - 경로는 `WARP x,z [방향]`(그 자리에서 마을을 다시 불러옴, 건물에서 나올 때처럼)으로 문 앞에 가서 `NOJOB`(너굴
    아르바이트를 끝냄: 첫날 세이브는 아르바이트 중이라 가게·집이 평소와 다르다)을 하고 게임대로 문으로 들어간다.
    문 위치·방향은 스크립트의 `DOORS`(그 세이브의 마을 기준).
  - `shop`, `museum`(홀·부엉 방·물고기 방), `post`, `tailors`, `police`, `myhouse`, `house주민id`(예 `houseE015`),
    `all`(앞의 것과 주민 집 둘을 한 번에).
  - `state`: 집에 상자를 놓고(소지품 → 배치하기; 놓은 가구를 마주 보고 B를 누르면 다시 집어 든다), 부엉에게 물고기를
    기증하고(`GIVE`), 너굴 상점 서쪽 벽의 물건을 사고(`BELLS`) 하니와에서 저장한다. `state-check`는 그 세이브를
    불러와 같은 방을 돌고 다시 저장한다(다른 빌드의 세이브로도).
  - `town`(마을 몇 군데, `--days`·`--time`과 같이: 밤, 눈), `town-rain`(`WEATHER 1 3`으로 큰비; 아르바이트 중에는
    게임이 비를 맑음으로 바꾼다), `famicom`(집에 패미컴을 놓고 켠다, 돌아 나오지 않음).
- 타이틀 데모: `make_opening_script.py --idle 12000`(아무것도 누르지 않음, 300프레임마다 스크린샷).
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
