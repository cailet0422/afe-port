/* gx.vert - GX vertex transform and texture coordinate generation.
 *
 * The backend puts a prelude in front (pc_gx_shader.c, pc/tools/gen_shaders.py):
 * the #version, the VS_IN/VARYING_OUT/UBLOCK macros, and C_<name> for each
 * configuration value of pc/include/pc_gx_config.h (constants in a specialized
 * program, gx_cfg[] in the uber one). */
VS_IN(0) vec3 a_position;
VS_IN(1) vec3 a_normal;
VS_IN(2) vec4 a_color0;
VS_IN(3) vec2 a_texcoord0;

/* PCGXVertBlock: the matrices' columns, then the texture matrices' rows */
DATA_BLOCK(0, GXVert, vec4, gx_vert, 15);
#define u_projection mat4(gx_vert[0], gx_vert[1], gx_vert[2], gx_vert[3])
#define u_modelview  mat4(gx_vert[4], gx_vert[5], gx_vert[6], gx_vert[7])
#define u_normal_mtx mat3(gx_vert[8].xyz, gx_vert[9].xyz, gx_vert[10].xyz)
#define u_texmtx(i)  gx_vert[11 + (i)] /* texgen 0's two rows, then texgen 1's */

/* the configuration, for the uber program (pc_gx_config.h) */
DATA_BLOCK(2, GXCfg, ivec4, gx_cfg, GX_CFG_VEC4S);

VARYING_OUT(0) vec4 v_color;
VARYING_OUT(1) vec2 v_texcoord0;
VARYING_OUT(2) vec2 v_texcoord1;
VARYING_OUT(3) vec3 v_normal;
VARYING_OUT(4) float v_fog_z;

void main() {
    vec4 eyePos = u_modelview * vec4(a_position, 1.0);
    gl_Position = u_projection * eyePos;
    v_fog_z = -eyePos.z;
    v_color = a_color0;
    v_normal = normalize(u_normal_mtx * a_normal);
    vec4 tc0 = (C_TEXGEN0 == 1) ? vec4(v_normal, 1.0) : vec4(a_texcoord0, 0.0, 1.0); /* 1 = GX_TG_NRM */
    if (C_TEXMTX0 != 0) {
        v_texcoord0 = vec2(dot(u_texmtx(0), tc0), dot(u_texmtx(1), tc0));
    } else {
        v_texcoord0 = tc0.xy;
    }
    vec4 tc1 = (C_TEXGEN1 == 1) ? vec4(v_normal, 1.0) : vec4(a_texcoord0, 0.0, 1.0);
    if (C_TEXMTX1 != 0) {
        v_texcoord1 = vec2(dot(u_texmtx(2), tc1), dot(u_texmtx(3), tc1));
    } else {
        v_texcoord1 = tc1.xy;
    }
#ifdef CLIP_DEPTH_ZERO_ONE
    /* Vulkan and Direct3D 12 clip depth to [0, w]: the depth OpenGL's [-w, w] gives */
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
#endif
#ifdef CLIP_FLIP_Y
    gl_Position.y = -gl_Position.y; /* Vulkan's y points down */
#endif
}
