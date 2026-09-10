/*
 * Salvia Round4G - MAME2003+ indexed-palette fast path for Xbox 360/Xenos.
 *
 * Integration: copy this file next to SDL_xboxvideo.c as
 *   libs/libSDLx360/SDL/src/video/xbox/SDL_xbox_mamepalette.inc
 * and apply patches/SDL_XBOX_ROUND4G_GPU_INDEXED.patch.
 *
 * The primary game texture remains D3DFMT_LIN_R5G6B5.  In this private mode
 * its 16 bits carry the original MAME palette index, not an RGB565 colour.
 * The pixel shader reconstructs those 16 bits from the exact R5G6B5 sample
 * and looks the final XRGB colour up in a 256x256 palette texture.
 *
 * This is intentionally enabled only for effect 0 (Nearest) in Round4G.
 * Bilinear/CRT/HQ/xBR shaders continue to use the normal CPU PALTO565 route,
 * because filtering palette indices before lookup would produce wrong colours.
 */

static LPDIRECT3DTEXTURE9    g_mame_index_palette_tex = NULL;
static LPDIRECT3DPIXELSHADER9 g_mame_index_palette_ps = NULL;
static int g_mame_index_palette_enabled = 0;
static int g_mame_index_palette_valid = 0;
static int g_mame_index_shader_ok = 0;
static int g_mame_index_texture_ok = 0;
static int g_mame_index_upload_ok = 0;
static const char* g_mame_index_reason = "not initialized";

static const char* g_strMameIndexPalettePS =
    "struct PS_IN { float2 TexCoord : TEXCOORD0; };\n"
    "sampler2D indexTex   : register(s0);\n"
    "sampler2D paletteTex : register(s1);\n"
    "float4 main(PS_IN In) : COLOR0\n"
    "{\n"
    "    float3 p = tex2D(indexTex, In.TexCoord).rgb;\n"
    "    float r5 = floor(p.r * 31.0 + 0.5);\n"
    "    float g6 = floor(p.g * 63.0 + 0.5);\n"
    "    float b5 = floor(p.b * 31.0 + 0.5);\n"
    "    float idx = r5 * 2048.0 + g6 * 32.0 + b5;\n"
    "    float py = floor(idx * (1.0 / 256.0));\n"
    "    float px = idx - py * 256.0;\n"
    "    float2 puv = (float2(px, py) + 0.5) * (1.0 / 256.0);\n"
    "    return tex2D(paletteTex, puv);\n"
    "}\n";

static int XBOX_MameIndexedEnsureResources(void)
{
    ID3DXBuffer* code = NULL;
    ID3DXBuffer* errs = NULL;
    HRESULT hr;

    if (!D3D_Device)
        return 0;

    if (!g_mame_index_palette_ps) {
        hr = D3DXCompileShader(g_strMameIndexPalettePS,
                              (UINT)strlen(g_strMameIndexPalettePS),
                              NULL, NULL, "main", "ps_3_0", 0,
                              &code, &errs, NULL);
        if (FAILED(hr) || !code) {
            g_mame_index_reason = "indexed shader compile failed";
            if (errs) errs->lpVtbl->Release(errs);
            return 0;
        }
        hr = IDirect3DDevice9_CreatePixelShader(
            D3D_Device,
            (DWORD*)code->lpVtbl->GetBufferPointer(code),
            &g_mame_index_palette_ps);
        code->lpVtbl->Release(code);
        if (errs) errs->lpVtbl->Release(errs);
        if (FAILED(hr) || !g_mame_index_palette_ps)
            return 0;
        g_mame_index_shader_ok = 1;
    }

    if (!g_mame_index_palette_tex) {
        hr = IDirect3DDevice9_CreateTexture(
            D3D_Device, 256, 256, 1, 0,
            D3DFMT_LIN_X8R8G8B8,
            D3DUSAGE_CPU_CACHED_MEMORY,
            (D3DTexture**)&g_mame_index_palette_tex, NULL);
        if (FAILED(hr) || !g_mame_index_palette_tex)
        {
            g_mame_index_reason = "palette texture creation failed";
            return 0;
        }
        g_mame_index_texture_ok = 1;
    }

    return 1;
}

/* Called by the Salvia frontend before it offers the private framebuffer. */
int SDL_XBOX_MameIndexedCanUse(void)
{
    /* Effect 0 is the stock Nearest path in this Salvia tree.  Other effects
       sample neighbouring texels and therefore must receive already-converted
       RGB, not palette indices. */
    if (g_current_effect != 0)
    {
        g_mame_index_reason = "sampler/effect is not POINT/Nearest";
        return 0;
    }
    if (!g_mame_index_palette_valid)
    {
        g_mame_index_reason = "palette has not been uploaded";
        return 0;
    }
    if (!XBOX_MameIndexedEnsureResources())
        return 0;
    g_mame_index_reason = "available";
    return 1;
}

/* Palette is XRGB8888/rgb_t.  dirty is MAME's 32-bit dirty bitfield. */
int SDL_XBOX_MameIndexedUploadPalette(const unsigned int* colors,
                                      const unsigned int* dirty,
                                      unsigned entries,
                                      int full_update)
{
    D3DLOCKED_RECT lr;
    HRESULT hr;
    unsigned i;
    unsigned int* row;

    if (!colors || entries == 0 || entries > 65536u)
        return 0;

    /* Xenon D3D9 is not thread-safe.  Use the same lock as Present so the
       watchdog SDL_Flip cannot collide with palette resource updates. */
    if (g_xboxFlipCSInit) EnterCriticalSection(&g_xboxFlipCS);

    if (!XBOX_MameIndexedEnsureResources()) {
        if (g_xboxFlipCSInit) LeaveCriticalSection(&g_xboxFlipCS);
        return 0;
    }

    hr = IDirect3DTexture9_LockRect(g_mame_index_palette_tex, 0, &lr, NULL, 0);
    if (FAILED(hr)) {
        g_mame_index_upload_ok = 0;
        g_mame_index_reason = "palette texture lock failed";
        if (g_xboxFlipCSInit) LeaveCriticalSection(&g_xboxFlipCS);
        return 0;
    }

    for (i = 0; i < entries; ++i) {
        if (!full_update && dirty && !(dirty[i >> 5] & (1u << (i & 31))))
            continue;
        row = (unsigned int*)((unsigned char*)lr.pBits + (i >> 8) * lr.Pitch);
        row[i & 255] = 0xFF000000u | (colors[i] & 0x00FFFFFFu);
    }

    IDirect3DTexture9_UnlockRect(g_mame_index_palette_tex, 0);
    g_mame_index_palette_valid = 1;
    g_mame_index_upload_ok = 1;
    g_mame_index_reason = "palette upload OK";
    if (g_xboxFlipCSInit) LeaveCriticalSection(&g_xboxFlipCS);
    return 1;
}

void SDL_XBOX_MameIndexedSetEnabled(int enabled)
{
    int next = enabled ? 1 : 0;

    if (next && !SDL_XBOX_MameIndexedCanUse())
        next = 0;

    if (g_mame_index_palette_enabled == next)
        return;

    g_mame_index_palette_enabled = next;
    g_mame_index_reason = next ? "active" : "disabled or fallback";

    /* When leaving the private shader, restore the complete normal effect
       state once.  This is transition-only, not a per-frame cost. */
    if (!next && D3D_Device)
        XBOX_SelectEffect(g_current_effect);
}

int SDL_XBOX_MameIndexedGetTelemetry(int* enabled, int* available,
                                     int* shader_ok, int* texture_ok,
                                     int* upload_ok, int* sampler_point,
                                     const char** reason)
{
    if (enabled) *enabled = g_mame_index_palette_enabled;
    if (available) *available = g_mame_index_palette_valid &&
                                g_current_effect == 0 &&
                                g_mame_index_shader_ok && g_mame_index_texture_ok;
    if (shader_ok) *shader_ok = g_mame_index_shader_ok;
    if (texture_ok) *texture_ok = g_mame_index_texture_ok;
    if (upload_ok) *upload_ok = g_mame_index_upload_ok;
    if (sampler_point) *sampler_point = (g_current_effect == 0);
    if (reason) *reason = g_mame_index_reason;
    return 1;
}

/* Called immediately before drawing the game quad.  No work in normal mode. */
static void XBOX_MameIndexedBindForMainQuad(void)
{
    if (!g_mame_index_palette_enabled ||
        !g_mame_index_palette_valid ||
        !g_mame_index_palette_ps ||
        !g_mame_index_palette_tex)
        return;

    IDirect3DDevice9_SetPixelShader(D3D_Device, g_mame_index_palette_ps);
    if (current_video && current_video->hidden && current_video->hidden->SDL_primary)
        IDirect3DDevice9_SetTexture(D3D_Device, 0,
            (D3DBaseTexture*)current_video->hidden->SDL_primary);
    IDirect3DDevice9_SetTexture(D3D_Device, 1,
        (D3DBaseTexture*)g_mame_index_palette_tex);

    /* Indices must never be interpolated. */
    IDirect3DDevice9_SetSamplerState(D3D_Device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(D3D_Device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(D3D_Device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(D3D_Device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    IDirect3DDevice9_SetSamplerState(D3D_Device, 1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(D3D_Device, 1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice9_SetSamplerState(D3D_Device, 1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(D3D_Device, 1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

static void XBOX_MameIndexedShutdown(void)
{
    g_mame_index_palette_enabled = 0;
    g_mame_index_palette_valid = 0;
    g_mame_index_shader_ok = 0;
    g_mame_index_texture_ok = 0;
    g_mame_index_upload_ok = 0;
    g_mame_index_reason = "shutdown";

    if (g_mame_index_palette_tex) {
        IDirect3DTexture9_Release(g_mame_index_palette_tex);
        g_mame_index_palette_tex = NULL;
    }
    if (g_mame_index_palette_ps) {
        IDirect3DPixelShader9_Release(g_mame_index_palette_ps);
        g_mame_index_palette_ps = NULL;
    }
}
