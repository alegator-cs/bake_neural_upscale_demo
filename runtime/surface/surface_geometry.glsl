#ifndef SURFACE_GEOMETRY_GLSL
#define SURFACE_GEOMETRY_GLSL
#include "surface_frame.glsl"

struct SurfaceTriangle { vec4 vertices[6]; uvec4 metadata; };
layout(std430,binding=0) readonly buffer SurfaceTriangles { SurfaceTriangle triangles[]; } geometry;
layout(std430,binding=1) readonly buffer SurfaceMaterials { vec4 values[]; } materials;

vec3 surfacePosition(SurfaceTriangle triangle, int corner) { return triangle.vertices[2*corner].xyz; }
vec3 surfaceCornerNormal(SurfaceTriangle triangle, int corner) {
    return vec3(triangle.vertices[2*corner].w, triangle.vertices[2*corner+1].xy);
}
vec2 surfaceCornerUV(SurfaceTriangle triangle, int corner) { return triangle.vertices[2*corner+1].zw; }

struct SurfaceHit {
    vec3 position;
    vec3 normal;
    vec3 geometricNormal;
    vec3 view;
    vec3 reflected;
    vec2 uv;
    vec2 barycentric;
    vec4 derivatives;
    float roughness;
    float metallic;
    float ndotv;
    float depth;
    uint chart;
    uint material;
    bool validDerivatives;
};

// Canonical compute visibility already supplies ray-intersection barycentrics.
// Keep them, and reconstruct features using the same CPU-order ray/math.
SurfaceHit surfaceFromCanonicalHit(uint triangleID,vec2 bary12,vec2 nativePixel) {
    SurfaceTriangle triangle=geometry.triangles[triangleID];
    precise vec3 p0=surfacePosition(triangle,0),p1=surfacePosition(triangle,1),p2=surfacePosition(triangle,2);
    precise vec2 uv0=surfaceCornerUV(triangle,0),uv1=surfaceCornerUV(triangle,1),uv2=surfaceCornerUV(triangle,2);
    precise float b0=1.0-bary12.x-bary12.y;
    precise vec3 e1=p1-p0,e2=p2-p0;
    precise vec3 rawNg=surfaceCanonicalCross(e1,e2);
    precise vec3 q=surfaceCanonicalVirtualQ(nativePixel);
    precise vec3 direction=surfaceCanonicalNormalize(q);
    precise vec3 view=direction*(-1.0);
    precise vec3 interpolatedNormal=(surfaceCornerNormal(triangle,0)*b0
                                  +surfaceCornerNormal(triangle,1)*bary12.x)
                                  +surfaceCornerNormal(triangle,2)*bary12.y;
    precise vec3 normal=surfaceCanonicalNormalize(interpolatedNormal);
    precise vec3 geometricNormal=surfaceCanonicalNormalize(rawNg);
    if(surfaceCanonicalDot(geometricNormal,view)<0.0) {geometricNormal=geometricNormal*(-1.0);normal=normal*(-1.0);}
    precise float normalDotView=surfaceCanonicalDot(normal,view);
    precise float twiceNormalDotView=2.0*normalDotView;
    precise vec3 reflectedUnnormalized=normal*twiceNormalDotView-view;
    precise vec3 interpolatedPosition=(p0*b0+p1*bary12.x)+p2*bary12.y;
    precise vec2 interpolatedUV=(uv0*b0+uv1*bary12.x)+uv2*bary12.y;

    SurfaceHit hit;
    hit.barycentric=bary12;
    hit.position=interpolatedPosition;
    hit.uv=interpolatedUV;
    hit.normal=normal;hit.geometricNormal=geometricNormal;hit.view=view;
    hit.reflected=surfaceCanonicalNormalize(reflectedUnnormalized);
    hit.ndotv=clamp(normalDotView,0.0,1.0);
    hit.roughness=uintBitsToFloat(triangle.metadata.w);
    hit.material=triangle.metadata.y;hit.metallic=materials.values[hit.material].y;
    hit.chart=triangle.metadata.x;

    // Diagnostic depth only: same Moller numerator/order as visibility. Geometry
    // interpolation and appearance use the preserved barycentric pair above.
    precise vec3 pvec=surfaceCanonicalCross(direction,e2);
    precise float determinant=surfaceCanonicalDot(e1,pvec);
    precise float inverseDeterminant=1.0/determinant;
    precise vec3 fromCorner=surfaceVirtualOrigin()-p0;
    precise vec3 qvec=surfaceCanonicalCross(fromCorner,e1);
    hit.depth=surfaceCanonicalDot(e2,qvec)*inverseDeterminant;

    precise float areaSquared=surfaceCanonicalDot(rawNg,rawNg);
    precise float denominator=surfaceCanonicalDot(geometricNormal,q);
    hit.validDerivatives=denominator!=0.0&&!isnan(denominator)&&!isinf(denominator)&&areaSquared>=1e-15;
    hit.derivatives=vec4(0.0);
    if(hit.validDerivatives) {
        precise float inverseArea=1.0/areaSquared;
        precise vec3 dual1=surfaceCanonicalCross(e2,rawNg)*inverseArea;
        precise vec3 dual2=surfaceCanonicalCross(rawNg,e1)*inverseArea;
        precise vec3 gradU=dual1*(uv1.x-uv0.x)+dual2*(uv2.x-uv0.x);
        precise vec3 gradV=dual1*(uv1.y-uv0.y)+dual2*(uv2.y-uv0.y);
        precise float qxScale=(2.0*frame.forwardTanX.w)/float(frame.info.x);
        precise float qyScale=(-2.0*frame.rightTanY.w)/float(frame.info.y);
        precise vec3 qx=surfaceMirrorVector(frame.rightTanY.xyz)*qxScale;
        precise vec3 qy=surfaceMirrorVector(frame.upNear.xyz)*qyScale;
        precise float planeT=surfaceCanonicalDot(geometricNormal,p0-surfaceVirtualOrigin())/denominator;
        precise float xRatio=surfaceCanonicalDot(geometricNormal,qx)/denominator;
        precise float yRatio=surfaceCanonicalDot(geometricNormal,qy)/denominator;
        precise vec3 dpdx=(qx-q*xRatio)*planeT;
        precise vec3 dpdy=(qy-q*yRatio)*planeT;
        hit.derivatives=vec4(surfaceCanonicalDot(gradU,dpdx),surfaceCanonicalDot(gradV,dpdx),
                             surfaceCanonicalDot(gradU,dpdy),surfaceCanonicalDot(gradV,dpdy));
        hit.validDerivatives=!any(isnan(hit.derivatives))&&!any(isinf(hit.derivatives));
        if(!hit.validDerivatives)hit.derivatives=vec4(0.0);
    }
    return hit;
}

SurfaceHit surfaceFromRaster(uint triangleID, vec2 bary12, vec2 nativePixel) {
    if((frame.info.z&2u)!=0u)return surfaceFromCanonicalHit(triangleID,bary12,nativePixel);
    SurfaceTriangle triangle = geometry.triangles[triangleID];
    vec3 p0=surfacePosition(triangle,0), p1=surfacePosition(triangle,1), p2=surfacePosition(triangle,2);
    vec2 uv0=surfaceCornerUV(triangle,0), uv1=surfaceCornerUV(triangle,1), uv2=surfaceCornerUV(triangle,2);
    // Rasterization selects the visible triangle. Recover its barycentrics at
    // the exact quadrant ray rather than interpolate quantized screen vertices.
    // This shares the CPU Moller-Trumbore arithmetic and does not change coverage.
    precise vec3 ray=normalize(surfaceMirrorVector(surfaceRealRayUnnormalized(nativePixel)));
    precise vec3 e1=p1-p0,e2=p2-p0;
    precise vec3 pvec=cross(ray,e2);
    precise float inv=1.0/dot(e1,pvec);
    precise vec3 fromCorner=surfaceVirtualOrigin()-p0;
    precise vec3 qvec=cross(fromCorner,e1);
    precise vec2 exactBary=vec2(dot(fromCorner,pvec)*inv,dot(ray,qvec)*inv);
    if(!any(isnan(exactBary))&&!any(isinf(exactBary)))bary12=exactBary;
    vec3 weights=vec3(1.0-bary12.x-bary12.y,bary12);
    SurfaceHit hit;
    hit.barycentric=bary12;
    hit.position=weights.x*p0+weights.y*p1+weights.z*p2;
    hit.uv=weights.x*uv0+weights.y*uv1+weights.z*uv2;
    hit.normal=normalize(weights.x*surfaceCornerNormal(triangle,0)
                       +weights.y*surfaceCornerNormal(triangle,1)
                       +weights.z*surfaceCornerNormal(triangle,2));
    vec3 edge1=p1-p0, edge2=p2-p0, ng=cross(edge1,edge2);
    vec3 virtualOrigin=surfaceVirtualOrigin();
    vec3 q=surfaceMirrorVector(surfaceRealRayUnnormalized(nativePixel));
    hit.view=-normalize(q);
    hit.geometricNormal=normalize(ng);
    if (dot(hit.geometricNormal,hit.view)<0.0) {
        hit.geometricNormal=-hit.geometricNormal;
        hit.normal=-hit.normal;
    }
    hit.reflected=normalize(reflect(-hit.view,hit.normal));
    hit.ndotv=clamp(dot(hit.normal,hit.view),0.0,1.0);
    hit.roughness=uintBitsToFloat(triangle.metadata.w);
    hit.material=triangle.metadata.y;
    hit.metallic=materials.values[hit.material].y;
    hit.chart=triangle.metadata.x;
    hit.depth=length(hit.position-virtualOrigin);
    // Analytic same-triangle UV derivatives per NATIVE output pixel. No screen
    // neighbor lookup, no normal-dependent ray bending and no visibility query.
    // The CPU oracle also uses the oriented, normalized Ng for this plane
    // calculation; the unnormalized cross product still defines UV gradients.
    vec3 planeNormal=hit.geometricNormal;
    float denominator=dot(planeNormal,q), areaSquared=dot(ng,ng);
    hit.validDerivatives=denominator!=0.0 && areaSquared>=1e-15;
    hit.derivatives=vec4(0.0);
    if (hit.validDerivatives) {
        vec3 qx=(2.0*frame.forwardTanX.w/float(frame.info.x))*surfaceMirrorVector(frame.rightTanY.xyz);
        vec3 qy=(-2.0*frame.rightTanY.w/float(frame.info.y))*surfaceMirrorVector(frame.upNear.xyz);
        float k=dot(planeNormal,p0-virtualOrigin)/denominator;
        vec3 dpdx=k*(qx-q*(dot(planeNormal,qx)/denominator));
        vec3 dpdy=k*(qy-q*(dot(planeNormal,qy)/denominator));
        vec3 dual1=cross(edge2,ng)/areaSquared, dual2=cross(ng,edge1)/areaSquared;
        vec3 gradU=(uv1.x-uv0.x)*dual1+(uv2.x-uv0.x)*dual2;
        vec3 gradV=(uv1.y-uv0.y)*dual1+(uv2.y-uv0.y)*dual2;
        hit.derivatives=vec4(dot(gradU,dpdx),dot(gradV,dpdx),dot(gradU,dpdy),dot(gradV,dpdy));
        hit.validDerivatives=!any(isnan(hit.derivatives)) && !any(isinf(hit.derivatives));
        // Preserve coverage and the actual first surface. Diagnostics expose
        // this fallback; no nonfinite derivative may enter an appearance head.
        if (!hit.validDerivatives) hit.derivatives=vec4(0.0);
    }
    return hit;
}
#endif
