/* ui.vert - the menus, minimap, touch pad and FixNES (pc_gfx_draw_ui):
 * pixel positions in a u_space-sized space the viewport spans, y down. The
 * backend's prelude declares the macros (see gx.vert). */
VS_IN(0) vec2 a_pos;
VS_IN(1) vec2 a_uv;
VS_IN(2) vec2 a_local;
VS_IN(3) vec4 a_color;
VS_IN(4) vec2 a_shape;

DATA_BLOCK(0, UISpace, vec4, ui_space, 1); /* x, y: the space's size */

VARYING_OUT(0) vec2 v_uv;
VARYING_OUT(1) vec2 v_local;
VARYING_OUT(2) vec4 v_color;
VARYING_OUT(3) vec2 v_shape;

void main() {
    gl_Position = vec4(a_pos.x / ui_space[0].x * 2.0 - 1.0, 1.0 - a_pos.y / ui_space[0].y * 2.0, 0.0, 1.0);
#ifdef CLIP_FLIP_Y
    gl_Position.y = -gl_Position.y;
#endif
    v_uv = a_uv;
    v_local = a_local;
    v_color = a_color;
    v_shape = a_shape;
}
