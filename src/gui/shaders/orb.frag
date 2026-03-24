#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float level;
    float speaking;
    float mode;
    float distortion;
    vec2 resolution;
    vec4 colorA;
    vec4 colorB;
    vec4 colorC;
} ubuf;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 6; ++i) {
        value += amplitude * noise(p);
        p = mat2(1.7, 1.2, -1.2, 1.7) * p;
        amplitude *= 0.52;
    }
    return value;
}

vec2 domainWarp(vec2 p, float t)
{
    float x = fbm(p * 1.9 + vec2(t * 0.18, -t * 0.12));
    float y = fbm(p * 2.3 + vec2(-t * 0.16, t * 0.21));
    return p + vec2(x - 0.5, y - 0.5) * 0.42;
}

void main()
{
    vec2 uv = qt_TexCoord0 * 2.0 - 1.0;
    vec2 warpedUv = domainWarp(uv * (1.15 + ubuf.distortion * 0.7), ubuf.time);
    float radius = length(warpedUv);
    float angle = atan(warpedUv.y, warpedUv.x);

    float listening = clamp(1.0 - abs(ubuf.mode - 1.0), 0.0, 1.0);
    float processing = clamp(1.0 - abs(ubuf.mode - 2.0), 0.0, 1.0);
    float speakingMode = clamp(1.0 - abs(ubuf.mode - 3.0), 0.0, 1.0);

    float swirl = sin(angle * (4.0 + processing * 2.4) + ubuf.time * (1.1 + processing * 2.2)) * 0.055;
    float innerSwirl = cos(angle * (8.0 + processing * 3.0) - ubuf.time * (1.6 + processing * 1.8)) * 0.025;
    float listenWave = sin(angle * 7.0 - ubuf.time * 3.6) * (0.03 + listening * (0.09 + ubuf.level * 0.12));
    float speakWave = cos(angle * 5.0 + ubuf.time * 4.7) * (0.025 + speakingMode * (0.06 + ubuf.speaking * 0.09));
    float detail = (fbm(warpedUv * (3.2 + ubuf.distortion * 4.8) + vec2(ubuf.time * 0.24, -ubuf.time * 0.18)) - 0.5)
        * (0.13 + ubuf.distortion * 0.18);

    float boundary = 0.67 + swirl + innerSwirl + listenWave + speakWave + detail;
    float mask = 1.0 - smoothstep(boundary - 0.065, boundary + 0.01, radius);

    float innerGlow = 1.0 - smoothstep(0.02, 0.7, radius);
    float caustic = fbm(warpedUv * 4.9 + vec2(ubuf.time * 0.35, ubuf.time * -0.28));
    float subSurface = fbm(warpedUv * 7.0 - vec2(ubuf.time * 0.42, ubuf.time * 0.31));
    float rim = smoothstep(boundary - 0.02, boundary - 0.16, radius);
    float shell = 1.0 - smoothstep(0.48, boundary + 0.05, radius);

    vec3 normal = normalize(vec3(warpedUv * 0.9, sqrt(max(0.0, 1.0 - min(radius * radius, 1.0)))));
    vec3 lightA = normalize(vec3(-0.45, -0.65, 0.8));
    vec3 lightB = normalize(vec3(0.7, -0.2, 0.65));
    vec3 viewDir = vec3(0.0, 0.0, 1.0);

    float diffuseA = max(dot(normal, lightA), 0.0);
    float diffuseB = max(dot(normal, lightB), 0.0);
    float specularA = pow(max(dot(reflect(-lightA, normal), viewDir), 0.0), 20.0);
    float specularB = pow(max(dot(reflect(-lightB, normal), viewDir), 0.0), 36.0);
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.8);

    vec3 gradient = mix(ubuf.colorC.rgb, ubuf.colorB.rgb, clamp((warpedUv.y + 1.0) * 0.5 + caustic * 0.18, 0.0, 1.0));
    gradient = mix(gradient, ubuf.colorA.rgb, innerGlow * (0.5 + ubuf.speaking * 0.2));
    gradient += ubuf.colorA.rgb * diffuseA * 0.16;
    gradient += ubuf.colorB.rgb * diffuseB * 0.12;
    gradient += mix(ubuf.colorA.rgb, ubuf.colorB.rgb, 0.4) * (subSurface - 0.5) * 0.18;

    float holographicBand = 0.5 + 0.5 * sin((warpedUv.y - warpedUv.x) * 7.0 + ubuf.time * (1.2 + processing * 1.8));
    gradient += ubuf.colorA.rgb * holographicBand * 0.08;
    gradient += ubuf.colorB.rgb * rim * (0.18 + processing * 0.12 + speakingMode * 0.08);
    gradient += ubuf.colorA.rgb * specularA * 0.55;
    gradient += vec3(1.0, 0.88, 1.0) * specularB * 0.24;
    gradient += mix(ubuf.colorA.rgb, ubuf.colorB.rgb, 0.5) * fresnel * 0.18;
    gradient += mix(ubuf.colorB.rgb, ubuf.colorA.rgb, 0.5) * smoothstep(0.0, 0.4, innerGlow) * 0.12;
    gradient *= 0.92 + shell * 0.18;

    float halo = smoothstep(boundary + 0.18, boundary - 0.1, radius) * 0.25;
    float alpha = (mask * (0.82 + innerGlow * 0.18 + rim * 0.18 + fresnel * 0.16) + halo) * ubuf.qt_Opacity;
    fragColor = vec4(gradient, alpha);
}
