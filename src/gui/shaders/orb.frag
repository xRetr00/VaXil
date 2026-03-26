#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;
    float audioLevel;
    float speaking;
    float uiState;
    float quality;
    vec2 resolution;
    float layerRole;
    float distortion;
    float intensity;
    float pulseAmount;
    float hueShift;
} ubuf;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise21(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);

    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));

    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.55;
    float frequency = 1.0;

    for (int i = 0; i < 4; ++i) {
        value += noise21(p * frequency) * amplitude;
        frequency *= 2.02;
        amplitude *= 0.5;
    }

    return value;
}

float layeredNoise(vec3 p, float t)
{
    float field0 = fbm(p.xy * 2.4 + vec2(t * 0.22, -t * 0.16) + p.z * 0.42);
    float field1 = fbm(p.yz * 3.8 + vec2(-t * 0.34, t * 0.26) + p.x * 0.51);
    float field2 = fbm((p.xz + p.yx) * 5.6 + vec2(t * 0.61, t * 0.45));
    return field0 * 0.44 + field1 * 0.34 + field2 * 0.22;
}

float sphereIntersect(vec3 ro, vec3 rd, float radius, out float nearHit, out float farHit)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) {
        nearHit = 0.0;
        farHit = 0.0;
        return -1.0;
    }

    float rootH = sqrt(h);
    nearHit = -b - rootH;
    farHit = -b + rootH;
    return farHit - nearHit;
}

void main()
{
    vec2 safeResolution = max(ubuf.resolution, vec2(1.0));
    vec2 uv = qt_TexCoord0 * 2.0 - 1.0;
    uv.x *= safeResolution.x / safeResolution.y;

    float audioReactive = clamp(ubuf.audioLevel, 0.0, 1.0);
    float speakingReactive = clamp(ubuf.speaking, 0.0, 1.0);
    float stateReactive = clamp(ubuf.uiState, 0.0, 3.0);
    float listeningAmount = 1.0 - clamp(abs(stateReactive - 1.0), 0.0, 1.0);
    float thinkingAmount = 1.0 - clamp(abs(stateReactive - 2.0), 0.0, 1.0);
    float executingAmount = 1.0 - clamp(abs(stateReactive - 3.0), 0.0, 1.0);
    float idleAmount = 1.0 - clamp(stateReactive, 0.0, 1.0);

    float sphereRadius = 0.72;
    float shellSharpness = 1.0;
    float outerGlowWidth = 0.22;
    vec3 innerColor = vec3(0.48, 0.84, 1.0);
    vec3 outerColor = vec3(0.12, 0.28, 0.78);

    if (ubuf.layerRole > 1.5) {
        sphereRadius = 0.96;
        shellSharpness = 0.72;
        outerGlowWidth = 0.34;
        innerColor = vec3(0.30, 0.64, 1.0);
        outerColor = vec3(0.07, 0.18, 0.56);
    } else if (ubuf.layerRole > 0.5) {
        sphereRadius = 0.84;
        shellSharpness = 1.0;
        outerGlowWidth = 0.27;
        innerColor = vec3(0.34, 0.75, 1.0);
        outerColor = vec3(0.10, 0.25, 0.74);
    }

    float motionSpeed = 0.62 + thinkingAmount * 0.75 + executingAmount * 0.95 + speakingReactive * 0.35;
    float pulse = sin(ubuf.time * motionSpeed + audioReactive * 3.0 + ubuf.layerRole * 1.7);
    float pulseWave = 0.5 + 0.5 * pulse;
    float breathing = 0.5 + 0.5 * sin(ubuf.time * (0.62 + idleAmount * 0.18));

    vec2 distortedUv = uv;
    distortedUv += sin(distortedUv.yx * 4.0 + ubuf.time * (1.0 + listeningAmount * 0.8))
        * (0.012 + ubuf.distortion * 0.022)
        * (0.28 + audioReactive * 0.55 + listeningAmount * 0.35);
    distortedUv += vec2(
        sin(ubuf.time * 44.0 + uv.y * 14.0),
        cos(ubuf.time * 38.0 + uv.x * 12.0))
        * 0.006 * listeningAmount * (0.4 + audioReactive * 0.6);
    distortedUv *= 1.0 + (pulse * 0.035 + speakingReactive * 0.018) * ubuf.pulseAmount;

    float radius = length(distortedUv);
    float radialDensity = clamp(1.0 - radius / max(sphereRadius + outerGlowWidth, 0.001), 0.0, 1.0);
    float depthFalloff = radialDensity * radialDensity;

    int baseSteps = 40;
    if (ubuf.quality < 0.5) {
        baseSteps = 20;
    } else if (ubuf.quality > 1.5) {
        baseSteps = 60;
    }
    int steps = min(60, baseSteps + int((1.0 - smoothstep(0.45, 1.12, radius)) * 8.0));

    vec3 rayOrigin = vec3(0.0, 0.0, 2.2);
    vec3 rayDir = normalize(vec3(distortedUv * 1.04, -1.8));
    float tNear = 0.0;
    float tFar = 0.0;
    float hitDistance = sphereIntersect(rayOrigin, rayDir, sphereRadius, tNear, tFar);

    vec3 marchColor = vec3(0.0);
    float marchAlpha = 0.0;

    if (hitDistance > 0.0) {
        float startT = max(tNear, 0.0);
        float span = max(tFar - startT, 0.001);
        float baseStepSize = span / float(max(steps, 1));
        float marchT = startT;

        for (int i = 0; i < 60; ++i) {
            if (i >= steps || marchAlpha > 0.985) {
                break;
            }

            vec3 pos = rayOrigin + rayDir * marchT;
            float normalizedRadius = clamp(length(pos) / sphereRadius, 0.0, 1.0);
            float centerFalloff = pow(1.0 - normalizedRadius, 1.45);
            float shell = smoothstep(1.04, 0.26, normalizedRadius);

            float flow = layeredNoise(pos * (1.4 + ubuf.layerRole * 0.16), ubuf.time * (0.7 + motionSpeed));
            float swirl = layeredNoise(
                vec3(pos.y, pos.z, pos.x) * (2.1 + ubuf.layerRole * 0.22),
                ubuf.time * (1.2 + speakingReactive * 0.5));
            float detail = mix(flow, swirl, 0.42);

            float density = centerFalloff * shell * shellSharpness;
            density *= (0.38 + detail * 1.08);
            density *= (0.82 + pulseWave * 0.28 + breathing * 0.16);
            density *= ubuf.intensity;

            vec3 sampleColor = mix(outerColor, innerColor, clamp(centerFalloff * 0.85 + detail * 0.35, 0.0, 1.0));
            sampleColor += vec3(0.10, 0.18, 0.28) * thinkingAmount;
            sampleColor += vec3(0.12, 0.20, 0.32) * speakingReactive;

            float alphaStep = density * baseStepSize * 0.32;
            marchColor += sampleColor * alphaStep * (1.0 - marchAlpha);
            marchAlpha += alphaStep * (1.0 - marchAlpha);

            marchT += baseStepSize * mix(0.82, 1.22, normalizedRadius);
            if (marchT > tFar) {
                break;
            }
        }
    }

    vec2 normalUv = distortedUv / max(sphereRadius, 0.001);
    float z = sqrt(max(0.0, 1.0 - dot(normalUv, normalUv)));
    vec3 normal = normalize(vec3(normalUv, z));
    vec3 lightDir = normalize(vec3(-0.34, -0.42, 0.84));
    float light = clamp(dot(normal, lightDir), 0.0, 1.0);
    float fresnel = pow(1.0 - clamp(normal.z, 0.0, 1.0), 2.2);

    float core = pow(clamp(1.0 - radius / max(sphereRadius * 0.92, 0.001), 0.0, 1.0), 2.4);
    float aura = smoothstep(sphereRadius + outerGlowWidth, sphereRadius - 0.08, radius);
    float softMask = 1.0 - smoothstep(sphereRadius + outerGlowWidth * 0.82, sphereRadius + outerGlowWidth + 0.12, radius);

    float flicker = 0.93 + 0.07 * sin(ubuf.time * 17.0 + audioReactive * 8.0 + ubuf.layerRole * 1.3);
    vec3 colorShift = vec3(
        0.93 + 0.07 * sin(ubuf.time * 0.17 + ubuf.hueShift),
        0.92 + 0.08 * sin(ubuf.time * 0.23 + 1.3 + ubuf.hueShift),
        0.90 + 0.10 * sin(ubuf.time * 0.19 + 2.2 + ubuf.hueShift)
    );

    vec3 coreColor = innerColor * core * (0.72 + 0.48 * speakingReactive + 0.12 * thinkingAmount);
    vec3 auraColor = outerColor * aura * (0.42 + 0.18 * listeningAmount + 0.2 * executingAmount);
    vec3 lit = marchColor * (0.72 + light * 0.58)
        + coreColor
        + auraColor
        + vec3(0.52, 0.82, 1.0) * fresnel * 0.34;
    lit *= flicker * colorShift;

    float alpha = clamp(
        marchAlpha * 1.22
        + core * (0.42 + speakingReactive * 0.18)
        + aura * 0.38
        + fresnel * 0.16
        + depthFalloff * 0.08,
        0.0,
        1.0);
    alpha *= softMask;

    fragColor = vec4(max(lit, vec3(0.0)), alpha * ubuf.qt_Opacity);
}
