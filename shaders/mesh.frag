#version 330 core

in vec3 WorldPosition;
in vec3 WorldNormal;

out vec4 out_Color;

void main()
{
    vec3 SunDirection = normalize(vec3(-1, 0, 1));
    float LightIntensity = max(dot(normalize(WorldNormal), SunDirection), 0);
    out_Color = vec4(vec3(LightIntensity), 1);
}
