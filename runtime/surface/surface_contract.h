#pragma once
#include <cstdint>

// Shared private renderer set. No bindings in the existing demo are modified.
enum SurfaceBinding : uint32_t {
    SURFACE_TRIANGLES = 0, SURFACE_MATERIALS = 1, SURFACE_APPEARANCE = 2,
    SURFACE_IDS = 3, SURFACE_BARYCENTRICS = 4, SURFACE_OUTPUT = 5,
    SURFACE_DIAGNOSTICS = 6
};
struct SurfaceFrame {
    float origin[4];          // REAL camera origin, w reserved
    float forwardTanX[4];     // REAL forward, tan(horizontal FOV / 2)
    float rightTanY[4];       // REAL right, tan(vertical FOV / 2)
    float upNear[4];          // REAL up, reverse-Z near distance (0.128 game units)
    uint32_t info[4];         // native width, native height, diagnostics flag, triangle count
    uint32_t reserved[4];
};
struct SurfaceTriangle {
    float vertices[3][8];     // (Px,Py,Pz,Nx),(Ny,Nz,U,V), each corner
    uint32_t metadata[4];     // chart, material, object, roughness float bits
};
struct SurfaceMaterial {
    float roughness, metallic, reserved0, reserved1;
};
static_assert(sizeof(SurfaceFrame) == 96, "Shader push layout");
static_assert(sizeof(SurfaceTriangle) == 112, "Frozen evaluated triangle layout");
static_assert(sizeof(SurfaceMaterial) == 16, "Material scalar layout");
constexpr uint32_t SURFACE_DIAGNOSTIC_FLOATS = 48;
constexpr uint32_t SURFACE_MAX_NATIVE_PIXELS = 131072;
constexpr uint32_t SURFACE_MAX_TRIANGLES = 100000;
