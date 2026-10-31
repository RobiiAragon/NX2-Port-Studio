// SPDX-License-Identifier: GPL-3.0-or-later
// Native words remain native in guest memory; recording decodes into the shared
// host layout, including its vertex-pulling serialization contract.
#if DM_GAME_NVN2
static uint32_t nvn2_vertex_word(const void *state) { uint32_t word; memcpy(&word,state,4); return word; }
static void nvn2_vertex_store(void *state,uint32_t word) { memcpy(state,&word,4); }
static void n2_nvnVertexAttribStateSetDefaults(NVNvertexAttribState *state) { nvn2_vertex_store(state,0); }
static void n2_nvnVertexAttribStateSetFormat(NVNvertexAttribState *state,NVNformat format,ptrdiff_t offset) {
    if ((unsigned)format>1023 || offset<0 || offset>16383) die("[RED] NVN2 vertex format or offset exceeds its native field");
    nvn2_vertex_store(state,(nvn2_vertex_word(state)&0x8000003f)|((uint32_t)format<<21)|((uint32_t)offset<<7)|64);
}
static void n2_nvnVertexAttribStateSetStreamIndex(NVNvertexAttribState *state,int stream) {
    if ((unsigned)stream>31) die("[RED] NVN2 vertex stream exceeds its native field");
    nvn2_vertex_store(state,(nvn2_vertex_word(state)&~31u)|(uint32_t)stream);
}
static void n2_nvnVertexAttribStateGetFormat(const NVNvertexAttribState *state,NVNformat *format,ptrdiff_t *offset) {
    uint32_t word=nvn2_vertex_word(state); *format=(word>>21)&1023; *offset=(word>>7)&16383;
}
static int n2_nvnVertexAttribStateGetStreamIndex(const NVNvertexAttribState *state) { return nvn2_vertex_word(state)&31; }
static int nvn2_vertex_decode(uint32_t word,va_st *out) {
    unsigned format=(word>>21)&1023,stream=word&31;
    if ((word&64) && (format<1 || format>0x30 || stream>=16)) return 0;
    *out=(va_st){.fmt=(word&64)?format:0,.offset=(word>>7)&16383,.stream=stream}; return 1;
}
static void n2_nvnCommandBufferBindVertexAttribState(NVNcommandBuffer *command,int count,const NVNvertexAttribState *states) {
    if (count<0 || count>16 || (count && nvn2_readable_extent(states)<(size_t)count*4))
        die("[RED] NVN2 vertex attribute array exceeds the host or readable range");
    p_bind_vattr *record=REC(command,OP_BIND_VATTR,p_bind_vattr); record->first=0; record->n=count;
    for (int i=0;i<count;i++) {
        va_st decoded;
        if (!nvn2_vertex_decode(nvn2_vertex_word((const uint8_t *)states+i*4),&decoded))
            die("[RED] NVN2 vertex attribute format/stream has no audited Metal path");
        memcpy(&record->st[i],&decoded,4);
    }
}
static void n2_nvnVertexStreamStateSetStride(NVNvertexStreamState *state,ptrdiff_t stride) {
    if (stride<0 || (uint64_t)stride>UINT32_MAX) die("[RED] NVN2 vertex stride exceeds its native field");
    ST(vs_st,state)->stride=(uint32_t)stride;
}
static void n2_nvnVertexStreamStateSetDivisor(NVNvertexStreamState *state,int divisor) {
    if (divisor<0) die("[RED] NVN2 negative vertex divisor is unsupported");
    ST(vs_st,state)->divisor=(uint32_t)divisor;
}
static void n2_nvnCommandBufferBindVertexStreamState(NVNcommandBuffer *command,int count,const NVNvertexStreamState *states) {
    if (count<0 || count>16 || (count && nvn2_readable_extent(states)<(size_t)count*8))
        die("[RED] NVN2 vertex stream array exceeds the host or readable range");
    h_nvnCommandBufferBindVertexStreamState(command,count,states);
}
static void *nvn2_vertex_impl(const char *name) {
#define V2_PTR(n) if (!strcmp(name,#n)) return (void *)n2_##n;
    V2_PTR(nvnVertexStreamStateSetStride) V2_PTR(nvnVertexStreamStateSetDivisor) V2_PTR(nvnCommandBufferBindVertexStreamState)
    V2_PTR(nvnVertexAttribStateSetDefaults) V2_PTR(nvnVertexAttribStateSetFormat) V2_PTR(nvnVertexAttribStateSetStreamIndex)
    V2_PTR(nvnVertexAttribStateGetFormat) V2_PTR(nvnVertexAttribStateGetStreamIndex) V2_PTR(nvnCommandBufferBindVertexAttribState)
#undef V2_PTR
    return NULL;
}
#else
static void *nvn2_vertex_impl(const char *name) { (void)name; return NULL; }
#endif
