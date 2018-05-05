#version 330 core

uniform sampler2D u_HeightMap;

uniform vec2 u_HeightRange;

in vec2 TexCoord;

out vec4 out_Color;

void main()
{
    float Height = texture(u_HeightMap, TexCoord).r;

    if (u_HeightRange[0] != u_HeightRange[1])
        Height = (Height - u_HeightRange[0]) / (u_HeightRange[1] - u_HeightRange[0]);
    else
        Height = 0.5;

    out_Color = vec4(vec3(Height), 1);
}
