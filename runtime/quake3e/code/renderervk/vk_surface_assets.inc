/* Frozen Neural Light surface assets, selected 2026-10-08.
 * Include AFTER surfaceValidatedAssets_t is declared. No Vulkan calls belong
 * here: every file is validated before the caller allocates GPU resources.
 * The caller supplies an empty destination, then calls surface_free_assets()
 * after upload (or on its own setup failure). Both surface modes load the
 * selected correction, so switching modes needs no file access or allocation.
 */
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#endif
#endif

static const char surface_geometry_sha[] = "657e3d200dcf36412bec2e4b3b3730737a5de07a8e8a49df6e2ba80bb9b1c9ce";
static const char surface_material_sha[] = "c5ff9715587f4ae1bc0d31c9c3bc98cdaa802a53e1a91e9950f0df468b049f16";
static const char surface_scene_sha[] = "c844dfc2401c5fb3b8e30d9f4f375dbeca4fbb8d5aeb21ff66e6876bc2237f93";
static char surface_base_sha[65] = "cdd20ada8c45ea7aaacf2dd79ea55314c37e68be1d7857e8f2692fbb344ac064";
static char surface_correction_sha[65] = "4b3a42f65b1598f0236ac427ac6083685f120d2774ee8f631fd1ab5d34803bf1";

static const surfaceExperimentAssetIdentity_t *surface_selected_identity;

/* Explicit little-endian reads avoid alignment and host-endian assumptions. */
static uint32_t surface_asset_word(const byte *a, size_t offset) {
    return (uint32_t)a[offset] | ((uint32_t)a[offset + 1] << 8) |
        ((uint32_t)a[offset + 2] << 16) | ((uint32_t)a[offset + 3] << 24);
}

static uint64_t surface_asset_quad(const byte *a, size_t offset) {
    return (uint64_t)surface_asset_word(a, offset) |
        ((uint64_t)surface_asset_word(a, offset + 4) << 32);
}

static float surface_asset_real(const byte *a, size_t offset) {
    uint32_t bits = surface_asset_word(a, offset);
    float value;
    Com_Memcpy(&value, &bits, sizeof(value));
    return value;
}

static qboolean surface_asset_finite(const byte *a, size_t offset) {
    return (surface_asset_word(a, offset) & 0x7f800000U) != 0x7f800000U;
}

static qboolean surface_digest_is(const byte digest[32], const char *hex) {
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < 32; ++i) {
        if (digits[digest[i] >> 4] != hex[i * 2] ||
            digits[digest[i] & 15] != hex[i * 2 + 1]) return qfalse;
    }
    return qtrue;
}

static qboolean surface_asset_sha256(const void *data, size_t bytes, byte digest[32]) {
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    PUCHAR object = NULL;
    DWORD objectBytes = 0, returned = 0;
    qboolean ok = qfalse;
    /* File caps are smaller than ULONG_MAX; preserve that fact explicitly. */
    if (bytes > 0xffffffffU) return qfalse;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0) goto done;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objectBytes,
            sizeof(objectBytes), &returned, 0) < 0 || returned != sizeof(objectBytes) ||
            !objectBytes || objectBytes > 1024U * 1024U) goto done;
    object = (PUCHAR)ri.Malloc(objectBytes);
    if (!object) goto done;
    if (BCryptCreateHash(algorithm, &hash, object, objectBytes, NULL, 0, 0) < 0 ||
        BCryptHashData(hash, (PUCHAR)data, (ULONG)bytes, 0) < 0 ||
        BCryptFinishHash(hash, digest, 32, 0) < 0) goto done;
    ok = qtrue;
done:
    if (hash) BCryptDestroyHash(hash);
    if (object) ri.Free(object);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
#else
    (void)data; (void)bytes; (void)digest;
    /* This isolated Windows build has no silently weaker hash fallback. */
    return qfalse;
#endif
}

static const char *surface_validate_charts(const byte *a, uint32_t charts, uint32_t texels,
        const byte *exactBase) {
    uint32_t i, j, start, width, height, flags, otherStart;
    uint64_t end, otherEnd;
    size_t offset, other;
    for (i = 0; i < charts; ++i) {
        offset = 256 + (size_t)i * 16;
        start = surface_asset_word(a, offset);
        width = surface_asset_word(a, offset + 4);
        height = surface_asset_word(a, offset + 8);
        flags = surface_asset_word(a, offset + 12);
        end = (uint64_t)start + (uint64_t)width * height;
        if (!width || !height || flags > 3 || end > texels) return "chart range or periodicity";
        if (exactBase && memcmp(a + offset, exactBase + offset, 16)) return "exact frozen chart layout";
        /* Exactly 88 records: this bounded pair check needs no allocation. */
        for (j = 0; j < i; ++j) {
            other = 256 + (size_t)j * 16;
            otherStart = surface_asset_word(a, other);
            otherEnd = (uint64_t)otherStart +
                (uint64_t)surface_asset_word(a, other + 4) * surface_asset_word(a, other + 8);
            if ((uint64_t)start < otherEnd && (uint64_t)otherStart < end) return "overlapping charts";
        }
    }
    return NULL;
}

static const char *surface_validate_base(const byte *a, size_t bytes,
        const surfaceExperimentAssetIdentity_t **accepted) {
    static const float normalization[] = { -174, -166, 34, 112, 166, 302, 8 };
    uint32_t checksum=2166136261U,charts,texels; uint64_t payload,learned,usedTexels=0;
    byte digest[32]; size_t i,k; const char *failure;
    const surfaceExperimentAssetIdentity_t *identity=NULL;
    *accepted=NULL;
    if(bytes<256 || memcmp(a,"NLSF0001",8))return "NLSF0001 header/magic";
    texels=surface_asset_word(a,16);charts=surface_asset_word(a,40);
    if(surface_asset_word(a,8)!=256 || surface_asset_word(a,12)!=2 || !texels || texels>SURFACE_EXPERIMENT_MAX_TEXELS ||
       surface_asset_word(a,20)!=12 || surface_asset_word(a,24)!=12 || surface_asset_word(a,28) ||
       surface_asset_word(a,32) || surface_asset_word(a,36) || charts!=88)return "directional shape/chart count/texel cap";
    for(i=44;i<60;i+=4)if(surface_asset_word(a,i)!=1)return "feature/resolve/transfer flags";
    for(i=208;i<256;i++)if(a[i])return "reserved header bytes";
    for(i=0;i<7;i++)if(surface_asset_real(a,160+i*4)!=normalization[i])return "feature normalization";
    if(surface_asset_word(a,188))return "reserved normalization";
    payload=(uint64_t)charts*16+(uint64_t)texels*12*4;learned=(uint64_t)texels*12;
    if(surface_asset_quad(a,192)!=payload || surface_asset_quad(a,200)!=learned || (uint64_t)bytes!=256+payload)
        return "exact base payload/file lengths";
    for(i=0;i<bytes;i++)checksum=(checksum^((i>=60&&i<64)?0:a[i]))*16777619U;
    if(checksum!=surface_asset_word(a,60))return "whole-file FNV";
    if(!surface_digest_is(a+64,surface_geometry_sha)||!surface_digest_is(a+96,surface_scene_sha))return "physical scene/geometry";
    if(!surface_asset_sha256(a+256,(size_t)charts*16,digest)||memcmp(digest,a+128,32))return "chart SHA256";
    failure=surface_validate_charts(a,charts,texels,NULL);if(failure)return failure;
    for(i=256+(size_t)charts*16;i<bytes;i+=4)if(!surface_asset_finite(a,i))return "nonfinite base atlas";
    if(!surface_asset_sha256(a,bytes,digest))return "SHA256 provider failure";
    for(k=0;k<SURFACE_EXPERIMENT_ASSET_COUNT;k++) {
        const surfaceExperimentAssetIdentity_t *row=&surface_experiment_assets[k];
        if(row->texels==texels && row->baseBytes==(uint64_t)bytes && surface_digest_is(digest,row->baseSHA)) {identity=row;break;}
    }
    if(!identity)return "base absent from explicit experiment allowlist";
    if(identity->genuineFHD) {
        if(texels<65536)return "B requires >=4x pooled A texels";
        for(i=0;i<88;i++) {
            size_t p=256+i*16;
            if(surface_asset_word(a,p+4)<2*surface_original_charts[i][0] ||
               surface_asset_word(a,p+8)<2*surface_original_charts[i][1] ||
               surface_asset_word(a,p+12)!=surface_original_charts[i][2])return "B chart detail/wrap contract";
            usedTexels+=(uint64_t)surface_asset_word(a,p+4)*surface_asset_word(a,p+8);
        }
        if(usedTexels<4ULL*SURFACE_ORIGINAL_USED_TEXELS)return "B requires >=4x USED chart texels";
    }
    if(!identity->genuineFHD && (texels!=16384 || !surface_digest_is(a+128,SURFACE_ORIGINAL_CHART_SHA)))
        return "A/original requires exact original coarse chart layout";
    *accepted=identity;return NULL;
}

static const char *surface_validate_correction(const byte *a,size_t bytes,
        const byte *base,size_t baseBytes,const surfaceExperimentAssetIdentity_t **selected) {
    uint32_t charts,texels;uint64_t learned,payload;byte digest[32];size_t i,k;const char *failure;
    const surfaceExperimentAssetIdentity_t *identity=NULL;
    if(bytes<256 || memcmp(a,"NLSR0001",8))return "NLSR0001 identity/header";
    if(!surface_asset_sha256(a,bytes,digest))return "correction SHA provider failure";
    for(k=0;k<SURFACE_EXPERIMENT_ASSET_COUNT;k++) {
        const surfaceExperimentAssetIdentity_t *row=&surface_experiment_assets[k];
        if(row->correctionBytes==(uint64_t)bytes && row->baseBytes==(uint64_t)baseBytes &&
           surface_digest_is(digest,row->correctionSHA) && surface_digest_is(a+64,row->baseSHA)) {identity=row;break;}
    }
    if(!identity)return "exact paired base/correction absent from compiled allowlist";
    texels=surface_asset_word(a,16);charts=surface_asset_word(a,36);
    if(surface_asset_word(a,8)!=256 || surface_asset_word(a,12)!=1 || texels!=identity->texels ||
       surface_asset_word(a,20)!=4 || surface_asset_word(a,24)!=34 || surface_asset_word(a,28)!=32 ||
       surface_asset_word(a,32)!=2275 || charts!=88 || charts!=surface_asset_word(base,40))return "small32 decoder/chart shape";
    for(i=40;i<52;i+=4)if(surface_asset_word(a,i)!=1)return "correction feature/resolve flags";
    for(i=52;i<64;i++)if(a[i])return "reserved correction word";
    for(i=208;i<256;i++)if(a[i])return "reserved correction bytes";
    learned=(uint64_t)texels*4+2275;payload=(uint64_t)charts*16+learned*4;
    if((uint64_t)bytes!=identity->correctionBytes || (uint64_t)bytes!=256+payload ||
       surface_asset_quad(a,192)!=payload || surface_asset_quad(a,200)!=learned)return "exact correction lengths";
    if((uint64_t)baseBytes+(uint64_t)bytes>SURFACE_EXPERIMENT_COMBINED_CAP)return "combined asset memory cap";
    if(!surface_asset_sha256(base,baseBytes,digest)||memcmp(a+64,digest,32)||!surface_digest_is(a+64,identity->baseSHA))
        return "paired baseline SHA256";
    if(!surface_digest_is(a+96,surface_geometry_sha)||!surface_digest_is(a+128,surface_scene_sha))return "physical scene/geometry";
    if(!surface_asset_sha256(a+256,bytes-256,digest)||memcmp(digest,a+160,32))return "payload SHA256";
    failure=surface_validate_charts(a,charts,texels,base);if(failure)return failure;
    for(i=256+(size_t)charts*16;i<bytes;i+=4)if(!surface_asset_finite(a,i))return "nonfinite correction";
    if(!surface_asset_sha256(a,bytes,digest)||!surface_digest_is(digest,identity->correctionSHA))return "paired correction allowlist SHA256";
    *selected=identity;return NULL;
}

static const char *surface_validate_geometry(const byte *triangles, size_t triangleBytes,
        const byte *materials, size_t materialBytes, uint32_t charts) {
    byte digest[32];
    size_t i, j;
    if (!triangleBytes || triangleBytes % 112 || triangleBytes / 112 > 100000) return "triangle count/112-byte stride";
    if (!materialBytes || materialBytes % 16) return "material count/16-byte stride";
    for (i = 0; i < triangleBytes; i += 112) {
        if (surface_asset_word(triangles, i + 96) >= charts ||
            surface_asset_word(triangles, i + 100) >= materialBytes / 16) return "triangle chart/material address";
        for (j = 0; j < 96; j += 4)
            if (!surface_asset_finite(triangles, i + j)) return "nonfinite triangle position/normal/UV";
        if (!surface_asset_finite(triangles, i + 108)) return "nonfinite triangle roughness";
    }
    for (i = 0; i < materialBytes; i += 4)
        if (!surface_asset_finite(materials, i)) return "nonfinite material scalar";
    if (!surface_asset_sha256(triangles, triangleBytes, digest)) return "SHA256 provider failure";
    if (!surface_digest_is(digest, surface_geometry_sha)) return "exact evaluated triangle SHA256 identity";
    if (!surface_asset_sha256(materials, materialBytes, digest)) return "SHA256 provider failure";
    if (!surface_digest_is(digest, surface_material_sha)) return "exact material SHA256 identity";
    return NULL;
}

static qboolean surface_read_asset(const char *role, const char *path, size_t maxBytes,
        const void **destination, VkDeviceSize *destinationBytes) {
    int expectedBytes, loadedBytes;
    void *buffer = NULL;
    const char *failure = NULL;
    /* File names are game-relative FS paths, never host paths. */
    if (!path || !*path || strlen(path) >= MAX_QPATH || path[0] == '/' ||
        path[0] == '\\' || strchr(path, ':') || strstr(path, "..")) {
        failure = "invalid game-relative path";
        goto rejected;
    }
    /* FS_ReadFile(NULL) performs a length query without allocating file bytes. */
    expectedBytes = ri.FS_ReadFile(path, NULL);
    if (expectedBytes <= 0) {
        failure = "missing or empty file";
        goto rejected;
    }
    if ((size_t)expectedBytes > maxBytes) {
        failure = "file exceeds bounded allocation limit";
        goto rejected;
    }
    loadedBytes = ri.FS_ReadFile(path, &buffer);
    if (!buffer || loadedBytes != expectedBytes) {
        failure = "read failure or file changed during length preflight";
        goto rejected;
    }
    *destination = buffer;
    *destinationBytes = (VkDeviceSize)loadedBytes;
    return qtrue;
rejected:
    if (buffer) ri.FS_FreeFile(buffer);
    ri.Printf(PRINT_WARNING, "SURFACE ASSET REJECTED [%s] (%s): %s; diagnostic checker.\n",
        role, failure, path ? path : "<null>");
    return qfalse;
}

static void surface_free_assets(surfaceValidatedAssets_t *assets) {
    if (assets->triangles) ri.FS_FreeFile((void *)assets->triangles);
    if (assets->materials) ri.FS_FreeFile((void *)assets->materials);
    if (assets->appearance) ri.FS_FreeFile((void *)assets->appearance);
    if (assets->correction) ri.FS_FreeFile((void *)assets->correction);
    Com_Memset(assets, 0, sizeof(*assets));
}

static qboolean surface_load_assets(surfaceValidatedAssets_t *assets) {
    surfaceValidatedAssets_t loaded;
    const char *failure, *role, *path;
    const surfaceExperimentAssetIdentity_t *identity=NULL;
    const char *trianglePath = r_surfaceTriangles ? r_surfaceTriangles->string : "surface/triangles.bin";
    const char *materialPath = r_surfaceMaterials ? r_surfaceMaterials->string : "surface/materials.bin";
    const char *basePath = r_surfaceBase ? r_surfaceBase->string : "surface/directional.nlsf";
    const char *correctionPath = r_surfaceCorrection ? r_surfaceCorrection->string : "surface/small32_zero.nlsr";
    Com_Memset(assets, 0, sizeof(*assets));
    Com_Memset(&loaded, 0, sizeof(loaded));
    if (!surface_read_asset("triangles", trianglePath, 12U * 1024U * 1024U,
            &loaded.triangles, &loaded.triangleBytes) ||
        !surface_read_asset("materials", materialPath, 1024U * 1024U,
            &loaded.materials, &loaded.materialBytes) ||
        !surface_read_asset("base", basePath, SURFACE_EXPERIMENT_BASE_CAP,
            &loaded.appearance, &loaded.appearanceBytes) ||
        !surface_read_asset("correction", correctionPath, SURFACE_EXPERIMENT_CORRECTION_CAP,
            &loaded.correction, &loaded.correctionBytes)) goto failed;
    role = "base"; path = basePath;
    failure = surface_validate_base((const byte *)loaded.appearance, (size_t)loaded.appearanceBytes,&identity);
    if (failure) goto invalid;
    role = "geometry/materials"; path = trianglePath;
    failure = surface_validate_geometry((const byte *)loaded.triangles, (size_t)loaded.triangleBytes,
        (const byte *)loaded.materials, (size_t)loaded.materialBytes, 88);
    if (failure) goto invalid;
    role = "correction"; path = correctionPath;
    failure = surface_validate_correction((const byte *)loaded.correction, (size_t)loaded.correctionBytes,
        (const byte *)loaded.appearance, (size_t)loaded.appearanceBytes,&identity);
    if (failure) goto invalid;
    loaded.triangleCount = (uint32_t)(loaded.triangleBytes / 112);
    *assets = loaded;
    surface_selected_identity=identity;
    Q_strncpyz(surface_base_sha,identity->baseSHA,sizeof(surface_base_sha));
    Q_strncpyz(surface_correction_sha,identity->correctionSHA,sizeof(surface_correction_sha));
    ri.Printf(PRINT_ALL,"SURFACE ASSETS VALIDATED: %u triangles,88 charts; experiment=%s texels=%u base=%lluB correction=%lluB; genuineHigherDetailFHD=%d.\n",loaded.triangleCount,identity->name,identity->texels,(unsigned long long)identity->baseBytes,(unsigned long long)identity->correctionBytes,identity->genuineFHD);
    ri.Printf(PRINT_ALL, "SURFACE SHA256 geometry=%s materials=%s scene=%s\n",
        surface_geometry_sha, surface_material_sha, surface_scene_sha);
    ri.Printf(PRINT_ALL, "SURFACE SHA256 base=%s correction=%s; validated before Vulkan allocation.\n",
        surface_base_sha, surface_correction_sha);
    return qtrue;
invalid:
    ri.Printf(PRINT_WARNING, "SURFACE ASSET REJECTED [%s] (%s): %s; diagnostic checker.\n",
        role, failure, path);
failed:
    surface_free_assets(&loaded);
    return qfalse;
}
