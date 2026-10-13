#ifndef SURFACE_CANONICAL_MATH_GLSL
#define SURFACE_CANONICAL_MATH_GLSL

// Match scalar float32 operation order in first_hit.cpp (/fp:precise).
float surfaceCanonicalDot(vec3 a,vec3 b) {
    precise float x=a.x*b.x,y=a.y*b.y,z=a.z*b.z;
    precise float xy=x+y;
    precise float result=xy+z;
    return result;
}
vec3 surfaceCanonicalCross(vec3 a,vec3 b) {
    precise vec3 result=vec3(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
    return result;
}
vec3 surfaceCanonicalNormalize(vec3 value) {
    precise float squared=surfaceCanonicalDot(value,value);
    precise float magnitude=sqrt(squared);
    precise float inverse=1.0/max(magnitude,1e-30);
    precise vec3 result=value*inverse;
    return result;
}
#endif
