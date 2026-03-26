// Qt ShaderEffect-compatible reference version of the orb shader.

#define MAX_DISTANCE 4.0
#define SURFACE_DISTANCE 0.0025

uniform float time;
uniform vec2 resolution;
uniform float audioLevel;
uniform float orbState;
uniform float quality;
uniform float speaking;
uniform float distortion;
uniform float glow;
uniform float pulseAmount;
uniform float flicker;

struct Hit
{
    float dist;
    float closestDist;
    vec3 p;
};

vec4 mod289(vec4 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 perm(vec4 x)
{
    return mod289(((x * 34.0) + 1.0) * x);
}

float noise(vec3 p)
{
    vec3 a = floor(p);
    vec3 d = p - a;
    d = d * d * (3.0 - 2.0 * d);

    vec4 b = a.xxyy + vec4(0.0, 1.0, 0.0, 1.0);
    vec4 k1 = perm(b.xyxy);
    vec4 k2 = perm(k1.xyxy + b.zzww);

    vec4 c = k2 + a.zzzz;
    vec4 k3 = perm(c);
    vec4 k4 = perm(c + 1.0);

    vec4 o1 = fract(k3 * (1.0 / 41.0));
    vec4 o2 = fract(k4 * (1.0 / 41.0));
    vec4 o3 = mix(o1, o2, d.z);
    vec2 o4 = mix(o3.xz, o3.yw, d.x);

    return mix(o4.x, o4.y, d.y);
}

float stateAmount(float target)
{
    return clamp(1.0 - abs(orbState - target), 0.0, 1.0);
}

float pulseValue(float audioReactive, float thinkingAmount, float executingAmount)
{
    float speed = 1.0 + thinkingAmount * 0.75 + executingAmount * 1.2;
    return 0.5 + 0.5 * sin(time * speed + audioReactive * 3.0);
}

float sdf(vec3 point, float audioReactive, float listeningAmount, float thinkingAmount, float executingAmount)
{
    float motionSpeed = 0.3 + thinkingAmount * 0.35 + executingAmount * 0.18;
    vec3 p = vec3(point.xy, time * motionSpeed + point.z);
    p += vec3(
        sin(time + p.y),
        cos(time + p.x),
        sin(time * 0.5)
    ) * 0.2;
    p.xy += vec2(
        noise(vec3(point.yx * 1.4, time * 0.14)),
        noise(vec3(point.xy * 1.1 + 4.3, -time * 0.11))
    ) * 0.08;

    float n0 = noise(p * 1.35 + vec3(0.0, 0.0, time * 0.12));
    float n1 = noise((p.yzx + vec3(time * 0.11, -time * 0.07, time * 0.09)) * 2.45);
    float n2 = noise((p.zxy + vec3(-time * 0.08, time * 0.13, -time * 0.05)) * 3.7);
    float n = n0 * 0.58 + n1 * 0.30 + n2 * 0.12;

    float audioInfluence = audioReactive * 0.3;
    float pulse = pulseValue(audioReactive, thinkingAmount, executingAmount);
    float radius = 0.34 + pulse * (0.02 + executingAmount * 0.025) + audioReactive * 0.02;
    float field = 0.18 + audioInfluence + thinkingAmount * 0.06;

    vec3 listenWarp = point;
    listenWarp.xy += vec2(
        sin(point.y * 14.0 + time * 22.0),
        cos(point.x * 12.0 + time * 18.0)
    ) * 0.008 * listeningAmount * (1.0 + audioReactive);

    return length(listenWarp) - radius - n * field;
}

vec3 getNormal(vec3 point, float audioReactive, float listeningAmount, float thinkingAmount, float executingAmount)
{
    vec2 e = vec2(0.002, 0.0);
    float center = sdf(point, audioReactive, listeningAmount, thinkingAmount, executingAmount);
    vec3 grad = center - vec3(
        sdf(point - e.xyy, audioReactive, listeningAmount, thinkingAmount, executingAmount),
        sdf(point - e.yxy, audioReactive, listeningAmount, thinkingAmount, executingAmount),
        sdf(point - e.yyx, audioReactive, listeningAmount, thinkingAmount, executingAmount));
    return normalize(grad);
}

Hit raymarch(vec3 origin, vec3 direction, int steps, float audioReactive, float listeningAmount, float thinkingAmount, float executingAmount)
{
    Hit hit;
    hit.dist = 0.0;
    hit.closestDist = MAX_DISTANCE;
    hit.p = origin;

    vec3 p = origin;
    for (int i = 0; i < 60; ++i) {
        if (i >= steps) {
            break;
        }

        float field = sdf(p, audioReactive, listeningAmount, thinkingAmount, executingAmount);
        float absField = abs(field);
        hit.closestDist = min(hit.closestDist, absField);
        hit.p = p;

        if (hit.dist >= MAX_DISTANCE || absField <= SURFACE_DISTANCE) {
            break;
        }

        float stepDistance = max(absField * 0.82, 0.012);
        p += direction * stepDistance;
        hit.dist += stepDistance;
    }

    return hit;
}

void main()
{
    vec2 safeResolution = max(resolution, vec2(1.0, 1.0));
    vec2 uv = qt_TexCoord0 * 2.0 - 1.0;
    uv.x *= safeResolution.x / safeResolution.y;

    float radius = length(uv);
    if (radius > 1.08) {
        gl_FragColor = vec4(0.0);
        return;
    }

    float audioReactive = clamp(max(audioLevel, speaking * 0.9), 0.0, 1.0);
    float listeningAmount = stateAmount(1.0);
    float thinkingAmount = stateAmount(2.0);
    float executingAmount = stateAmount(3.0);
    float idleAmount = stateAmount(0.0);

    vec2 distortedUv = uv;
    distortedUv += sin(distortedUv.yx * 4.0 + time) * 0.02 * audioReactive;
    distortedUv += vec2(
        sin(time * 42.0 + uv.y * 18.0),
        cos(time * 37.0 + uv.x * 16.0)
    ) * 0.008 * listeningAmount * (0.4 + audioReactive + distortion);

    float pulse = pulseValue(audioReactive, thinkingAmount, executingAmount);
    distortedUv *= 1.0 + (pulse - 0.5) * (0.05 + executingAmount * 0.06 + pulseAmount * 0.03);

    vec3 origin = vec3(0.0, 0.0, -1.25);
    vec3 direction = normalize(vec3(distortedUv, 1.15));

    int steps = 20 + int(clamp(orbState, 0.0, 3.0) * 10.0);
    if (quality < 0.5) {
        steps = max(20, steps - 6);
    } else if (quality > 1.5) {
        steps = min(60, steps + 10);
    }
    steps = clamp(steps, 20, 60);

    Hit hit = raymarch(origin, direction, steps, audioReactive, listeningAmount, thinkingAmount, executingAmount);
    float hitMask = 1.0 - step(SURFACE_DISTANCE * 1.8, hit.closestDist);

    float edgeMask = 1.0 - smoothstep(0.6, 1.0, radius);
    float densityFalloff = pow(clamp(1.0 - radius, 0.0, 1.0), 1.8);
    float fakeVolume = (1.0 - smoothstep(0.08, 1.0, radius)) * (0.35 + densityFalloff * 0.65);
    float edge = 1.0 - smoothstep(0.6, 1.0, radius);

    vec3 color = vec3(0.0);
    float alpha = edgeMask * clamp(1.0 - hit.closestDist * 3.0, 0.0, 1.0);

    vec3 depthColor = mix(
        vec3(0.2, 0.4, 1.0),
        vec3(0.6, 0.2, 1.0),
        clamp(hit.closestDist * 2.2, 0.0, 1.0)
    );

    float aura = (1.0 - smoothstep(0.18, 0.98, radius)) * (0.25 + audioReactive * 0.2 + glow * 0.2);
    float core = pow(clamp(1.0 - radius / 0.62, 0.0, 1.0), 2.6);
    float breathing = 0.5 + 0.5 * sin(time * (0.8 + idleAmount * 0.2));

    if (hitMask > 0.0) {
        vec3 normal = getNormal(hit.p, audioReactive, listeningAmount, thinkingAmount, executingAmount);
        vec3 lightDir = normalize(vec3(-0.45, 0.35, 0.9));
        float light = clamp(dot(normal, lightDir), 0.0, 1.0);
        float radialShade = 1.0 - clamp(length(hit.p.xy) / 0.42, 0.0, 1.0);
        float fresnel = pow(1.0 - max(dot(normal, vec3(0.0, 0.0, -1.0)), 0.0), 2.0);
        float depth = clamp(1.0 - length(hit.p), 0.0, 1.0);
        float coreEnergy = exp(-length(hit.p) * 6.0);
        float pointLight = clamp(dot(normalize(hit.p + vec3(0.0, 0.0, 0.3)), normalize(vec3(0.5, 0.6, 1.0))), 0.0, 1.0);
        float lighting = pointLight * 0.5 + 0.5;

        color = depthColor * (0.55 + light * 0.7 + radialShade * 0.45);
        color += vec3(0.18, 0.32, 0.8) * fakeVolume;
        color += vec3(0.16, 0.28, 0.72) * depth * 0.5;
        color += coreEnergy * vec3(0.6, 0.8, 1.0);
        color += vec3(0.38, 0.64, 1.0) * core * (0.85 + speaking * 0.6);
        color += vec3(0.45, 0.75, 1.0) * fresnel * 0.35;
        color *= lighting;
        alpha *= 0.88 + depth * 0.34 + coreEnergy * 0.18;
    }

    color += depthColor * aura * (0.32 + breathing * 0.2);
    color += vec3(0.52, 0.82, 1.0) * core * (0.18 + glow * 0.16);

    float localFlicker = 0.94 + sin(time * 16.0 + audioReactive * 7.0) * 0.04 * (0.35 + flicker);
    float brightness = 0.82
        + thinkingAmount * 0.14
        + executingAmount * (0.22 + pulse * 0.22)
        + speaking * 0.18;
    color *= brightness * localFlicker;

    alpha *= 0.82 + fakeVolume * 0.28 + aura * 0.34 + core * 0.22;
    alpha *= edge;
    alpha = clamp(alpha, 0.0, 1.0);

    gl_FragColor = vec4(max(color, vec3(0.0)), alpha);
}
