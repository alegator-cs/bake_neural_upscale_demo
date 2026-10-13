#ifndef SURFACE_FRAME_GLSL
#define SURFACE_FRAME_GLSL
layout(push_constant) uniform SurfaceFrame {
    vec4 origin;
    vec4 forwardTanX;
    vec4 rightTanY;
    vec4 upNear;
    uvec4 info;
    uvec4 reserved;
} frame;

vec3 surfaceMirrorVector(vec3 value) { return vec3(-value.x, value.yz); }
vec3 surfaceVirtualOrigin() { return vec3(988.0-frame.origin.x, frame.origin.yz); }
vec3 surfaceRealRayUnnormalized(vec2 nativePixel) {
    vec2 uv = nativePixel / vec2(frame.info.xy);
    return frame.forwardTanX.xyz
         + (2.0*uv.x-1.0)*frame.forwardTanX.w*frame.rightTanY.xyz
         + (1.0-2.0*uv.y)*frame.rightTanY.w*frame.upNear.xyz;
}
#include "surface_canonical_math.glsl"

// Same mirrored quarter-ray operation order used by the CPU oracle.
vec3 surfaceCanonicalVirtualQ(vec2 nativePixel) {
    precise float px=(nativePixel.x/float(frame.info.x))*2.0-1.0;
    precise float py=1.0-(nativePixel.y/float(frame.info.y))*2.0;
    precise float horizontal=px*frame.forwardTanX.w;
    precise float vertical=py*frame.rightTanY.w;
    precise vec3 forward=surfaceMirrorVector(frame.forwardTanX.xyz);
    precise vec3 right=surfaceMirrorVector(frame.rightTanY.xyz);
    precise vec3 up=surfaceMirrorVector(frame.upNear.xyz);
    precise vec3 first=forward+right*horizontal;
    precise vec3 result=first+up*vertical;
    return result;
}
#endif
