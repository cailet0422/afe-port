# PC 포트 개발 환경 (Linux 컨테이너)

새 컨테이너에서 이어서 작업할 때의 순서. 게임 데이터는 저장소에 없으므로 매번 준비한다.

## 1. 패키지

```sh
dpkg --add-architecture i386 && apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y gcc-multilib g++-multilib libsdl2-dev:i386 \
    libgl-dev:i386 libgl1-mesa-dri:i386 xvfb mesa-utils x11-apps imagemagick xdotool gdb unifdef \
    binutils-powerpc-linux-gnu ninja-build cmake
```

## 2. 디스크 이미지

사용자가 제공한 CISO(구글 드라이브, 네트워크 허용 필요: `drive.google.com`, `drive.usercontent.google.com`):

```sh
mkdir -p /tmp/disc && curl -L -o /tmp/disc/game.ciso \
  "https://drive.usercontent.google.com/download?id=17KXSC8sp9Gl8rNrm5se79nWTlaW2ZDZI&export=download&confirm=t"
python3 pc/tools/gcdisc.py extract-orig /tmp/disc/game.ciso orig/GAEJ01_01
```

디스크 이미지·추출 파일은 커밋하지 않는다(`orig/*/*`, `pc/build*/`는 무시됨).

## 3. 매칭 빌드 (에셋 추출 겸 검증)

```sh
python3 configure.py --version GAEJ01_01 && ninja
sha1sum build/GAEJ01_01/static.dol build/GAEJ01_01/*/*.rel
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
  (`montage`로 묶어 보면 편하다). 약 66fps(llvmpipe)로 돈다.
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
