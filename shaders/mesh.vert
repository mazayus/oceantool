#version 330 core

uniform mat4 u_WorldToClipMatrix;
uniform mat4 u_ObjectToWorldMatrix;

uniform sampler2D u_HeightMap;
uniform sampler2D u_NormalMap;

uniform vec2 u_GridSize;
uniform vec2 u_OceanSize;

out vec3 WorldPosition;
out vec3 WorldNormal;

void main()
{
    int QuadIndex = gl_VertexID / 6;
    int VertexIndex = gl_VertexID % 6;

    int NumQuadsX = int(u_GridSize.x - 1);
    int NumQuadsY = int(u_GridSize.y - 1);

    int BaseX = QuadIndex % NumQuadsX;
    int BaseY = QuadIndex / NumQuadsX;

    const vec2 Offsets[6] = vec2[6](vec2(0, 0), vec2(1, 0), vec2(0, 1),
                                    vec2(0, 1), vec2(1, 0), vec2(1, 1));

    vec2 Offset = Offsets[VertexIndex];
    vec2 QuadPosition = vec2(BaseX, BaseY);
    vec2 NumQuads = vec2(NumQuadsX, NumQuadsY);
    vec2 GridPosition = (QuadPosition - NumQuads / 2 + Offset) * u_OceanSize / NumQuads;
    float X = GridPosition.x;
    float Y = GridPosition.y;

    float Height = texelFetch(u_HeightMap, ivec2(QuadPosition + Offset), 0).r;
    vec3 LocalNormal = texelFetch(u_NormalMap, ivec2(QuadPosition + Offset), 0).rgb * 2 - 1;

    WorldPosition = (u_ObjectToWorldMatrix * vec4(X, Y, Height, 1)).xyz;
    WorldNormal = (u_ObjectToWorldMatrix * vec4(LocalNormal, 0)).xyz;

    gl_Position = u_WorldToClipMatrix * vec4(WorldPosition, 1);
}
