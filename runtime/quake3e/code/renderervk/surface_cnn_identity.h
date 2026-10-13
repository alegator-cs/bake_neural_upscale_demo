#pragma once
// New matched CNN, when admitted, remains noise-limited and nonpromotable.
#define SURFACE_MATCHED_CNN_PROMOTION_ALLOWED 0
// Exact pilot identities plus at most one owner-frozen C16; no hash fallback.
typedef struct { const char *name,*modelSHA,*baselineSHA,*codeSHA,*protocolSHA,*qualityStatus; uint32_t width,parameters,bytes,diagnostic; } surfaceExperimentCnnIdentity_t;
static const surfaceExperimentCnnIdentity_t surface_experiment_cnns[]={
    {"pilot_C8","3124928cbde72d9912f6de71b1d7ad1c73f830035c02795d75b0751be6b77f67","4b3a42f65b1598f0236ac427ac6083685f120d2774ee8f631fd1ab5d34803bf1","65c92f0f1d89c0cc65206d14e9954602f12fbeca278afd1b88da1d6f1253c504","6f96c320c973bd54ee8d9d57126af19adf2587757c0087ade103db1df60a1e99","failed_selection_quality_guards_diagnostic_only",8u,1456u,6080u,1u},
    {"pilot_C16","219e88ffdaddc3c11836761cc7d3addd21cbd9bc690d83a107c064e15322d5d3","4b3a42f65b1598f0236ac427ac6083685f120d2774ee8f631fd1ab5d34803bf1","65c92f0f1d89c0cc65206d14e9954602f12fbeca278afd1b88da1d6f1253c504","6f96c320c973bd54ee8d9d57126af19adf2587757c0087ade103db1df60a1e99","failed_selection_quality_guards_diagnostic_only",16u,4000u,16256u,1u},
    {"pilot_C32","ef9f642c937df5fdb6e1e0fb82c627e2791ddde256f803b05f72eb3d0c21ed56","4b3a42f65b1598f0236ac427ac6083685f120d2774ee8f631fd1ab5d34803bf1","65c92f0f1d89c0cc65206d14e9954602f12fbeca278afd1b88da1d6f1253c504","6f96c320c973bd54ee8d9d57126af19adf2587757c0087ade103db1df60a1e99","failed_selection_quality_guards_diagnostic_only",32u,12544u,50432u,1u},
    {"noise_limited_common384_C16","d56767cf48ff7e2933867c8a7ddec43a4dabc4808eb5b1d53c5bf6e29441e3a5","02707afc205514bdd4a70675bc5e74e3e08f483783545822ff2b71406f062ece","39dabf8148069941ae10a09dbeba9e87774f4b7bf2fd4a783867803828f55bde","28a515aba61e95c66d2b93b8a4ed27f754bb22d7b507c1f67c8adea3eff2084f","noise_limited_common384_nonpromotable_runtime_validation_pending",16u,4000u,16256u,1u},
};
#define SURFACE_EXPERIMENT_CNN_COUNT (sizeof(surface_experiment_cnns)/sizeof(surface_experiment_cnns[0]))
static const surfaceExperimentCnnIdentity_t *surface_selected_cnn_identity;
#define SURFACE_CNN_CODE_SHA (surface_selected_cnn_identity?surface_selected_cnn_identity->codeSHA:"none")
#define SURFACE_CNN_PROTOCOL_SHA (surface_selected_cnn_identity?surface_selected_cnn_identity->protocolSHA:"none")
#define SURFACE_CNN_DIAGNOSTIC (surface_selected_cnn_identity?surface_selected_cnn_identity->diagnostic:1u)
#define SURFACE_CNN_QUALITY_STATUS (surface_selected_cnn_identity?surface_selected_cnn_identity->qualityStatus:"pending_identity")
#define SURFACE_CNN_PRODUCTION_CHOICE "bilinear_bypass"
static const char *surface_cnn_sha_for_width(uint32_t width) {
    return surface_selected_cnn_identity && surface_selected_cnn_identity->width==width?surface_selected_cnn_identity->modelSHA:"";
}
