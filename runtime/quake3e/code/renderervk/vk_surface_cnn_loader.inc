/* Strict NLR40001 intake: exact compiled model/correction/source/protocol identity. */
static uint32_t surface_cnn_width,surface_cnn_parameters,surface_cnn_bytes;
static qboolean surface_load_cnn(void **model) {
    const byte *a;const char *path;
    const surfaceExperimentCnnIdentity_t *identity=NULL;
    uint32_t width,parameters,i;size_t k;
    byte digest[32];int bytes;
    *model=NULL;surface_cnn_width=surface_cnn_parameters=surface_cnn_bytes=0;
    surface_selected_cnn_identity=NULL;
    path=r_surfaceCnnModel?r_surfaceCnnModel->string:NULL;
    if(!path || !*path || strlen(path)>=MAX_QPATH || path[0]=='/' || path[0]==92 || strchr(path,':') || strstr(path,".."))goto rejected;
    bytes=ri.FS_ReadFile(path,NULL);
    if((bytes!=6080 && bytes!=16256 && bytes!=50432) ||
            ri.FS_ReadFile(path,model)!=bytes || !*model)goto rejected;
    a=(const byte *)*model;
    if(!surface_asset_sha256(a,(size_t)bytes,digest))goto rejected;
    for(k=0;k<SURFACE_EXPERIMENT_CNN_COUNT;k++) {
        const surfaceExperimentCnnIdentity_t *row=&surface_experiment_cnns[k];
        if(row->bytes==(uint32_t)bytes && surface_digest_is(digest,row->modelSHA) &&
           !strcmp(row->baselineSHA,surface_correction_sha)) {identity=row;break;}
    }
    if(!identity)goto rejected;
    width=surface_asset_word(a,20);parameters=surface_asset_word(a,40);
    if(width!=identity->width || parameters!=identity->parameters ||
            memcmp(a,"NLR40001",8) || surface_asset_word(a,8)!=256 || surface_asset_word(a,12)!=1 || surface_asset_word(a,16)!=1 ||
            surface_asset_word(a,24)!=4 || surface_asset_word(a,28)!=64 || surface_asset_word(a,32)!=3 ||
            surface_asset_word(a,36)!=4 || parameters!=9u*width*width+102u*width+64u ||
            surface_asset_word(a,44)!=1 || surface_asset_word(a,48)!=1 || surface_asset_word(a,52)!=1 ||
            surface_asset_word(a,56)!=1 || surface_asset_word(a,60)!=1 ||
            !surface_digest_is(a+64,identity->baselineSHA) || !surface_digest_is(a+96,identity->codeSHA) ||
            !surface_digest_is(a+128,identity->protocolSHA) || bytes!=256u+4u*parameters ||
            surface_asset_quad(a,192)!=4u*parameters || !surface_asset_finite(a,200) || !surface_asset_finite(a,204) ||
            surface_asset_real(a,200)!=1.f || surface_asset_real(a,204)!=1.f ||
            !surface_asset_sha256(a+256,4u*parameters,digest) || memcmp(digest,a+160,32))goto rejected;
    for(i=208;i<256;i++)if(a[i])goto rejected;
    for(i=256;i<(uint32_t)bytes;i+=4)if(!surface_asset_finite(a,i))goto rejected;
    surface_cnn_width=width;surface_cnn_parameters=parameters;surface_cnn_bytes=(uint32_t)bytes;
    surface_selected_cnn_identity=identity;
    ri.Printf(PRINT_ALL,"CNN NLR40001 accepted: %dB C%u params%u SHA%s identity%s diagnostic%u; pixel-only4dispatch\n",bytes,width,parameters,identity->modelSHA,identity->name,identity->diagnostic);
    return qtrue;
rejected:
    if(*model)ri.FS_FreeFile(*model);*model=NULL;
    ri.Printf(PRINT_WARNING,"CNN NLR40001 rejected: exact compiled SHA/source/protocol/paired correction/header/finite payload; checker\n");
    return qfalse;
}
