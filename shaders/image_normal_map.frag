#version 330 core

uniform sampler2D u_NormalMap;

in vec2 TexCoord;

out vec4 out_Color;

void main()
{
    out_Color = texture(u_NormalMap, TexCoord);
}
