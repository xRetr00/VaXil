#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float audioLevel;
    float speaking;
    vec2 resolution;
} ubuf;

/*
    "Ionize" by @XorDev

    https://x.com/XorDev/status/1921224922166104360
*/
void mainImage(out vec4 O, in vec2 I, float time, vec3 resolution)
{
    //Time for waves and coloring
    float t = time,
    //Raymarch iterator
    i = 0.0,
    //Raymarch depth
    z = 0.0,
    //Raymarch step distance
    d = 0.0,
    //Signed distance for coloring
    s = 0.0;

    //Clear fragcolor and raymarch loop 100 times
    O = vec4(0.0);
    for (; i++ < 1e2; )
    {
        //Raymarch sample point
        vec3 p = z * normalize(vec3(I + I, 0.0) - resolution.xyy),
        //Vector for undistorted coordinates
        v;
        //Shift camera back 9 units
        p.z += 9.0;
        //Save coordinates
        v = p;
        //Apply turbulence waves
        //https://mini.gmshaders.com/p/turbulence
        for (d = 1.0; d < 9.0; d += d)
            p += 0.5 * sin(p.yzx * d + t) / d;
        //Distance to gyroid
        z += d = 0.2 * (0.01 + abs(s = dot(cos(p), sin(p / 0.7).yzx))
        //Spherical boundary
        - min(d = 6.0 - length(v), -d * 0.1));
        //Coloring and glow attenuation
        O += (cos(s / 0.1 + z + t + vec4(2.0, 4.0, 5.0, 0.0)) + 1.2) / d / z;
    }
    //Tanh tonemapping
    //https://www.shadertoy.com/view/ms3BD7
    O = tanh(O / 2e3);
}

void main()
{
    vec2 safeResolution = max(ubuf.resolution, vec2(1.0));
    vec2 uv = qt_TexCoord0;
    vec2 centeredUv = uv * 2.0 - 1.0;
    centeredUv.x *= safeResolution.x / safeResolution.y;

    float reactive = clamp(ubuf.audioLevel * 1.15 + ubuf.speaking * 0.95, 0.0, 1.0);
    float pulse = 1.0 + 0.035 * sin(ubuf.time * (1.1 + ubuf.speaking * 1.8)) + reactive * 0.06;
    float distortionStrength = 0.012 + reactive * 0.055;

    vec2 warpedUv = centeredUv / pulse;
    warpedUv += vec2(
        sin(centeredUv.y * 8.0 + ubuf.time * 1.7),
        cos(centeredUv.x * 7.0 - ubuf.time * 1.45)
    ) * distortionStrength;

    vec2 I = (warpedUv * 0.5 + 0.5) * safeResolution;
    vec4 O = vec4(0.0);
    mainImage(O, I, ubuf.time, vec3(safeResolution, 1.0));

    vec3 color = max(O.rgb, vec3(0.0));
    color *= 0.92 + reactive * 0.42;

    float radius = length(centeredUv);
    float orbRadius = 0.74 + reactive * 0.03;
    float mask = 1.0 - smoothstep(orbRadius - 0.09, orbRadius + 0.012, radius);
    float glow = smoothstep(orbRadius + 0.22, orbRadius - 0.03, radius) * (0.18 + reactive * 0.22);

    color += glow * vec3(0.26, 0.55, 0.95);
    color *= mask;

    float intensity = max(max(color.r, color.g), color.b);
    float alpha = clamp(mask * (0.62 + intensity * 0.9) + glow * 0.9, 0.0, 1.0) * ubuf.qt_Opacity;

    fragColor = vec4(color, alpha);
}
