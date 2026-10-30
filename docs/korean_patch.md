# 한글패치(역전맞고, v0.9.2) 호환 메모

PC 포트는 자체 번역을 만드는 대신, 사용자가 직접 적용한 한글패치 디스크 이미지를 읽어 한국어로
동작하는 것을 목표로 한다. 패치의 데이터·번역문·코드는 저장소에 넣지 않는다. 이 문서는 PC 포트가
그 데이터를 해석하는 데 필요한 형식만 정리한다.

## 패치가 바꾸는 것

- 디스크: `main.dol`은 그대로. `foresta/forestd/foresti/foresto.rel`, `forest_msg.arc`(msg.bin),
  `forest_1st_script.arc`(편지·선택지·문자열), `forest_2nd.arc`(주민 이름), GBA 연동 아카이브, `boot.bin`.
- `forestd/foresti/foresto.rel`: 크기와 배치는 그대로이고 데이터만 제자리에서 바뀐다
  (`FONT_tex_font1`, 아이템·가구 이름표, 타이틀 로고, 각종 UI 글자 텍스처 등).
- `foresta.rel`: 기존 섹션은 크기가 같고, 실행 섹션 2개(7번 약 72KB, 8번 약 7KB)가 추가된다.
  기존 함수 88개의 첫 명령 등을 새 코드로의 분기로 바꾸고, 문자열 표 포인터 일부를 새 섹션의
  한글 문자열로 돌린다. `.data`의 문자열·카탈로그 정렬표·폭 표 등도 제자리에서 바뀐다.

## 문자 인코딩

- 1바이트 문자는 원래 문자표 그대로. 줄바꿈 `0xCD`, 제어 코드 `0x80 len ...`도 그대로.
- 한글 한 글자는 2바이트:
  - 선두 바이트: `0x00–0x1F`, `0x86–0x8F`, `0x91–0x96` (48개, 순서대로 색인 0–47)
  - 둘째 바이트: `0xCE–0xFF` (50개)
  - 글리프 번호 `g = 선두색인 × 50 + (둘째 − 0xCE)`, 유효 범위 0–2389
- `g < 2350`이면 KS X 1001 완성형 한글 2,350자를 유니코드 순서로 늘어놓은 것의 `g`번째 글자.
  (KS X 1001의 2,350자는 유니코드 순서와 같은 순서, EUC-KR `0xB0A1–0xC8FE`.) 2350–2368은 초성
  19자(ㄱ ㄲ ㄴ ㄷ ㄸ ㄹ ㅁ ㅂ ㅃ ㅅ ㅆ ㅇ ㅈ ㅉ ㅊ ㅋ ㅌ ㅍ ㅎ), 2369–2389는 중성 21자(ㅏ … ㅣ).
- 1바이트 문자: 패치된 글꼴은 영문·숫자를 ASCII 위치에 둔다. 기호는 대부분 ASCII와 같지만
  `~`=0x2A, `+`=0x3D, `\`=0x60, `;`=0x7B, `*`=0x7C, `=`=0x7D, `/`=0x7E(`ko_charset_ascii`).
  한국어 모드에서 영문은 고정폭(12)이다.
- `0x96, 0xF6 이상, xx`의 3바이트는 사전 코드다. 번호 `e = b1×255 + b2 − (b2≥0x80 ? 1 : 0) − 0xF6×255`
  (0–1194), 섹션 7의 `0xD340 + 16 + 2e`에 있는 u16 오프셋 `o`에 대해 항목은 `0xD340 + o`이고 길이
  바이트가 `+1010`, 내용이 `+1011`부터다. 주로 필드(10바이트)에 들어가지 않는 긴 아이템 이름이다
  (예: 0번 "오렌지편지지").
- 1바이트 `0x97–0xC6` 일부는 자주 쓰는 한글 음절(예: `ac c0 a8` = "마을로"). 섹션 7 `0xA0DE`의 u16 표가
  각 1바이트 문자의 음절 번호+1을 준다(받침 판정용).

## 글리프

- `msg.bin`(forest_msg.arc) 안, 메시지 리소스 시작에서 `0x332780` 바이트 뒤부터 글리프 2,390개가
  192바이트씩 이어진다. 게임 코드에서는 `Msg_rom_start + 0x332780 + g × 192`.
- 한 글리프는 2bpp, 8×8 타일 12개(가로 3 × 세로 4) = 24×32픽셀. 한 바이트에 픽셀 4개
  (상위 비트부터), 값 0–3을 I4 강도 0, 5, 10, 15로 펼친다.
- 원래 게임의 글꼴 은행 텍스처는 384×512 I4(24×32 칸이 16×16개)이며, 패치는 쓰지 않는 두 번째
  은행(`FONT_tex_font2`)을 255칸 캐시로 써서 한글을 그 칸에 올린 뒤 은행 2의 글자로 그린다.

## 글자 폭

- 원래 e+ 글꼴은 고정폭(`mFont_Get_FontOffset`의 표가 전부 0). 패치는 이 표에 공백·쉼표·마침표
  `6`, `0xCE–0xFF` `12`를 넣고, `cut` 여부와 관계없이 이 표를 적용한다.

## 패치가 손대는 기능(코드)

- 글자 크기·폭·그리기: `mFont_CodeSize_get`, `mFontW_CodeSize_get`, `mFont_GetStringWidth(2F)`,
  `mFont_SetLineStrings_AndSpace_new`, `mFontChar_gppDraw`, `mFontCharW_gppDraw`, `mFont_GetCodeWidth(F)B`
- 메시지 조립 (PC 구현: `pc_ko_expand`, `pc_ko_fix_josa`)
  - `mMsg_Set_free_str`/`mMsg_Set_item_str`/`mHandbill_Set_free_str`: 사전 코드를 최대 66바이트로 펼친 뒤
    원래 함수로 넘긴다. 마지막 코드 뒤가 공백뿐이면 거기서 끝낸다.
  - `mMsg_CopyString`: 복사한 문자열의 마지막 소리로 받침을 정하고(한글 음절, 1바이트 한글, 숫자는
    영·일·이…·십·백·천·만, 영문 등은 모름 → 그대로), 바로 뒤(태그 건너뜀)의 조사 은/는·을/를·이/가·
    과/와·아/야를 섹션 7 `0xA0CA` 표의 형태로 바꾼다. 조사 뒤에 한글 음절이 이어지면 낱말로 보고 두며,
    아/야는 받침이 있을 때 뒤에 쉼표(또는 0xC7)가 있어야 바꾼다.
  - 이름 색, 말소리(`mMsg_sound_CodeVoice`), 선택지 커서: 미대응
- 말버릇 (PC 구현: `pc_ko_word_ending`): `mNpc_GetWordEnding(_forTag)`의 결과를 16바이트로 풀어 준다.
  `0xFE` + 6바이트면 12비트 값 4개(2400 미만 = 글리프, 2400–2655 = 1바이트 문자 `v−2400`), 사전 코드면
  사전 항목, 아니면 그대로. `mMsg_CopyTail(Truth)`의 길이도 4→16. 이 7바이트는 `Animal_c`의 `catchphrase`(4바이트,
  0x595)와 그 뒤 여백 3바이트(0x599–0x59B)에 들어가므로 저장 파일에도 그대로 남는다.
- 줄 검사: 게시판·일기·알림판 `*_strLineCheck` — 한 칸 폭을 글자별로(한글 둘째 바이트 0, 공백·쉼표·
  마침표 6, 줄바꿈 0, 그 밖 12) 센다. PC 구현 `pc_ko_line_char_width`.
- 입력기: `mED_*`(이름·편지 입력), `mLE_*`
  - 편집 버퍼에서 한글은 2칸(u8 버퍼면 2바이트, u16 버퍼면 바이트 하나씩 2칸). 패치의
    `mED_output_code`는 정적 변수의 플래그가 서 있으면 선두·둘째 바이트를 한 번에 넣고 커서를 2 옮긴다.
  - `mED_get_col_line_width`/`mED_set_idxcol_inLineWidth`/`mED_get_single_line_width`는 글자 단위로
    세고, 폭은 공백·쉼표·마침표 6, 그 밖 12(선두 바이트 판정은 섹션 7의 256바이트 표).
  - `mED_set_dl`은 섹션 8의 코드로 바뀌어 자체 입력 UI를 그린다(원래의 `lat_sousa_*` 텍스처를 재사용하므로
    원래 코드로 그리면 깨진다).
  - 편지 맺음말(`mBD_set_writing_footer`)은 훅이 없어 칸당 12픽셀로 오른쪽 정렬하는데 커서(`mBD_set_cursol`)는
    글자 폭으로 자리를 잡으므로, 한글이 있으면 둘이 어긋난다. PC는 한국어 모드에서 글자 폭으로 정렬한다.
  - 이름 입력 편집기(`m_ledit_ovl.c`, PC 구현: `pc_ko_ledit_buf`/`pc_ko_ledit_match`/`ko_charset_pack_ending`,
    afe-decomp의 `TARGET_PC`): 말버릇(EPHRASE)·도루묵씨에게 하는 대답(RESET)·K.K. 신청곡(REQUEST)은 섹션 7
    `0xBD68`의 새 버퍼(+0 8바이트, +0x18 18바이트, +0x30 18바이트)에 입력한다. 길이는 `.data`의 `mLE_win_data`
    (항목당 52바이트, `edit_max`는 +0x20)에서 4→8, 10→18, 10→18로 바뀌었다(PC는 디스크의 값을 읽는다).
    - `mLE_ledit_ovl_init+0xdc`: `mem_clear(str, max, ' ')`를 `s7+0x59c`로 — 종류(`data0`)가 2·3·4면 새 버퍼로
      바꿔서 지우고 그 버퍼를 편집기에 넘긴다.
    - `mLE_move_Wait+0xdc`(말버릇, `s7+0x1424`): `mem_copy(data2, 편집 버퍼, 4)` 대신, 8바이트의 끝 공백을 뺀
      길이가 4 이하면 4바이트 그대로 + 0 세 바이트, 아니면 앞에서부터 한글 2바이트(선두 바이트 표 `s7+0x6b7c`,
      둘째 바이트 0xCE 이상)는 글리프 번호, 그 밖의 바이트는 2400 + 코드로 12비트 값 4개(남는 값 0xFFF)를 만들어
      `0xFE` + 6바이트로 `data2`(주민의 `catchphrase`)에 쓴다.
    - `mLE_move_Wait+0x7c`(신청곡, `s7+0x12cc`): 곡 이름과의 `mem_cmp(이름, 입력, 10)` 대신, 이름(10바이트)을
      사전 코드면 펼쳐(최대 24바이트, `s7+0xee4`) 양쪽의 공백을 모두 건너뛰며 비교한다. 띄어 쓰든 안 쓰든 같다.
    - `aMJN3_check_word`(`s7+0x15ac`): 도루묵씨의 문구(`mString` 10바이트)를 같은 방법으로 대답 버퍼와 비교해
      맞으면 GOOD, 아니면 버퍼 앞 10바이트를 `actor->reply`에 옮기고 원래의 금지어 검사(`aMJN3_check_ng_word`).
    - `mED_all_space_check`는 그대로라 말버릇에서는 `data2`(압축된 값)와 입력 버퍼를 비교한다(패치와 같음).
- 고정 길이 상수와 옮긴 문자열: 원본과 패치의 `.text`를 비교하면 훅 말고 직접 바뀐 명령은 32곳이고,
  대부분 문자열 복사 길이(2→4, 5→6 등)다. 그 문자열 자체는 패치가 섹션 7에 새로 두고 주소 재배치를
  그쪽으로 돌렸다(`reldis.py retarget 패치.rel 원본.rel`로 130곳: `.text` 참조와 `.data` 표의 포인터).
  - PC 구현: `pc_ko_patched_str`(섹션 7 오프셋으로 문자열), `pc_ko_data_ptr`/`pc_ko_data_u32`(디스크의
    `.data`에 있는 포인터·길이; 포인터가 `.data`를 가리키면 디스크 사본을 준다 — 제자리에서 길어진
    문자열이 PC 객체 크기를 넘기 때문).
  - 대응함: 생일 편지·편지지 맺음말("드림"), 알림판 마을 접미사("마을", 패치는 4바이트를 쓰고 2로 세지만
    PC는 4로 센다), 낚시 기록 마을("쿠니치"), 분실물 이름, 하니와 메시지 표("それ" 접두어 없음), 지도 라벨
    (한 줄), 배치 불가 효과 문자열("위엔 못 둬!"/"이미 있어!").
  - 미대응: `mPM/mAL/mHB/mTI_*`(비밀번호·알람·시간 입력), `mNT_set_day_strings_dl`, `mTG_price_draw`,
    `aMKBC_clip_set_itemstr3`, `player_select_draw_title`, `mMsg_sound_CodeVoice`, 말소리 `voice$3952`,
    RTC 연도 상한(2032→2100), SD·필름 경고 문자열(`fl_strb_*`, `wr_*`).
- 그 밖: 화면 비율(시야·직교 행렬·카메라), RTC 연도 상한 2032→2100, 구역 경계 달리기, 부팅 주민의
  한자 메뉴 → 화면 비율 메뉴

## 배포 형식과 게임 안 적용

- 배포 zip(`Doubutsu-no-Mori-e-plus-Korean-v0.9.2/`): `*.xdelta`(약 8MB), `manifest.json`, 적용 도구
  (`vcdiff.py`·`apply_gc_xdelta.py`·브라우저용 `패치적용.html`), 설명 문서. 패치 도구는 MIT.
- `.xdelta`는 xdelta3가 만든 VCDIFF(RFC 3284): 헤더 `D6 C3 C4 00`, 보조 압축·사용자 코드 표 없음,
  창(window) 58개, 창마다 xdelta3 확장인 Adler-32(창 표시 `0x04`)가 있다. 원본 창은 4MB 이하이고 원본 위치는
  디스크 전체(1,459,978,240바이트) 범위, 결과는 36,995,072바이트의 잘린 ISO.
- `manifest.json`: `patch_sha256`, `target_bytes`, `target_sha256`, `target_filename`, `source_*`(원본
  CRC-32/MD5/SHA-1/SHA-256, 원본은 일본판 Rev 1 GAEJ01).
- PC 포트의 설치기(`pc/src/pc_ko_patch.c`, 순수 코드는 `pc/src/ko/ko_patch.c` + `pc/tests/test_ko_patch.c`):
  zip이면 안의 `.xdelta`와 `manifest.json`을 풀고(inflate 자체 구현), 설정 메뉴가 쓰는 디스크와 따로 연
  원래 이미지(CISO도 그대로)에서 창 단위로 원본을 읽어 `korean/<target_filename>.part`에 쓰며 SHA-256을
  계산한다. 크기·해시가 manifest와 맞으면 이름을 바꾸고 `settings.ini`의 `korean = 1`, `ko_image`를 쓴다.
  디스크 헤더가 `GAEJ01`·버전 1이 아니면 시작하지 않고, 창의 Adler-32가 다르면(다른 판·이미 패치된 이미지)
  멈춘다. 리눅스에서 v0.9.2 적용에 0.2초 남짓.
- 다음 실행 때 `pc_disc_init`이 `korean = 1`이면 `ko_image`를 먼저 연다(없거나 디스크가 아니면 원래 이미지).

## PC 포트 구현 계획

1. 패치 디스크 감지 → 한국어 모드.
2. 글자 크기·그리기·폭: 위 형식대로 C로 구현(`TARGET_PC`), 글리프는 디스크의 msg.bin에서 읽는다.
3. REL 데이터 덮어쓰기: 디스크의 REL에서 데이터 객체를 읽어 PC 빌드의 같은 객체에 덮어쓴다
   (텍스처·이름표·문자열·폭 표 등). 같은 장치로 런타임 에셋 로더(M6)도 해결한다.
4. 메시지 조립·말버릇·줄 검사·고정 길이 상수를 한국어 모드에 맞게 조정.
5. 입력기: 물리 키보드 QWERTY 두벌식 + 한/영 전환(`pc/src/ko/ko_ime.c`)을 패치 인코딩으로 출력 —
   구현됨(README의 "편집기 연결"). 게임패드·터치용 화면 키보드도 구현됨(`pc_ko_editor.c`, `ko_osk.c`).
   이름·마을 이름·편지(머리말·본문·맺음말)·말버릇·도루묵씨 대답·K.K. 신청곡을 확인했다
   (`make_opening_script.py --korean`, `make_ko_edit_script.py`). 남은 것: 편지 보내기와 주민 답장, 게시판·일기
   화면, 실제 대화에서 말버릇을 바꿔 달라는 요청(무작위)과 아르바이트를 마친 뒤의 K.K. 공연은 디버그 명령으로만
   시험했다.

## REL 데이터 오버레이 범위

`gen_rel_overlay.py`는 요소 형식이 단순한(바이트·u16·u32·Vtx) 객체만 덮어쓴다. 함수 안의 static
(`country_str$457` 같은 것, PC 빌드에서는 `country_str.1`)도 포함한다 — 마을 이름 입력의 접미사
`마을로`가 여기서 온다. 앞부분이 글자인 구조체는 그 부분만 덮는다(`STRUCT_PREFIXES`: 태그 메뉴 문구
`mTG_tag_word_c`의 8바이트). 그 밖의 포인터 표·구조체(`type:None`, `type:Gfx` 등)는 빠진다.
한 소스에 같은 이름의 함수 안 static이 여러 형으로 있으면(`m_police_box.c`의 `int category_table[]`와
`mActor_name_t category_table[6]`) 객체 크기에 맞는 선언의 형을 쓰고, 그래도 모르면 덮어쓰지 않는다
(빌드에 들어간 원래 값을 쓴다). 예전에는 마지막 선언의 형을 써서 이 int 표가 u16 단위로 뒤집혔고,
날이 바뀌면 분실물 추가(`mPB_force_set_keep_item`)에서 크래시가 났다.
