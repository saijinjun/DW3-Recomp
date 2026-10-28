#version 450
#extension GL_EXT_samplerless_texture_functions : require
// Blends the notification image (or the touch controls) over the window rectangle `dest`.

layout(set = 0, binding = 0) uniform texture2D uOsd;

layout(push_constant) uniform Params {
    vec4 dest;
    vec2 window;
    float opacity;
    float hdrScale;  // 0 for an SDR swap chain; otherwise the multiplier of linear SDR white (paper white / 80 nits for scRGB, 1 for EDR)
} params;

layout(location = 0) in vec2 vFrac;
layout(location = 0) out vec4 outColor;

void main() {
    ivec2 size = textureSize(uOsd, 0);
    // Bilinear by hand (the texture has no sampler): exact texel centres when the image is drawn 1:1, smooth when it is
    // drawn larger (touch controls rendered at half resolution). Colour is weighted by alpha so edges do not darken.
    vec2 pos = vFrac * vec2(size) - 0.5;
    ivec2 i0 = ivec2(floor(pos));
    vec2 f = pos - vec2(i0);
    vec4 t00 = texelFetch(uOsd, clamp(i0, ivec2(0), size - 1), 0);
    vec4 t10 = texelFetch(uOsd, clamp(i0 + ivec2(1, 0), ivec2(0), size - 1), 0);
    vec4 t01 = texelFetch(uOsd, clamp(i0 + ivec2(0, 1), ivec2(0), size - 1), 0);
    vec4 t11 = texelFetch(uOsd, clamp(i0 + ivec2(1, 1), ivec2(0), size - 1), 0);
    vec4 p00 = vec4(t00.rgb * t00.a, t00.a), p10 = vec4(t10.rgb * t10.a, t10.a);
    vec4 p01 = vec4(t01.rgb * t01.a, t01.a), p11 = vec4(t11.rgb * t11.a, t11.a);
    vec4 p = mix(mix(p00, p10, f.x), mix(p01, p11, f.x), f.y);
    vec4 c = vec4(p.a > 0.0 ? p.rgb / p.a : vec3(0.0), p.a);
    vec3 rgb = c.rgb;
    if (params.hdrScale > 0.0) {
        rgb = clamp(rgb, 0.0, 1.0);
        rgb = mix(pow((rgb + 0.055) / 1.055, vec3(2.4)), rgb / 12.92, lessThanEqual(rgb, vec3(0.04045))) * params.hdrScale;
    }
    outColor = vec4(rgb, c.a * params.opacity);
}
