uniform sampler2D tex_in;

uniform vec3 wb_gain_in;

in vec2 ove_texcoord;
out vec4 frag_color;

void main(void)
{
    vec4 source = texture(tex_in, ove_texcoord);

    // Deliberately not clamped: white balance must also work on HDR/linear
    // footage with values above 1.0
    frag_color = vec4(source.rgb * wb_gain_in, source.a);
}
