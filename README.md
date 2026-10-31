# afe-port — 동물의 숲 e+ PC/Android 포트

[afe-decomp](https://github.com/cailet0422/afe-decomp)(どうぶつの森 e+ 디컴파일)를 Windows·Linux·Android에서
돌리는 포트와 한글패치 지원.

| 경로 | 내용 |
|---|---|
| `pc/` | 플랫폼 계층(SDL2, OpenGL/GLES, 오디오, 디스크 읽기, 설정·일시정지 메뉴, 한글패치 설치기, 한글 입력기), Android 프로젝트(`pc/android`), 생성·시험 도구(`pc/tools`), 단위 테스트(`pc/tests`) |
| `docs/` | 빌드·실행 방법과 진행 상황([README](docs/README.md)), [Android](docs/android.md), [한글패치 형식](docs/korean_patch.md), [64비트](docs/64bit.md), [개발 환경](docs/dev_setup.md) |
| `decomp/` | afe-decomp 서브모듈. 게임 코드와 그 안의 `#ifdef TARGET_PC` 훅이 여기 있다 |

게임 데이터(디스크 이미지, 디스크에서 추출한 에셋)와 그것이 들어간 빌드 결과물(실행 파일, APK)은 저장소에
넣지 않고 배포하지 않는다. 각자 본인 디스크(일본판 Rev 1, GAEJ01)로 빌드한다.

## 빠른 시작 (Linux, 32비트)

```sh
git clone --recursive https://github.com/cailet0422/afe-port.git
cd afe-port
python3 pc/tools/gcdisc.py extract-orig <디스크.ciso> decomp/orig/GAEJ01_01
(cd decomp && python3 configure.py --version GAEJ01_01 && ninja)
cmake -S pc -B pc/build32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake
ninja -C pc/build32
```

디스크 이미지를 `pc/build32/bin/rom/`에 넣고 `pc/build32/bin/DobutsuNoMoriEPlus`를 실행한다. 필요한 패키지,
Windows(MinGW)·Android 빌드, 한글패치 적용(게임 안 설정 메뉴에서 zip을 골라 적용)은 [docs/README.md](docs/README.md).

디컴프를 다른 곳에 두었다면 cmake에 `-DDECOMP_ROOT=<afe-decomp 경로>`를 준다.

## 한국어 패치

한국어로 하려면 한글패치를 아래 원 배포처로 가는 페이지에서 받는다. 받은 zip은 게임 안 설정 메뉴에서 그대로
적용한다([docs/README.md](docs/README.md)의 "한국어(한글패치)로 실행").

- 받는 곳: [retrodb 한글패치 — 동물의 숲 e+](https://retrodb.info/krpatch/7428/%EB%8F%99%EB%AC%BC%EC%9D%98-%EC%88%B2-e)

한글패치는 이 저장소와 관계없는 별도 제작자(역전맞고)의 작업이다. 이 저장소와 그 관리자는 패치 제작에 참여하지
않았고, 패치 파일·번역문·글꼴은 저장소에 넣지 않는다. 패치 자체에 관한 문의는 패치 배포처로 한다.

## 디컴프 쪽 수정

게임 코드의 PC용 수정은 afe-decomp에 `#ifdef TARGET_PC` 안으로만 커밋한다(매칭 빌드 해시가 그대로여야 한다).
그 뒤 이 저장소에서 `decomp` 서브모듈이 그 커밋을 가리키게 올린다.

## 라이선스

`pc/`의 플랫폼 계층은 [ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port)(MIT)를 바탕으로 했다 —
[pc/LICENSE](pc/LICENSE). 디컴프(`decomp/`)는 afe-decomp의 라이선스(CC0 1.0)를 따른다.
