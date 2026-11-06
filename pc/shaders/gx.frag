/* gx.frag - the GameCube's TEV: up to 3 stages, indirect texturing, color
 * channels with lighting, alpha compare and fog.
 *
 * The backend puts a prelude in front (see gx.vert). Every C_<name> is a
 * configuration value (pc/include/pc_gx_config.h): in a specialized program
 * they are constants, and the compiler leaves out the stages, branches and
 * switch cases a draw does not use. */
VARYING_IN(0) vec4 v_color;
VARYING_IN(1) vec2 v_texcoord0;
VARYING_IN(2) vec2 v_texcoord1;
VARYING_IN(3) vec3 v_normal;
VARYING_IN(4) float v_fog_z;

/* PCGXFragBlock */
DATA_BLOCK(1, GXFrag, vec4, gx_frag, 35);
#define u_tev_regs(i)    gx_frag[(i)] /* PREV, REG0, REG1, REG2 */
#define u_kcolor(i)      gx_frag[4 + (i)]
#define u_mat_color      gx_frag[8]
#define u_amb_color      gx_frag[9]
#define u_light_pos(i)   gx_frag[10 + (i)]
#define u_light_color(i) gx_frag[18 + (i)]
#define u_fog_color      gx_frag[26]
#define u_fog_range      gx_frag[27] /* x = start, y = end */
#define u_ind_mtx_r0(i)  gx_frag[28 + (i)]
#define u_ind_mtx_r1(i)  gx_frag[31 + (i)]
#define u_alpha_ref      gx_frag[34] /* x = ref0, y = ref1 */

/* the configuration, for the uber program (pc_gx_config.h) */
DATA_BLOCK(2, GXCfg, ivec4, gx_cfg, GX_CFG_VEC4S);

/* uber: ivec4 k of stage s's configuration */
#define STAGE_CFG(s, k) gx_cfg[GX_CFG_STAGE_VEC4 + (s) * 8 + (k)]

/* each stage samples its own texture; indirect stages 0 and 1 theirs */
TEXTURE(0) u_texture0;
TEXTURE(1) u_texture1;
TEXTURE(2) u_texture2;
TEXTURE(3) u_ind_tex0;
TEXTURE(4) u_ind_tex1;

FRAG_OUT vec4 fragColor;

/* GXTevKColorSel: 0-7 = constant fractions 8/8..1/8, 8-11 = reserved (zero),
   12-15 = K0..K3 RGB, 16+ = K0..K3 single channel (R,G,B,A) */
vec3 getKonstC(int sel) {
    if (sel <= 7)  return vec3(float(8 - sel) / 8.0);
    if (sel <= 11) return vec3(0.0);
    if (sel <= 15) return u_kcolor(sel - 12).rgb;
    int ki = (sel - 16) & 3;
    int ch = (sel - 16) >> 2;
    switch (ch) {
        case 0:  return vec3(u_kcolor(ki).r);
        case 1:  return vec3(u_kcolor(ki).g);
        case 2:  return vec3(u_kcolor(ki).b);
        default: return vec3(u_kcolor(ki).a);
    }
}

float getKonstA(int sel) {
    if (sel <= 7)  return float(8 - sel) / 8.0;
    if (sel <= 15) return 0.0;
    int ki = (sel - 16) & 3;
    int ch = (sel - 16) >> 2;
    switch (ch) {
        case 0:  return u_kcolor(ki).r;
        case 1:  return u_kcolor(ki).g;
        case 2:  return u_kcolor(ki).b;
        default: return u_kcolor(ki).a;
    }
}

/* GXTevColorArg enum (0-15) */
vec3 getTevC(int id, vec4 prev, vec4 tex, vec4 ras,
             vec4 r0, vec4 r1, vec4 r2, vec3 konst) {
    switch (id) {
        case 0:  return prev.rgb;      /* CPREV */
        case 1:  return vec3(prev.a);  /* APREV */
        case 2:  return r0.rgb;        /* C0 */
        case 3:  return vec3(r0.a);    /* A0 */
        case 4:  return r1.rgb;        /* C1 */
        case 5:  return vec3(r1.a);    /* A1 */
        case 6:  return r2.rgb;        /* C2 */
        case 7:  return vec3(r2.a);    /* A2 */
        case 8:  return tex.rgb;       /* TEXC */
        case 9:  return vec3(tex.a);   /* TEXA */
        case 10: return ras.rgb;       /* RASC */
        case 11: return vec3(ras.a);   /* RASA */
        case 12: return vec3(1.0);     /* ONE */
        case 13: return vec3(0.5);     /* HALF */
        case 14: return konst;         /* KONST */
        default: return vec3(0.0);     /* ZERO */
    }
}

/* GXTevAlphaArg enum (0-7) */
float getTevA(int id, float prev, float tex, float ras,
              float r0, float r1, float r2, float konst) {
    switch (id) {
        case 0:  return prev;   /* APREV */
        case 1:  return r0;     /* A0 */
        case 2:  return r1;     /* A1 */
        case 3:  return r2;     /* A2 */
        case 4:  return tex;    /* TEXA */
        case 5:  return ras;    /* RASA */
        case 6:  return konst;  /* KONST */
        default: return 0.0;    /* ZERO */
    }
}

/* swap table t (constants in a specialized program) */
ivec4 swapTable(int t) {
    switch (t) {
        case 0:  return ivec4(C_SWAP0_R, C_SWAP0_G, C_SWAP0_B, C_SWAP0_A);
        case 1:  return ivec4(C_SWAP1_R, C_SWAP1_G, C_SWAP1_B, C_SWAP1_A);
        case 2:  return ivec4(C_SWAP2_R, C_SWAP2_G, C_SWAP2_B, C_SWAP2_A);
        default: return ivec4(C_SWAP3_R, C_SWAP3_G, C_SWAP3_B, C_SWAP3_A);
    }
}

vec4 applySwap(vec4 v, ivec4 sw) {
    return vec4(v[sw.x], v[sw.y], v[sw.z], v[sw.w]);
}

/* TEV formula: d OP ((1-c)*a + c*b) */
vec4 tevStage(ivec4 cin, int cop, ivec4 ain, int aop,
              vec4 prev, vec4 tex, vec4 ras,
              vec4 r0, vec4 r1, vec4 r2,
              vec3 konstC, float konstA) {
    vec3 ca = getTevC(cin.x, prev, tex, ras, r0, r1, r2, konstC);
    vec3 cb = getTevC(cin.y, prev, tex, ras, r0, r1, r2, konstC);
    vec3 cc = getTevC(cin.z, prev, tex, ras, r0, r1, r2, konstC);
    vec3 cd = getTevC(cin.w, prev, tex, ras, r0, r1, r2, konstC);
    vec3 blend = mix(ca, cb, cc);
    vec3 cResult = (cop == 1) ? (cd - blend) : (cd + blend);

    float aa = getTevA(ain.x, prev.a, tex.a, ras.a, r0.a, r1.a, r2.a, konstA);
    float ab = getTevA(ain.y, prev.a, tex.a, ras.a, r0.a, r1.a, r2.a, konstA);
    float ac = getTevA(ain.z, prev.a, tex.a, ras.a, r0.a, r1.a, r2.a, konstA);
    float ad = getTevA(ain.w, prev.a, tex.a, ras.a, r0.a, r1.a, r2.a, konstA);
    float aBlend = mix(aa, ab, ac);
    float aResult = (aop == 1) ? (ad - aBlend) : (ad + aBlend);

    return vec4(cResult, aResult);
}

/* bias: 0=none, 1=+0.5, 2=-0.5; scale: 0=x1, 1=x2, 2=x4, 3=x0.5 */
vec4 applyBSC(vec4 v, ivec4 bsc) {
    switch (bsc.x) {
        case 1: v.rgb += 0.5; break;
        case 2: v.rgb -= 0.5; break;
    }
    switch (bsc.y) {
        case 1: v.rgb *= 2.0; break;
        case 2: v.rgb *= 4.0; break;
        case 3: v.rgb *= 0.5; break;
    }
    switch (bsc.z) {
        case 1: v.a += 0.5; break;
        case 2: v.a -= 0.5; break;
    }
    switch (bsc.w) {
        case 1: v.a *= 2.0; break;
        case 2: v.a *= 4.0; break;
        case 3: v.a *= 0.5; break;
    }
    return v;
}

/* out_cfg.zw selects target register: 0=PREV, 1=REG0, 2=REG1, 3=REG2 */
void writeToReg(vec4 val, ivec4 out_cfg,
                inout vec4 prev, inout vec4 r0, inout vec4 r1, inout vec4 r2) {
    vec3 rgb = (out_cfg.x != 0) ? clamp(val.rgb, 0.0, 1.0) : val.rgb;
    float a  = (out_cfg.y != 0) ? clamp(val.a,   0.0, 1.0) : val.a;
    switch (out_cfg.z) {
        case 0:  prev.rgb = rgb; break;
        case 1:  r0.rgb   = rgb; break;
        case 2:  r1.rgb   = rgb; break;
        default: r2.rgb   = rgb; break;
    }
    switch (out_cfg.w) {
        case 0:  prev.a = a; break;
        case 1:  r0.a   = a; break;
        case 2:  r1.a   = a; break;
        default: r2.a   = a; break;
    }
}

vec3 sampleIndTex(int ind_stage, vec2 tc0, vec2 tc1) {
    vec2 tc = (ind_stage == 0) ? tc0 : tc1;
    vec4 s = (ind_stage == 0) ? texture(u_ind_tex0, tc) : texture(u_ind_tex1, tc);
    return s.rgb;
}

/* ind_cfg: x = indirect stage, y = matrix, z = bias; ind_wrap: x = wrap s, y = wrap t, z = add previous */
vec2 applyIndirect(ivec4 ind_cfg, ivec3 ind_wrap, vec2 coord,
                   vec2 prev_offset, vec2 tc0, vec2 tc1) {
    int ind_mtx_id = ind_cfg.y;
    if (ind_mtx_id == 0) return coord;  /* GX_ITM_OFF */

    vec3 indSample = sampleIndTex(ind_cfg.x, tc0, tc1);

    /* Bias: shift [0,1] to [-0.5,0.5] per axis (GX_ITB bitmask: S=1, T=2, U=4) */
    int bias = ind_cfg.z;
    vec3 biased = indSample;
    if ((bias & 1) != 0) biased.x -= 0.5;
    if ((bias & 2) != 0) biased.y -= 0.5;
    if ((bias & 4) != 0) biased.z -= 0.5;

    /* GX_ITM 1-3=full 2D, 5-7=S-only, 9-11=T-only */
    vec2 offset = vec2(0.0);
    int idx;
    if (ind_mtx_id >= 1 && ind_mtx_id <= 3) {
        idx = ind_mtx_id - 1;
        offset.x = dot(u_ind_mtx_r0(idx).xyz, biased);
        offset.y = dot(u_ind_mtx_r1(idx).xyz, biased);
    } else if (ind_mtx_id >= 5 && ind_mtx_id <= 7) {
        idx = ind_mtx_id - 5;
        offset.x = dot(u_ind_mtx_r0(idx).xyz, biased);
        offset.y = 0.0;
    } else if (ind_mtx_id >= 9 && ind_mtx_id <= 11) {
        idx = ind_mtx_id - 9;
        offset.x = 0.0;
        offset.y = dot(u_ind_mtx_r1(idx).xyz, biased);
    }

    if (ind_wrap.z != 0) offset += prev_offset;

    /* Wrap base texcoord: 0=off, 1-5=256..16 texels, 6=zero.
       Uses floor-based wrap instead of mod() to handle negative inputs. */
    vec2 wrappedCoord = coord;
    if (ind_wrap.x == 6) wrappedCoord.x = 0.0;
    else if (ind_wrap.x >= 1 && ind_wrap.x <= 5) {
        float wrapSize = float(1 << (9 - ind_wrap.x));  /* 256,128,64,32,16 */
        float w = coord.x * 256.0;
        wrappedCoord.x = (w - wrapSize * floor(w / wrapSize)) / 256.0;
    }
    if (ind_wrap.y == 6) wrappedCoord.y = 0.0;
    else if (ind_wrap.y >= 1 && ind_wrap.y <= 5) {
        float wrapSize = float(1 << (9 - ind_wrap.y));
        float w = coord.y * 256.0;
        wrappedCoord.y = (w - wrapSize * floor(w / wrapSize)) / 256.0;
    }

    return wrappedCoord + offset;
}

/* EQUAL/NEQUAL use epsilon for float precision after interpolation */
bool alphaTest(int comp, float val, float ref) {
    const float EPS = 0.5 / 255.0;
    switch (comp) {
        case 0:  return false;                  /* NEVER */
        case 1:  return val < ref;              /* LESS */
        case 2:  return abs(val - ref) < EPS;   /* EQUAL */
        case 3:  return val <= ref;             /* LEQUAL */
        case 4:  return val > ref;              /* GREATER */
        case 5:  return abs(val - ref) >= EPS;  /* NEQUAL */
        case 6:  return val >= ref;             /* GEQUAL */
        default: return true;                   /* ALWAYS */
    }
}

/* a stage's texture coordinates (texcoord 1 or 0), moved by its indirect stage */
vec2 stageCoord(int tc, int ind, int imtx, int ibias, int iwraps, int iwrapt, int iadd,
                inout vec2 ind_prev_offset) {
    vec2 stc = (tc == 1) ? v_texcoord1 : v_texcoord0;
    if (C_NUM_IND > 0 && imtx != 0) {
        vec2 base = stc;
        stc = applyIndirect(ivec4(ind, imtx, ibias, 0), ivec3(iwraps, iwrapt, iadd),
                            stc, ind_prev_offset, v_texcoord0, v_texcoord1);
        ind_prev_offset = stc - base;
    }
    return stc;
}

/* one TEV stage: tswap and rswap are the swap tables' channel orders */
void stageRun(vec4 texColor, vec4 rasColor, ivec4 tswap, ivec4 rswap,
              ivec4 cin, int cop, ivec4 ain, int aop, int kcsel, int kasel, ivec4 bsc, ivec4 out_cfg,
              inout vec4 prev, inout vec4 r0, inout vec4 r1, inout vec4 r2) {
    vec4 sTex = applySwap(texColor, tswap);
    vec4 sRas = applySwap(rasColor, rswap);
    vec4 result = tevStage(cin, cop, ain, aop, prev, sTex, sRas, r0, r1, r2, getKonstC(kcsel), getKonstA(kasel));
    result = applyBSC(result, bsc);
    writeToReg(result, out_cfg, prev, r0, r1, r2);
}

void main() {
    /* each stage's texture coordinates, in stage order: an indirect stage may
       add the previous one's offset */
    vec2 stc0 = vec2(0.0), stc1 = vec2(0.0), stc2 = vec2(0.0);
    vec2 ind_prev_offset = vec2(0.0);
    if (GX_UBER != 0) {
        /* as 1.4.x's shader: a loop over the stages there are */
        for (int s = 0; s < C_NUM_STAGES; s++) {
            ivec4 k5 = STAGE_CFG(s, 5); /* KCSEL KASEL TC TEX */
            ivec4 k6 = STAGE_CFG(s, 6); /* IND IMTX IBIAS IWRAPS */
            ivec4 k7 = STAGE_CFG(s, 7); /* IWRAPT IADD */
            vec2 c = stageCoord(k5.z, k6.x, k6.y, k6.z, k6.w, k7.x, k7.y, ind_prev_offset);
            if (s == 0) stc0 = c;
            else if (s == 1) stc1 = c;
            else stc2 = c;
        }
    } else {
    if (C_NUM_STAGES > 0) {
        stc0 = stageCoord(C_S0_TC, C_S0_IND, C_S0_IMTX, C_S0_IBIAS, C_S0_IWRAPS, C_S0_IWRAPT, C_S0_IADD,
                          ind_prev_offset);
    }
    if (C_NUM_STAGES > 1) {
        stc1 = stageCoord(C_S1_TC, C_S1_IND, C_S1_IMTX, C_S1_IBIAS, C_S1_IWRAPS, C_S1_IWRAPT, C_S1_IADD,
                          ind_prev_offset);
    }
    if (C_NUM_STAGES > 2) {
        stc2 = stageCoord(C_S2_TC, C_S2_IND, C_S2_IMTX, C_S2_IBIAS, C_S2_IWRAPS, C_S2_IWRAPT, C_S2_IADD,
                          ind_prev_offset);
    }
    }

    vec4 texColor0 = (C_S0_TEX != 0) ? texture(u_texture0, stc0) : vec4(1.0);
    vec4 texColor1 = (C_S1_TEX != 0) ? texture(u_texture1, stc1) : vec4(1.0);
    vec4 texColor2 = (C_S2_TEX != 0) ? texture(u_texture2, stc2) : vec4(1.0);

    /* Rasterized color: GX lighting model */
    vec4 rasColor;
    if (C_NUM_CHANS == 0) {
        rasColor = vec4(1.0);
    } else {
        vec3 matC = (C_MAT_SRC != 0) ? v_color.rgb : u_mat_color.rgb;
        vec3 ambC = (C_AMB_SRC != 0) ? v_color.rgb : u_amb_color.rgb;
        if (C_LIGHTING != 0) {
            vec3 lightAccum = ambC;
            for (int i = 0; i < 8; i++) {
                if ((C_LIGHT_MASK & (1 << i)) != 0) {
                    vec3 L = normalize(u_light_pos(i).xyz);
                    float diff = clamp(dot(v_normal, L), 0.0, 1.0);
                    lightAccum += diff * u_light_color(i).rgb;
                }
            }
            rasColor.rgb = matC * clamp(lightAccum, 0.0, 1.0);
        } else {
            rasColor.rgb = matC;
        }
        float matA = (C_ALPHA_MAT_SRC != 0) ? v_color.a : u_mat_color.a;
        rasColor.a = (C_ALPHA_LIGHTING != 0) ? matA * u_amb_color.a : matA;
    }

    vec4 prev = u_tev_regs(0);
    vec4 r0 = u_tev_regs(1);
    vec4 r1 = u_tev_regs(2);
    vec4 r2 = u_tev_regs(3);

    if (GX_UBER != 0) {
        for (int s = 0; s < C_NUM_STAGES; s++) {
            ivec4 k2 = STAGE_CFG(s, 2); /* COP AOP CBIAS CSCALE */
            ivec4 k3 = STAGE_CFG(s, 3); /* ABIAS ASCALE CCLAMP ACLAMP */
            ivec4 k4 = STAGE_CFG(s, 4); /* COUT AOUT RSWAP TSWAP */
            ivec4 k5 = STAGE_CFG(s, 5); /* KCSEL KASEL TC TEX */
            vec4 tex = (s == 0) ? texColor0 : (s == 1) ? texColor1 : texColor2;
            stageRun(tex, rasColor, gx_cfg[GX_CFG_SWAP_VEC4 + k4.w], gx_cfg[GX_CFG_SWAP_VEC4 + k4.z],
                     STAGE_CFG(s, 0), k2.x, STAGE_CFG(s, 1), k2.y, k5.x, k5.y,
                     ivec4(k2.z, k2.w, k3.x, k3.y), ivec4(k3.z, k3.w, k4.x, k4.y), prev, r0, r1, r2);
        }
    } else {
    if (C_NUM_STAGES > 0) {
        stageRun(texColor0, rasColor, swapTable(C_S0_TSWAP), swapTable(C_S0_RSWAP),
                 ivec4(C_S0_CA, C_S0_CB, C_S0_CC, C_S0_CD), C_S0_COP,
                 ivec4(C_S0_AA, C_S0_AB, C_S0_AC, C_S0_AD), C_S0_AOP,
                 C_S0_KCSEL, C_S0_KASEL,
                 ivec4(C_S0_CBIAS, C_S0_CSCALE, C_S0_ABIAS, C_S0_ASCALE),
                 ivec4(C_S0_CCLAMP, C_S0_ACLAMP, C_S0_COUT, C_S0_AOUT), prev, r0, r1, r2);
    }
    if (C_NUM_STAGES > 1) {
        stageRun(texColor1, rasColor, swapTable(C_S1_TSWAP), swapTable(C_S1_RSWAP),
                 ivec4(C_S1_CA, C_S1_CB, C_S1_CC, C_S1_CD), C_S1_COP,
                 ivec4(C_S1_AA, C_S1_AB, C_S1_AC, C_S1_AD), C_S1_AOP,
                 C_S1_KCSEL, C_S1_KASEL,
                 ivec4(C_S1_CBIAS, C_S1_CSCALE, C_S1_ABIAS, C_S1_ASCALE),
                 ivec4(C_S1_CCLAMP, C_S1_ACLAMP, C_S1_COUT, C_S1_AOUT), prev, r0, r1, r2);
    }
    if (C_NUM_STAGES > 2) {
        stageRun(texColor2, rasColor, swapTable(C_S2_TSWAP), swapTable(C_S2_RSWAP),
                 ivec4(C_S2_CA, C_S2_CB, C_S2_CC, C_S2_CD), C_S2_COP,
                 ivec4(C_S2_AA, C_S2_AB, C_S2_AC, C_S2_AD), C_S2_AOP,
                 C_S2_KCSEL, C_S2_KASEL,
                 ivec4(C_S2_CBIAS, C_S2_CSCALE, C_S2_ABIAS, C_S2_ASCALE),
                 ivec4(C_S2_CCLAMP, C_S2_ACLAMP, C_S2_COUT, C_S2_AOUT), prev, r0, r1, r2);
    }
    }

    /* Alpha compare */
    if (C_ACMP0 != 7 || C_ACMP1 != 7) {
        float ref0 = u_alpha_ref.x / 255.0;
        float ref1 = u_alpha_ref.y / 255.0;
        bool pass0 = alphaTest(C_ACMP0, prev.a, ref0);
        bool pass1 = alphaTest(C_ACMP1, prev.a, ref1);
        bool pass;
        switch (C_AOP) {
            case 0:  pass = pass0 && pass1; break;
            case 1:  pass = pass0 || pass1; break;
            case 2:  pass = pass0 != pass1; break;
            default: pass = pass0 == pass1; break;
        }
        if (!pass) discard;
    }

    fragColor = prev;

    /* Fog */
    if (C_FOG != 0) {
        float fog_denom = max(u_fog_range.y - u_fog_range.x, 1e-6);
        float fog_factor = clamp((v_fog_z - u_fog_range.x) / fog_denom, 0.0, 1.0);
        fragColor.rgb = mix(fragColor.rgb, u_fog_color.rgb, fog_factor);
    }
}
