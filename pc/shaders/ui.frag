/* ui.frag - the texture times the color; kind 1 cuts an ellipse out of it, 2
 * a ring (inner radius v_shape.y), with edges a pixel wide */
VARYING_IN(0) vec2 v_uv;
VARYING_IN(1) vec2 v_local;
VARYING_IN(2) vec4 v_color;
VARYING_IN(3) vec2 v_shape;

TEXTURE(0) u_tex;

FRAG_OUT vec4 frag_color;

void main() {
    vec4 c = texture(u_tex, v_uv) * v_color;
    float a = 1.0;
    if (v_shape.x > 0.5) {
        float d = length(v_local);
        float aa = max(fwidth(d), 0.001);
        a = 1.0 - smoothstep(1.0 - aa, 1.0, d);
        if (v_shape.x > 1.5) {
            a *= smoothstep(v_shape.y - aa, v_shape.y, d);
        }
    }
    frag_color = vec4(c.rgb, c.a * a);
}
