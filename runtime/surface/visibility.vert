#version 450
#extension GL_GOOGLE_include_directive : require
#include "surface_geometry.glsl"
layout(location=0) out vec2 bary12;
layout(location=1) flat out uint triangleID;
void main() {
    triangleID=uint(gl_VertexIndex)/3u;
    int corner=gl_VertexIndex%3;
    vec3 point=surfacePosition(geometry.triangles[triangleID],corner)-surfaceVirtualOrigin();
    float forwardDistance=dot(point,surfaceMirrorVector(frame.forwardTanX.xyz));
    float horizontal=dot(point,surfaceMirrorVector(frame.rightTanY.xyz))/frame.forwardTanX.w;
    float vertical=-dot(point,surfaceMirrorVector(frame.upNear.xyz))/frame.rightTanY.w;
    // Infinite reverse-Z. Depth clear=0, compare=GREATER, culling disabled.
    gl_Position=vec4(horizontal,vertical,frame.upNear.w,forwardDistance);
    bary12=corner==1 ? vec2(1,0) : (corner==2 ? vec2(0,1) : vec2(0));
}
