# PC 포트 개발 환경 (Linux 컨테이너)

새 컨테이너에서 이어서 작업할 때의 순서. 게임 데이터는 저장소에 없으므로 매번 준비한다.

## 1. 패키지

```sh
dpkg --add-architecture i386 && apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y gcc-multilib g++-multilib libsdl2-dev:i386 \
    libgl-dev:i386 libgl1-mesa-dri:i386 xvfb mesa-utils x11-apps x11-utils imagemagick xdotool gdb unifdef \
    binutils-powerpc-linux-gnu ninja-build cmake
```

단위 테스트(한글 입력기·화면 키보드·문자표·패치 적용기·메모리 카드·GCI 세이브 변환)는 게임 데이터 없이 돌아간다(CI와 같음,
메모리 카드와 GCI 변환 테스트만 `decomp/` 서브모듈의 헤더가 필요):

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
  `--game 0`–`14`가 디스크의 15종(0 쿠루쿠루랜드, 1 벌룬 파이트, 2 동키콩 … 13 익사이트바이크, 14 와리오의 숲)이고,
  15–18은 얻을 수 없는 빈 가구라 "이 방향엔 못 놓아!"가 나온다. 최고 점수는 `save/card_a/DobutsunomoriP_F_SAVE.sav`
  (헤더 0x40바이트 + 플레이어마다 0x660바이트, 게임 종료 때 `nesinfo_tag_process3`가 WRAM의 점수를 옮김)에 남는다.
  "WRAMのハイスコアが更新されました"가 게임 중 점수가 세이브 데이터로 옮겨진 것이고, "共通セーブ領域が壊れているのでセーブしません"이
  나오면 저장되지 않은 것이다(게임 종료 때 `nesinfo_tag_process3` 아래 찍히는 바이트와 "初期値"는 태그의 초기값이지 저장된
  점수가 아니다). 실제 플레이로 점수를 내려면 `--keys`(누를 버튼을 차례로, 기본 `SR,SD,SL,SU`)·`--every`·`--hold`:
  핀볼은 `--game 4 --keys A,SL,A,SR --every 50 --hold 20 --play 6000`이면 공이 저절로 점수를 내 TOP이 01만 점대가 된다. 저장된 점수를 바꿔 보려면 플레이어 데이터의 바이트를 고치고 그 0x660바이트의 합이 0(mod 256)이
  되게 셋째 바이트를 맞춘다(벌룬 파이트 TOP은 플레이어 0의 0x4C부터 5바이트, 낮은 자리부터).
- `static` 팔레트(부팅 때 바이트 순서 보정, README "구조"): 일기장 가구는 `make_famicom_script.py`의 `GIVE 1DE8`을
  `GIVE 3150`(분홍일기장, `FTR_IID_DIARY`)으로 바꿔 방에 놓으면 분홍색이어야 하고(보정이 없으면 연두색), 리셋 센터는
  `--save` 세이브에서 집 앞(2128,1488) 남쪽 칸에 `3000 FG 2140,1540 008D`(도루묵씨 구멍)를 놓고 `3020 SD 30`으로
  걸어 들어가면 떨어져 들어간다(장면 52, 약 1000프레임 뒤). 벽·바닥·의자가 나무색이어야 한다(보정이 없으면 보라·초록 얼룩).
  빌드할 때 `gen_data_fixups.py`가 "N data fixups, 6 of static blobs"를 출력한다.
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
    ("사진을, SD카드에 저장?")를 연다(B로 닫는다; A로 저장을 시도하는 것은 아래 `make_device_script.py film`).
  - `voice`: `VOICELOG 1`을 켜고 밀크에게 말을 건다. 로그의 `[VOICE] msg 055C 1BDC U+C557 voice 1`은 메시지 번호, 바이트,
    글자, 고른 말소리 번호(가나 말소리 표의 번호, 둘째 바이트는 나오지 않음).
  - 시나리오마다 `--save`로 만든 세이브를 새로 복사해 쓴다(`letter`·`ephrase`는 저장하므로 `*-check`는 그 뒤에).
- 없는 장치 확인(한글패치 디스크, `--save`로 만든 세이브, `--days 1`):
  `python3 pc/tools/make_device_script.py 시나리오 > /tmp/d.txt` 후 건물 안 확인과 같이 실행(각 1분 안팎,
  스크린샷 `shots/d_시나리오_번호_단계.ppm`). 어느 것도 멈추지 않고 게임의 "없음" 문구로 끝나야 한다.
  - `film`: 사진 메뉴(`MENU FILM`)에서 SD 저장 → "SD카드 없음." → 어댑터 안내 → 다시 저장? → 한 번 더 → "사진을 저장할 수
    없었어요.".
  - `terminal`, `terminal-send`: 우체국 카드e 단말(60,220; x>90, 43 안에서 서쪽을 보고 A)에서 "카드e로 읽기"·"리더
    준비하기" → "게임보이 어드밴스가 제대로 연결되지 않았습니다.". 메시지 속도 때문에 한 번 대화한 뒤 같은 자리에서 다시
    고르는 것은 타이밍이 흔들려 시나리오를 나눴다.
  - `tailors`: 고순이에게 "어드밴스 얘기" → GBA에 디자인 도구 기록 → "제대로 연결이 안 된 것 같아예.".
  - `station`: `NOJOB`을 마을을 불러오기 전에(역무원은 생길 때 아르바이트 여부를 본다) 하고 역무원에게 "외출할 거야" →
    SD 확인 → 슬롯 B 메모리 카드에 플레이어, 슬롯 A에 마을 기록 → 기차 → 타이틀. 세이브가 바뀐다(`save/card_b/`).
  - `all`: `film`, `terminal`, `tailors`.
- 아르바이트(한글패치 디스크, `--save`로 만든 첫날 세이브에서, `--days` 없이):
  `python3 pc/tools/make_job_script.py 단계 > /tmp/j.txt` 후 실행하고 `save/`를 복사해 다음 단계에 쓴다(단계마다 1–2분, 하니와에서
  저장하고 타이틀로 돌아감). `plant`(작업복: 소지품에서 집기 → 플레이어 위에 놓기 / 꽃씨·묘목 10개 심기 — 가게 남동쪽
  z 1260 줄, 처음 두 자리는 안 받아서 아래 줄에 다시), `hello`(주민 여섯과 신사 옆 촌장 D07F에게 인사; 그 시각 집 안에 있는
  주민은 집에 들어가서), `furniture`(가구를 E08C에게), `letter`(너굴의 편지지로 루시에게 편지 → 펠리), `carpet`(E09D),
  `axe`(E09E), `notice`(집들 북쪽 알림판 2220,1580에 글) → 너굴이 가게 주인으로 맞이함(아르바이트 끝). 받는 주민은
  `POS`의 `first job N progress P to XXXX`로 본다. 대화는 `TALK`/`CHOOSE`/`TALKEND`로 기다린다.
- 여러 날(한글패치 디스크, `--save`로 만든 세이브):
  `python3 pc/tools/make_days_script.py 시나리오 > /tmp/n.txt` 후 위와 같이 실행(스크린샷 `shots/n_시나리오_번호_단계.ppm`).
  - `letter`(`--days 1`): `JOBDONE` → 편지지(`GIVE 2000`)로 밀크에게 "안녕" → 우체국에서 펠리에게 "편지를 보내고 싶어" →
    "네, 확실히 받았습니다." → "또 다른 편지도?" "이제 됐어" → 하니와에서 저장(약 1분). 이 세이브를 따로 복사해 둔다.
  - `reply`(`--days 2 --time 16:59`, `letter`의 세이브): 불러올 때 밀크의 답장이 만들어져 우체국에 맡겨지고(`mNpc_Remail`)
    17시 배달 때 집배원이 우편함(2140,1420)에 넣는다. 우편함 앞에서 북서쪽을 보고 열어 두 번째 편지("밀크님 보냄")를 읽는다.
    답장은 편지가 "좋다"·"나쁘다"로 판정될 때만 온다(한글로 몇 마디 쓴 편지는 판정이 안 되어 답장이 없다; korean_patch.md).
  - `kk`(`--days 6 --time 20`, `letter`의 세이브): 토요일 밤 역 앞 K.K.(2220,940)에게 "들려 줘!" → "있어 있어!" → 편집기에
    "K.K.보사노바" → "오케이! 그럼 들어 주세요. K.K.보사노바" → 연주(첫 연주는 스태프롤, 약 2분) → 답례 음반(약 4분).
    선택지는 질문 쪽에서 A를 한 번 더 눌러야 나오고, 그 뒤에 스틱을 움직인다.
  - 증축과 섬(`letter`의 세이브에서 하루씩, 각 1–2분; 하루가 끝날 때마다 세이브를 복사해 다음 날에 쓴다):
    `house`(`--days 3`) 우체국에서 대출 갚기(`BELLS 99999` → "입금하고 싶어" → 금액 입력에서 위로 한 번 = 갚을 수 있는 만큼 →
    START) → 너굴의 HRA 이야기("나한테 맡겨!") → 다시 들어가 증축 제안(지붕 색 첫 번째) → 저장.
    `house-next 갚는횟수 선택들`(`--days 4, 5, …`): 너굴이 증축을 알리고 새 대출 → 그만큼 갚기 → 다음 제안. 이 세이브로는
    `house-next 2`(4일, 대출 125000 → 큰 집 제안), `house-next 3 9`(5일, 298000 → 마지막 선택지로 지하실 주문),
    `house-next 3`(6일, 249000 → 2층 제안), `island-buy 6`(7일, 598000 → 섬 "선리섬" 구입, 대출 998000).
    `rooms`(8일): 집 1층(장면 40)에서 앞쪽 왼편 계단(100,370에서 남쪽)으로 2층(41), 오른편(300,370)으로 지하(45).
    `island 0`(8일): 선착장 북쪽 블록으로 `WARP` 뒤 걸어 들어가야 배와 갑돌이가 놓인다(선착장 블록에 바로 `WARP`하면 둘 다
    없다; `ac_birth_control`이 E-5/F-4 블록에 들어올 때 배를 놓음). 갑돌이(3438,4230) 옆(3470,4240)에서 "섬에 갈래" → 노래를
    부르며 섬으로 → 섬 산책 → 섬 선착장(3660,5262에서 남쪽)에서 갑돌이에게 → 마을로 돌아옴(약 5분).
  - `week`(`--days 1`, `--save` 세이브): 하루마다 새로 실행하지 않고 한 번에 일주일. 입력 스크립트의 `CLOCK h[:m[:s]]`가
    시계를 그 시각까지 앞으로 돌리고(오늘 그 시각이 지났으면 내일), 게임은 매 프레임 시계를 읽으므로 자정을 스스로 넘긴다
    (날짜·요일, 무 시세, 하루 갱신 플래그). 월요일 편지 → `CLOCK 23:59:40` 뒤 40초 → 화요일 0시에 하니와에서 저장하고
    끝냄 → 밀크의 질문에 A로 이어하기(답장은 마을을 불러올 때 만들어진다) → `CLOCK 16:59` → 우편함의 밀크 답장 →
    자정 네 번 → 토요일 `CLOCK 20:00` → K.K. 신청곡 → 저장. 약 41000프레임(`--no-framelimit`로 몇 분, 보통 속도로 약 12분).
    `POS`가 게임의 날짜·시각(`time 2026-11-03 16:59:00 weekday 2`)을 찍는다. 실시간으로 하루를 보내려면 518만 프레임
    (보통 속도로 24시간)이라 `CLOCK`으로 건너뛴다.
  - 대화가 프레임 수로 맞지 않는 곳은 입력 스크립트의 `TALK`(선택지가 나올 때까지 A), `CHOOSE n`(n번째 선택지, 9는 마지막),
    `WAITMENU`(메뉴가 열릴 때까지 A), `TALKEND [n [조용]]`(대화창이 닫혀 조용한 프레임만큼 열리지 않을 때까지 A)로 기다린다.
    기다리는 동안 스크립트 시간이 멈추고 뒤 줄은 그만큼 늦게 일어난다. `POS`는 집 크기·다음 크기·대출·소지금도 찍는다.
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

## 8. 게임큐브 세이브(GCI) 가져오기·내보내기

게임큐브 메모리 카드의 세이브(Dolphin 등에서 꺼낸 `.gci`)를 PC 포트의 `save/card_a/`로 가져오고, 반대로 내보낸다.
대상은 마을(`DobutsunomoriE_MURA`, 데이터 0x72000바이트)과 패미컴 공통 세이브(`DobutsunomoriE_F_SAVE` →
PC의 `DobutsunomoriP_F_SAVE.sav`). 도구 `afe_gci`는 게임과 함께 `bin/`에 빌드된다(`PC_TESTS_ONLY` 빌드에도 있음).
`bin/`에서 실행하면 기본 폴더가 게임과 같은 `save/card_a`다.

```sh
cd pc/build32/bin
./afe_gci info  ~/01-GAEJ-DobutsunomoriE_MURA.gci        # 어떤 파일인지, 체크섬이 맞는지
./afe_gci import ~/01-GAEJ-DobutsunomoriE_MURA.gci       # → save/card_a/DobutsunomoriE_MURA
./afe_gci import ~/F_SAVE.gci --force                     # 있던 파일은 .bak으로 남기고 바꿈
./afe_gci export land -o ~/mura.gci                       # save/card_a의 마을 → GCI
./afe_gci export famicom -o ~/fsave.gci [--template 원래의_F_SAVE.gci]
```

- `--card DIR`로 다른 폴더(예: `save/card_b`)를 쓴다. 게임을 끈 상태에서 바꾼다.
- 게임 코드 GAEJ01(일본판 e+)의 파일만 받는다. 마을 세이브는 본·백업 두 벌 중 하나라도 체크섬이 맞아야 가져온다;
  체크섬이 틀린 쪽은 틀린 채로 옮긴다(게임이 원래처럼 다른 벌을 쓰거나 고장 난 마을로 본다).
- 변환: 필드 표 `pc/src/pc_save_swap_table.inc`는 `pc/tools/gen_save_swap.py`가 e+ 헤더를 32·64비트로
  컴파일한 DWARF에서 만든다(`gcc-multilib`와 `pip install pyelftools` 필요; CI는 `--check`로 헤더와 맞는지
  본다). 헤더가 바뀌면 다시 실행한다. 어느 멤버를 쓰는지 정할 수 없는 공용체가 나오면 멈추므로, 게임 코드에서
  쓰임을 확인해 스크립트의 `UNION_POLICY`(또는 태그로 고르는 경우 `pc_save_swap.c`의 처리기)에 적는다.
- 체크섬: 마을 데이터(0x2E000)와 일기(0xD1C0)는 u16 단어의 합이 0이 되게 한다(`m_flashrom.c`). 게임큐브는
  빅엔디언 단어로 더하므로 변환한 뒤 다시 계산한다. 패미컴 세이브는 바이트 합이라 순서와 무관하다.
- 확인하지 못한 것: 가져온 마을을 PC 게임에서 불러오는 것, 내보낸 GCI를 게임큐브·Dolphin에서 불러오는 것,
  실제 패미컴 세이브 GCI. 확인할 때는 가져온 마을로 `make_opening_script.py 5200`처럼 이어하기를 해 본다.
