/* pc_gx_config.h - the values that set up the GX shader for a draw
 *
 * The GX shader (pc/shaders/gx.vert, gx.frag) reads its configuration (TEV
 * stages, color channels, alpha compare, fog, texgen...) through C_<name>
 * macros. Every backend compiles it two ways:
 *
 * - "uber": the values come from the GXCfg uniform block (gx_cfg[], in this
 *   order), so one program draws everything; it loops over the stages
 *   (gx_cfg[PC_GX_CONFIG_STAGE_VEC4 + stage * 8 + k]) as 1.4.x's shader did;
 * - specialized: the values are constants (OpenGL: #defines; Vulkan:
 *   specialization constants, constant_id = index + 1; Direct3D 12:
 *   SPIRV_CROSS_CONSTANT_ID_<index + 1> defines), so the compiler drops what
 *   a draw does not use. Much less work per pixel.
 *
 * The list is laid out in groups of four (one ivec4 each): the swap tables
 * and each stage's eight ivec4s start on an ivec4. pc_gx_shader_config()
 * fills the values for the current GX state, with the ones the shader does
 * not read in that configuration zeroed, so equal configurations share a
 * program. pc/tools/gen_shaders.py reads this list. */
#ifndef PC_GX_CONFIG_H
#define PC_GX_CONFIG_H

#define PC_GX_CONFIG_STAGE(X, s) \
    X(S##s##_CA) X(S##s##_CB) X(S##s##_CC) X(S##s##_CD) \
    X(S##s##_AA) X(S##s##_AB) X(S##s##_AC) X(S##s##_AD) \
    X(S##s##_COP) X(S##s##_AOP) X(S##s##_CBIAS) X(S##s##_CSCALE) \
    X(S##s##_ABIAS) X(S##s##_ASCALE) X(S##s##_CCLAMP) X(S##s##_ACLAMP) \
    X(S##s##_COUT) X(S##s##_AOUT) X(S##s##_RSWAP) X(S##s##_TSWAP) \
    X(S##s##_KCSEL) X(S##s##_KASEL) X(S##s##_TC) X(S##s##_TEX) \
    X(S##s##_IND) X(S##s##_IMTX) X(S##s##_IBIAS) X(S##s##_IWRAPS) \
    X(S##s##_IWRAPT) X(S##s##_IADD) X(S##s##_PAD0) X(S##s##_PAD1)

#define PC_GX_CONFIG_FIELDS(X) \
    X(NUM_STAGES) X(NUM_IND) X(NUM_CHANS) X(LIGHTING) \
    X(MAT_SRC) X(AMB_SRC) X(ALPHA_LIGHTING) X(ALPHA_MAT_SRC) \
    X(LIGHT_MASK) X(FOG) X(ACMP0) X(ACMP1) \
    X(AOP) X(TEXGEN0) X(TEXGEN1) X(TEXMTX0) \
    X(TEXMTX1) X(PAD0) X(PAD1) X(PAD2) \
    X(SWAP0_R) X(SWAP0_G) X(SWAP0_B) X(SWAP0_A) X(SWAP1_R) X(SWAP1_G) X(SWAP1_B) X(SWAP1_A) \
    X(SWAP2_R) X(SWAP2_G) X(SWAP2_B) X(SWAP2_A) X(SWAP3_R) X(SWAP3_G) X(SWAP3_B) X(SWAP3_A) \
    PC_GX_CONFIG_STAGE(X, 0) PC_GX_CONFIG_STAGE(X, 1) PC_GX_CONFIG_STAGE(X, 2)

enum {
#define PC_GX_CONFIG_ENUM(name) PC_GXC_##name,
    PC_GX_CONFIG_FIELDS(PC_GX_CONFIG_ENUM)
#undef PC_GX_CONFIG_ENUM
    PC_GX_CONFIG_COUNT
};

/* the uniform block holds them as ivec4s */
#define PC_GX_CONFIG_VEC4S (PC_GX_CONFIG_COUNT / 4)
#define PC_GX_CONFIG_SWAP_VEC4  (PC_GXC_SWAP0_R / 4)
#define PC_GX_CONFIG_STAGE_VEC4 (PC_GXC_S0_CA / 4)

/* fields of stage s: PC_GXC_S0_<name> + s * PC_GX_CONFIG_STAGE_SIZE */
#define PC_GX_CONFIG_STAGE_SIZE (PC_GXC_S1_CA - PC_GXC_S0_CA)

typedef struct {
    int v[PC_GX_CONFIG_COUNT];
} PCGXShaderConfig;

/* the layout the shader's uber loop depends on */
typedef char pc_gx_config_layout_check[(PC_GX_CONFIG_COUNT % 4 == 0 && PC_GXC_SWAP0_R % 4 == 0 &&
                                        PC_GXC_S0_CA % 4 == 0 && PC_GX_CONFIG_STAGE_SIZE == 32) ? 1 : -1];

#endif
