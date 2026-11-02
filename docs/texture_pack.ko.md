[English](texture_pack.md) · **한국어**

# HD 텍스처팩

이 포트는 Dolphin의 "사용자 지정 텍스처 불러오기"용으로 만든 HD 텍스처팩을 읽습니다. 파일 이름은
`tex1_{가로}x{세로}[_m]_{해시}[_{팔레트 해시}|_$]_{형식}.dds`(또는 `.png`)이고, 게임의 텍스처를 그 데이터의 해시로
찾아 바꿉니다. 코드는 `pc/src/pc_texture_pack.c`, 설정 화면은 `pc/src/gui/pc_gui_textures.cpp`입니다.

## 기준으로 삼은 텍스처팩

**Animal Crossing HD Texture Pack, 버전 25 (2026년 10월 11일)** — TechieAndroid와 Dolphin 포럼 프로젝트 기여자들이
2016년부터 (업스케일이 아니라) 손으로 다시 그린 텍스처팩입니다. 이 저장소에는 들어 있지 않고 포트도 함께 배포하지
않습니다. 사용자가 직접 받습니다.

- 원본 출처: <https://forums.dolphin-emu.org/Thread-animal-crossing-hd-texture-pack-version-25-october-11th-2026>
- 다운로드: <https://drive.google.com/file/d/1Z8-pMx20xO8q3caiqy4NOrSNVT1veDSk/view> (`ACHD V25.zip`, 160MB)

게임의 설정 > **HD 텍스처** 화면에 두 링크와 함께, 팩의 `Readme.txt`에서 읽은 제목과 버전이 나옵니다. 이 팩은 북미판
동물의 숲(`GAF`)용이며 e+(`GAE`)도 염두에 두고 만들어졌습니다. 두 판에 똑같이 있는 텍스처는 바뀌고, 북미판의 영어
글자가 들어간 텍스처는 e+와 애초에 맞지 않습니다.

## 설치

설정 > **HD 텍스처** > **텍스처팩 설치 (zip 선택)...**을 누르면 시스템 파일 선택 창이 열립니다(Windows, Android.
Linux는 zenity나 kdialog가 있으면). 고른 zip은 스레드에서 게임 옆 `texture_pack/`(Android는 앱 폴더)로 복사되고,
Dolphin 텍스처 이름이 들어 있는 zip인지 확인한 뒤 바로 켜집니다. 다시 시작할 필요 없이 게임이 그리는 대로 바뀝니다.
선택 창이 없으면 받은 zip을 그대로 `texture_pack/`에 넣고 **다시 읽기**를 누릅니다.

- zip은 **풀지 않고 그대로** 읽습니다. zip의 목록(중앙 디렉터리)을 한 번 읽어 두고, 각 텍스처는 게임이 처음 그릴 때
  해당 부분만 읽어 압축을 풉니다. 160MB zip은 디스크에서 그대로 160MB입니다(풀면 1.27GB).
- `texture_pack/` 아래에 푼 폴더(Dolphin 방식)도 됩니다. 같은 텍스처가 폴더와 zip에 다 있으면 폴더 쪽을 씁니다.
- 화면에서 zip마다 지울 수 있습니다. 화면의 스위치(`settings.ini`의 `hd_textures = 0`)로 바로 끌 수 있습니다.

## 불러오기

- **플레이하면서** (기본, `preload_textures = 0`): 교체할 텍스처가 있으면 스레드가 읽고 풀고 디코딩합니다. 준비될
  때까지는 원래 텍스처가 그려지고, 준비되면 게임의 텍스처 캐시에서 원본을 지워(`pc_gx_texture_hd_ready`) 다음
  그릴 때 HD로 바뀝니다. 디스크를 기다리느라 멈추는 프레임이 없습니다.
- **시작할 때 전부** (`preload_textures = 1`): 타이틀 전에 팩 전체(텍스처 19,061개, BC7 기준 GPU 메모리 약 1.3GB)를
  올립니다.
- `--deterministic`이나 입력 스크립트로 실행하면 같은 화면이 나오도록 게임 스레드에서 바로 읽습니다.
- BC7(버전 25는 전부), BC1, BC3 DDS는 GPU가 `GL_ARB/EXT_texture_compression_bptc` / `_s3tc`를 지원하면 그대로
  올립니다. 아니면(대부분의 휴대폰) 스레드에서 bcdec(MIT/Unlicense, `pc/lib/bcdec`)로 RGBA로 풀고 밉맵을 만듭니다.
  PNG는 stb_image(`pc/lib/stb`). `hd_quality = 1`(크기: 절반, Android 기본값)이면 절반 크기로 씁니다.
- 메모리 한도: GPU에 BC7로 1.5GB, 풀어서 쓰면 1GB, Android 320MB를 넘으면 가장 오래 안 그린 텍스처부터 내리고, 다시
  그릴 때 zip에서 읽어 옵니다(`pc_texture_pack_touch`, `pc_gx_texture_forget`).
- 해시: 게임큐브에 있는 그대로의 텍스처(빅엔디언 타일, 형식별 0단계 크기)의 XXH64(시드 0). C4/C8/C14X2 텍스처의
  팔레트 해시는 쓰는 항목(최소~최대 인덱스)만, 빅엔디언으로. 팔레트 해시가 `$`이면 어떤 팔레트와도 맞습니다.
  Dolphin 형식이 아닌 이름(`..._8(1).dds`, `-export`)은 쓰지 않습니다(버전 25에 17개).

## 한글패치와 함께

텍스처는 정확한 데이터의 해시로만 맞추므로, 한글패치가 바꾼 텍스처(로고 "안녕동물의숲 e+", 이름표, 한글 글꼴, 그림 속
글자)는 원본과 해시가 달라 원본용 텍스처팩과 절대 맞지 않습니다. 패치가 만든 그대로 그려집니다. 한국어로 실행 중에는
팩의 `Localisation/` 폴더(독일어·스페인어 글자, 버전 25에 203개)는 아예 읽지 않습니다.

한글패치 디스크에서 `--deterministic`으로 같은 스크립트를 팩 없이/팩과 함께 돌려 비교했습니다: 타이틀(한글 로고
그대로, 저작권 문구는 HD), K.K., 이름 입력(한글 입력 화면과 자판 그대로), 기차 안 낯선고양이 대화(한글 대사와
이름표 그대로, 고양이와 의자·벽은 HD).
