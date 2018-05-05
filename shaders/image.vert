#version 330 core

out vec2 TexCoord;

void main()
{
    vec2 Positions[4] = vec2[4](
        vec2(-1, -1),
        vec2( 1, -1),
        vec2(-1,  1),
        vec2( 1,  1)
    );
    vec2 TexCoords[4] = vec2[4](
        vec2(0, 0),
        vec2(1, 0),
        vec2(0, 1),
        vec2(1, 1)
    );

    gl_Position = vec4(Positions[gl_VertexID], 0, 1);
    TexCoord = TexCoords[gl_VertexID];
}
