/* Unit tests for the Hangul input method (pc/src/ko/ko_ime.c).
 *
 * Each case types a key string starting in Korean mode and compares the
 * resulting text (committed + composing) as UTF-8.
 *   '\b' = backspace, '\t' = Korean/English toggle
 */
#include "ko_ime.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t text[256];
    int len;
} Buffer;

static void buf_push(Buffer* b, const uint16_t* units, int n) {
    int i;

    for (i = 0; i < n; i++) {
        b->text[b->len++] = units[i];
    }
}

static void to_utf8(const uint16_t* units, int n, char* out) {
    int i;

    for (i = 0; i < n; i++) {
        uint16_t c = units[i];

        if (c < 0x80) {
            *out++ = (char)c;
        } else if (c < 0x800) {
            *out++ = (char)(0xC0 | (c >> 6));
            *out++ = (char)(0x80 | (c & 0x3F));
        } else {
            *out++ = (char)(0xE0 | (c >> 12));
            *out++ = (char)(0x80 | ((c >> 6) & 0x3F));
            *out++ = (char)(0x80 | (c & 0x3F));
        }
    }

    *out = '\0';
}

/* Types keys and returns committed text plus the composing syllable. */
static void type_keys(const char* keys, char* result, KoImeMode start_mode) {
    KoIme ime;
    Buffer b;
    uint16_t out[KO_IME_MAX_COMMIT];
    uint16_t pre;
    const char* k;

    ko_ime_init(&ime);
    b.len = 0;
    buf_push(&b, out, ko_ime_set_mode(&ime, start_mode, out));

    for (k = keys; *k != '\0'; k++) {
        if (*k == '\b') {
            if (!ko_ime_backspace(&ime) && b.len > 0) {
                b.len--;
            }
        } else if (*k == '\t') {
            buf_push(&b, out, ko_ime_toggle_mode(&ime, out));
        } else {
            buf_push(&b, out, ko_ime_key(&ime, *k, out));
        }
    }

    pre = ko_ime_preedit(&ime);
    if (pre != 0) {
        b.text[b.len++] = pre;
    }

    to_utf8(b.text, b.len, result);
}

static int s_failures = 0;
static int s_total = 0;

static void check_mode(const char* keys, const char* expected, KoImeMode mode) {
    char result[1024];

    type_keys(keys, result, mode);
    s_total++;
    if (strcmp(result, expected) != 0) {
        s_failures++;
        printf("FAIL: keys \"%s\": expected \"%s\", got \"%s\"\n", keys, expected, result);
    }
}

static void check(const char* keys, const char* expected) {
    check_mode(keys, expected, KO_IME_MODE_KO);
}

static void test_preedit_and_flush(void) {
    KoIme ime;
    uint16_t out[KO_IME_MAX_COMMIT];
    int n;

    ko_ime_init(&ime);
    s_total++;
    if (ime.mode != KO_IME_MODE_EN || ko_ime_preedit(&ime) != 0) {
        s_failures++;
        printf("FAIL: init state\n");
    }

    ko_ime_set_mode(&ime, KO_IME_MODE_KO, out);
    n = ko_ime_key(&ime, 'r', out);
    n += ko_ime_key(&ime, 'k', out);
    s_total++;
    if (n != 0 || ko_ime_preedit(&ime) != 0xAC00) { /* 가 */
        s_failures++;
        printf("FAIL: preedit after rk = %04X (committed %d)\n", ko_ime_preedit(&ime), n);
    }

    n = ko_ime_flush(&ime, out);
    s_total++;
    if (n != 1 || out[0] != 0xAC00 || ko_ime_preedit(&ime) != 0 || ko_ime_flush(&ime, out) != 0) {
        s_failures++;
        printf("FAIL: flush\n");
    }

    s_total++;
    if (ko_ime_backspace(&ime) != 0) {
        s_failures++;
        printf("FAIL: backspace on empty composition must not be consumed\n");
    }
}

static void test_key_jamo(void) {
    static const struct {
        char key;
        uint16_t jamo;
    } cases[] = {
        { 'r', 0x3131 }, { 'R', 0x3132 }, { 'k', 0x314F }, { 'K', 0x314F }, { 'O', 0x3152 },
        { 'm', 0x3161 }, { '1', 0 },      { ' ', 0 },      { 'Z', 0x314B },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        s_total++;
        if (ko_ime_key_jamo(cases[i].key) != cases[i].jamo) {
            s_failures++;
            printf("FAIL: key_jamo('%c') = %04X, expected %04X\n", cases[i].key, ko_ime_key_jamo(cases[i].key),
                   cases[i].jamo);
        }
    }
}

int main(void) {
    /* Basic syllables and words */
    check("rk", "가");
    check("dkssud", "안녕");
    check("gksrmf", "한글");
    check("dkssudgktpdy", "안녕하세요");
    check("ehdanfdmltnv", "동물의숲");
    check("ehdanfdml tnv", "동물의 숲");
    check("xkdns", "타운");

    /* Compound vowels */
    check("rhk", "과");
    check("dnjs", "원");
    check("dml", "의");
    check("ghl", "회");
    check("dho", "왜");
    check("dnp", "웨");
    check("dnl", "위");

    /* Shifted double consonants and vowels */
    check("Rk", "까");
    check("Tk", "싸");
    check("Ekf", "딸");
    check("dO", "얘");
    check("dP", "예");
    check("Qkd", "빵");
    check("Wkd", "짱");

    /* Final consonants, compound finals and moving finals */
    check("rkqt", "값");
    check("rkqtdl", "값이");
    check("rkqtl", "갑시");
    check("dlfrdj", "읽어");
    check("dlfrj", "일거");
    check("ekfr", "닭");
    check("dksg", "않");
    check("dkswdk", "앉아");
    check("rkr", "각");
    check("rkrk", "가가");
    check("ekT", "닸");
    check("ekTdj", "닸어");
    check("ekTj", "다써");
    check("ektT", "닷ㅆ");

    /* Consonants that cannot be finals start a new syllable */
    check("rkE", "가ㄸ");
    check("rkQ", "가ㅃ");
    check("rkW", "가ㅉ");
    check("rkEk", "가따");

    /* Lone jamo */
    check("r", "ㄱ");
    check("rr", "ㄱㄱ");
    check("k", "ㅏ");
    check("hk", "ㅘ");
    check("kr", "ㅏㄱ");
    check("kk", "ㅏㅏ");
    check("hkk", "ㅘㅏ");
    check("rkqtt", "값ㅅ");

    /* Vowel that does not combine ends the syllable */
    check("rkk", "가ㅏ");
    check("rhkk", "과ㅏ");
    check("rhkl", "과ㅣ");

    /* Backspace removes one jamo at a time */
    check("rhkd\b", "과");
    check("rhkd\b\b", "고");
    check("rhkd\b\b\b", "ㄱ");
    check("rhkd\b\b\b\b", "");
    check("rkqt\b", "갑");
    check("rkqtdl\b", "값ㅇ");
    check("rkqtdl\b\b", "값");
    check("rkqtdl\b\b\b", "");
    check("rk rk\b\b\b", "가");
    check("gksrmf\b\b\b", "한");
    check("gksrmf\b\b\b\b", "");
    check("\b\b", "");

    /* Non-letter keys commit and pass through */
    check("rk!", "가!");
    check("rk1rk", "가1가");
    check("rk.", "가.");
    check("dkssud, ", "안녕, ");

    /* Korean/English toggle */
    check_mode("abc", "abc", KO_IME_MODE_EN);
    check_mode("Hello", "Hello", KO_IME_MODE_EN);
    check_mode("\tgksrmf", "한글", KO_IME_MODE_EN);
    check("rk\tk", "가k");
    check("rkq\tQ", "갑Q");
    check("\tabc\trk", "abc가");
    check("gks\tA\tdud", "한A영");
    check("rk\t\b", "");

    test_preedit_and_flush();
    test_key_jamo();

    if (s_failures != 0) {
        printf("%d of %d checks failed\n", s_failures, s_total);
        return 1;
    }

    printf("All %d checks passed\n", s_total);
    return 0;
}
