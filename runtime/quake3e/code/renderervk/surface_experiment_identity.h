#pragma once
// Noise-limited admission only: failed512 noise proof; no clean-reference/winner claim.
#define SURFACE_MATCHED_NOISE_LIMITED 1
#define SURFACE_MATCHED_PROMOTION_ALLOWED 0
#define SURFACE_MATCHED_DESIRED_TRAIN_VIEWS 384u
#define SURFACE_MATCHED_SELECTED_TRAIN_VIEWS 384u
#define SURFACE_MATCHED_TRAIN_MANIFEST_SHA "f6a0bc5bdf570968d5c01efd33d7fdebe82b4b66179a794d8a85fb53ba7c9fbb"
#define SURFACE_MATCHED_NOISE_DISPOSITION_SHA "d97ade7d9a89d9030005b2668ebb1a331c8f586f0d08bec028db79f133cd8d18"
#define SURFACE_ORIGINAL_CHART_SHA "eab741dc46955245b54445948d58267ca616c7826a80c252a797b31ffb097d0f"
// Generated exact experiment asset allowlist; no path or hash fallback.
#define SURFACE_EXPERIMENT_MAX_TEXELS 262144u
#define SURFACE_EXPERIMENT_BASE_CAP (16u*1024u*1024u)
#define SURFACE_EXPERIMENT_CORRECTION_CAP (8u*1024u*1024u)
#define SURFACE_EXPERIMENT_COMBINED_CAP (64u*1024u*1024u)
#define SURFACE_ORIGINAL_USED_TEXELS 16384u
#define SURFACE_EXPERIMENT_GATE_SHA "fab1a1368d2ee8983e85dfbe95a430a43f55ef348ca01cac08a2c9110c9635ce"
typedef struct { const char *name; uint32_t texels; uint64_t baseBytes,correctionBytes; const char *baseSHA,*correctionSHA; int genuineFHD; } surfaceExperimentAssetIdentity_t;
static const surfaceExperimentAssetIdentity_t surface_experiment_assets[]={
    {"original_coarse_control",16384u,788096ULL,272908ULL,"cdd20ada8c45ea7aaacf2dd79ea55314c37e68be1d7857e8f2692fbb344ac064","4b3a42f65b1598f0236ac427ac6083685f120d2774ee8f631fd1ab5d34803bf1",0},
    {"noise_limited_common384_A",16384u,788096ULL,272908ULL,"546a3d123111056e155f4f0491245775375f4047711441997072cb98bd944f01","02707afc205514bdd4a70675bc5e74e3e08f483783545822ff2b71406f062ece",0},
    {"noise_limited_common384_B",65536u,3147392ULL,1059340ULL,"d202614363bcae939fcb67fe6300605fa77b7d5e04a2237f3f9021be3acd959e","c32eb09d35ac75226e36a2bcff101e247f67bb7281cb6d6f3aca05ed3bede603",1},
};
#define SURFACE_EXPERIMENT_ASSET_COUNT (sizeof(surface_experiment_assets)/sizeof(surface_experiment_assets[0]))
static const uint32_t surface_original_charts[88][3]={
    {87u,5u,0u},
    {2u,2u,0u},
    {27u,2u,0u},
    {27u,2u,0u},
    {23u,27u,0u},
    {2u,2u,0u},
    {42u,33u,0u},
    {2u,2u,0u},
    {2u,29u,0u},
    {2u,29u,0u},
    {2u,2u,0u},
    {2u,2u,0u},
    {6u,91u,0u},
    {2u,2u,0u},
    {29u,28u,0u},
    {17u,16u,0u},
    {2u,2u,0u},
    {2u,2u,0u},
    {6u,91u,0u},
    {2u,2u,0u},
    {17u,16u,0u},
    {30u,28u,0u},
    {2u,2u,0u},
    {2u,2u,0u},
    {84u,5u,0u},
    {2u,2u,0u},
    {18u,2u,0u},
    {18u,2u,0u},
    {2u,2u,0u},
    {25u,30u,0u},
    {5u,66u,0u},
    {2u,2u,0u},
    {3u,43u,0u},
    {2u,38u,0u},
    {2u,6u,0u},
    {2u,3u,0u},
    {5u,69u,0u},
    {2u,2u,0u},
    {2u,40u,0u},
    {2u,35u,0u},
    {2u,7u,0u},
    {2u,3u,0u},
    {6u,70u,0u},
    {2u,2u,0u},
    {2u,39u,0u},
    {2u,42u,0u},
    {2u,2u,0u},
    {2u,4u,0u},
    {5u,67u,0u},
    {2u,2u,0u},
    {2u,38u,0u},
    {2u,39u,0u},
    {3u,7u,0u},
    {2u,3u,0u},
    {5u,67u,0u},
    {2u,2u,0u},
    {2u,35u,0u},
    {3u,43u,0u},
    {2u,6u,0u},
    {2u,4u,0u},
    {42u,9u,0u},
    {2u,2u,0u},
    {25u,5u,0u},
    {20u,4u,0u},
    {19u,16u,0u},
    {10u,9u,0u},
    {34u,22u,1u},
    {45u,9u,0u},
    {2u,2u,0u},
    {16u,4u,0u},
    {22u,5u,0u},
    {16u,19u,0u},
    {9u,11u,0u},
    {27u,21u,1u},
    {25u,16u,1u},
    {49u,13u,3u},
    {18u,41u,0u},
    {2u,2u,0u},
    {3u,43u,0u},
    {2u,30u,0u},
    {3u,17u,0u},
    {4u,20u,0u},
    {17u,39u,0u},
    {2u,2u,0u},
    {2u,28u,0u},
    {4u,40u,0u},
    {3u,16u,0u},
    {4u,18u,0u},
};
