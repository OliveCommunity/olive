uniform sampler2D ove_maintex;

uniform vec2 viewport;
uniform vec3 luma_coeffs;

uniform float waveform_scale;
uniform float parade_mode;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void) {
    float waveform_height = ceil(waveform_scale * viewport.y);
    float quantisation = 1.0 / (waveform_height - 1.0);
    float intensity = 0.10;
    vec4 col = vec4(0.0);
    vec4 cur_col = vec4(0.0);
    float ratio = 0.0;

    if (parade_mode > 0.5) {
        // RGB parade: three zones side by side, one channel per zone
        float zone = floor(ove_texcoord.x * 3.0);
        float zone_x = fract(ove_texcoord.x * 3.0);
        vec3 zone_color = zone < 0.5 ? vec3(1.0, 0.0, 0.0) :
                          (zone < 1.5 ? vec3(0.0, 1.0, 0.0) :
                                        vec3(0.0, 0.0, 1.0));

        for (int i = 0; float(i) < waveform_height; i++) {
            ratio = float(i) / float(waveform_height - 1.0);
            vec3 sample_rgb = texture(
                ove_maintex,
                vec2(zone_x, ratio)
            ).rgb;

            float channel = zone < 0.5 ? sample_rgb.r :
                            (zone < 1.5 ? sample_rgb.g : sample_rgb.b);

            float hit = step(ove_texcoord.y - quantisation, channel) *
                        step(channel, ove_texcoord.y + quantisation) * intensity;
            hit += step(1.0 - quantisation, ove_texcoord.y) *
                   step(1.0 - quantisation, channel) * intensity;

            col.rgb += hit * zone_color;
        }
    } else {
        for (int i = 0; float(i) < waveform_height; i++) {
            ratio = float(i) / float(waveform_height - 1.0);
            cur_col.rgb = texture(
                ove_maintex,
                vec2(ove_texcoord.x, ratio)
            ).rgb;

            cur_col.w = dot(cur_col.rgb, luma_coeffs);

            col += (
                step(vec4(ove_texcoord.y - quantisation), cur_col) *
                step(cur_col, vec4(ove_texcoord.y + quantisation)) *
                intensity) +
                (step(1.0 - quantisation, ove_texcoord.y) *
                step(vec4(1.0 - quantisation), cur_col) * intensity);
        }

        col.rgb += vec3(col.w);
    }

    frag_color = vec4(col.rgb, 1.0);
}
