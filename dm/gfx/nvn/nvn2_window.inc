// SPDX-License-Identifier: GPL-3.0-or-later
#if DM_GAME_NVN2
#define W2(b) ((nvn2_win_builder *)(b))
#define CW2(b) ((const nvn2_win_builder *)(b))
static void n2_nvnWindowBuilderSetDefaults(NVNwindowBuilder *b) {
    // Native defaults preserve bytes 0..15. The remaining values are sRGB/HDR metadata.
    static const float color[12] = {.64f,.33f,.30f,.60f,.15f,.06f,.3127f,.329f,.03f,1000.f,200.f,60.f};
    memset((uint8_t *)b + 16, 0, 32); memcpy(W2(b)->color,color,sizeof color);
}
static void n2_nvnWindowBuilderSetDevice(NVNwindowBuilder *b, NVNdevice *d) { W2(b)->dev=d; }
static void n2_nvnWindowBuilderSetNativeWindow(NVNwindowBuilder *b, NVNnativeWindow w) { W2(b)->native=w; }
static void n2_nvnWindowBuilderSetTextures(NVNwindowBuilder *b, int n, NVNtexture *const *t) { W2(b)->ntex=n; W2(b)->textures=(void *)t; }
static void n2_nvnWindowBuilderSetPresentInterval(NVNwindowBuilder *b, int n) { W2(b)->interval_minus_one=(int32_t)((uint32_t)n-1); }
static void n2_nvnWindowBuilderSetNumActiveTextures(NVNwindowBuilder *b, int n) { W2(b)->active=n; }
static NVNdevice *n2_nvnWindowBuilderGetDevice(const NVNwindowBuilder *b) { return CW2(b)->dev; }
static NVNnativeWindow n2_nvnWindowBuilderGetNativeWindow(const NVNwindowBuilder *b) { return CW2(b)->native; }
static int n2_nvnWindowBuilderGetNumTextures(const NVNwindowBuilder *b) { return CW2(b)->ntex; }
static int n2_nvnWindowBuilderGetNumActiveTextures(const NVNwindowBuilder *b) { return CW2(b)->active; }
static int n2_nvnWindowBuilderGetPresentInterval(const NVNwindowBuilder *b) { return (int32_t)((uint32_t)CW2(b)->interval_minus_one+1); }
static NVNtexture *n2_nvnWindowBuilderGetTexture(const NVNwindowBuilder *b, int i) {
    return i>=0 && i<CW2(b)->ntex ? ((NVNtexture *const *)CW2(b)->textures)[i] : NULL;
}
static NVNboolean n2_nvnWindowInitialize(NVNwindow *w, const NVNwindowBuilder *b) {
    const nvn2_win_builder *s=CW2(b);
    if (s->ntex<2 || s->ntex>4 || !s->textures || s->mode ||
        (s->active && s->active!=s->ntex) || s->interval_minus_one < -1 || s->interval_minus_one > 4)
        die("[RED] NVN2 unsupported window configuration (textures=%d active=%d mode=%d interval=%d)",s->ntex,s->active,s->mode,s->interval_minus_one+1);
    win_builder_t decoded={.dev=s->dev,.native=s->native,.ntex=s->ntex,.interval=s->interval_minus_one+1};
    for (int i=0;i<s->ntex;i++) {
        decoded.tex[i]=((NVNtexture *const *)s->textures)[i];
        if (!decoded.tex[i] || CTX(decoded.tex[i])->magic!=MAGIC('T','E','X',0))
            die("[RED] NVN2 window references an uninitialized texture");
    }
    _Static_assert(sizeof(win_t)<=536,"NVN2 window object extent");
    return h_nvnWindowInitialize(w,(const NVNwindowBuilder *)&decoded);
}
static void n2_nvnWindowFinalize(NVNwindow *w) {
    // Textures are owned by the caller. Drain the host presentation jobs before invalidating the window.
    mtl_present_flush(); memset(w,0,sizeof(win_t));
}
static int n2_nvnWindowGetNumActiveTextures(const NVNwindow *w) { return ((const win_t *)w)->ntex; }
static void *nvn2_window_impl(const char *name) {
#define N2_WINDOW_PTR(n) if (!strcmp(name,#n)) return (void *)n2_##n;
    N2_WINDOW_PTR(nvnWindowBuilderSetDefaults) N2_WINDOW_PTR(nvnWindowBuilderSetDevice)
    N2_WINDOW_PTR(nvnWindowBuilderSetNativeWindow) N2_WINDOW_PTR(nvnWindowBuilderSetTextures)
    N2_WINDOW_PTR(nvnWindowBuilderSetPresentInterval) N2_WINDOW_PTR(nvnWindowBuilderSetNumActiveTextures)
    N2_WINDOW_PTR(nvnWindowBuilderGetDevice) N2_WINDOW_PTR(nvnWindowBuilderGetNativeWindow)
    N2_WINDOW_PTR(nvnWindowBuilderGetTexture) N2_WINDOW_PTR(nvnWindowBuilderGetNumTextures)
    N2_WINDOW_PTR(nvnWindowBuilderGetNumActiveTextures) N2_WINDOW_PTR(nvnWindowBuilderGetPresentInterval)
    N2_WINDOW_PTR(nvnWindowInitialize) N2_WINDOW_PTR(nvnWindowFinalize) N2_WINDOW_PTR(nvnWindowGetNumActiveTextures)
#undef N2_WINDOW_PTR
    return NULL;
}
#undef W2
#undef CW2
#else
static void *nvn2_window_impl(const char *name) { (void)name; return NULL; }
#endif
