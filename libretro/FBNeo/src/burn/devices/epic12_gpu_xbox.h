// Xenos offscreen compositor for EPIC12. All D3D access shares SDL's Present
// lock. A batch ends with one readback; textures are cached across batches.
#ifndef FBNEO_EPIC12_GPU_XBOX_H
#define FBNEO_EPIC12_GPU_XBOX_H
#include <xtl.h>
#include <d3dx9.h>
#include <xgraphics.h>
#include "epic12_gpu_untile.h"
#include "epic12_gpu_cache.h"
#include "epic12_gpu_alpha.h"
#include "epic12_gpu_tile.h"
#include "epic12_gpu_tile_alpha.h"

extern "C" void *SDL_XBOX_AcquireCoreGpu(unsigned *generation);
extern "C" void SDL_XBOX_ReleaseCoreGpu(void);
extern "C" void SDL_XBOX_SetCoreGpuDiagnostics(void (*callback)(void));
extern "C" void SDL_XBOX_SetCoreGpuAsync(int enabled);
extern "C" unsigned SDL_XBOX_GetCoreGpuFrameInfo(unsigned *presentSequence);
extern "C" void SDL_XBOX_GetCoreGpuPresentStats(unsigned stats[8]);
extern "C" void SDL_XBOX_GetCoreGpuRecoveryStats(unsigned stats[6]);

static D3DDevice *epic12_xenos_device;
static D3DTexture *epic12_xenos_atlas, *epic12_xenos_input, *epic12_xenos_result;
static D3DSurface *epic12_xenos_target;
static D3DVertexShader *epic12_xenos_vs, *epic12_xenos_uniform_vs;
static D3DPixelShader *epic12_xenos_ps, *epic12_xenos_uniform_ps, *epic12_xenos_copy_ps;
static D3DPixelShader *epic12_xenos_fast_ps, *epic12_xenos_feedback_ps;
static D3DVertexDeclaration *epic12_xenos_decl, *epic12_xenos_uniform_decl;
static D3DStateBlock *epic12_xenos_state;
static unsigned epic12_xenos_generation, epic12_xenos_batches;
static UINT64 epic12_xenos_commands, epic12_xenos_pixels, epic12_xenos_readback;
static UINT64 epic12_xenos_draws, epic12_xenos_feedbacks, epic12_xenos_feedback_pixels;
static Epic12GpuVertex epic12_xenos_vertices[EPIC12_GPU_DRAW_RECTS * 3];
struct Epic12GpuUniformVertex { float x, y, u, v; };
static Epic12GpuUniformVertex epic12_xenos_uniform_vertices[EPIC12_GPU_DRAW_RECTS * 3];
static volatile LONG epic12_xenos_notice;
static int epic12_xenos_status, epic12_xenos_error;
static bool epic12_xenos_attributes;
static bool epic12_xenos_specialized, epic12_xenos_fast_transfer;
static bool epic12_xenos_alpha_trim, epic12_xenos_snapshot_reuse, epic12_xenos_tiled;
static bool epic12_xenos_reorder;
static UINT64 epic12_xenos_reordered;
static Epic12GpuAlphaPage epic12_xenos_alpha[256];
static Epic12GpuCommand epic12_xenos_cropped[1024];
static UINT64 epic12_xenos_raster_commands, epic12_xenos_raster_pixels;

// Randomly sample about one in 64 batches to avoid locking to the same
// batch position in every frame. Lock/readback waits include prior GPU work;
// these are not GPU timestamp queries or an estimate of the game's FPS.
struct Epic12GpuTiming { LARGE_INTEGER stamp[8]; };
static UINT64 epic12_xenos_ticks[7], epic12_xenos_peak_ticks;
static unsigned epic12_xenos_samples, epic12_xenos_last_report;
struct Epic12GpuTimingBucket {
 UINT64 ticks[7], peak;
 unsigned samples, batches;
};
// Sync/async crossed with first-after-Present/later-in-frame.
static Epic12GpuTimingBucket epic12_xenos_timing_buckets[4];
static unsigned epic12_xenos_sample_rng=0x4713bc29u;
static unsigned epic12_xenos_present_sequence, epic12_xenos_present_mode;
static bool epic12_xenos_present_known;
static bool epic12_xenos_sample_batch()
{
 unsigned x=epic12_xenos_sample_rng;
 x^=x<<13; x^=x>>17; x^=x<<5; epic12_xenos_sample_rng=x;
 return !(x&63);
}

// The worker only publishes an integer. Libretro consumes it on the emulation
// thread after BurnDrvFrame; frontend UI callbacks never run on the GPU worker.
static void epic12_xenos_set_status(int status)
{
	if (epic12_xenos_status == status) return;
	if(status>=2 && status<=6) SDL_XBOX_SetCoreGpuAsync(0);
	epic12_xenos_status = status;
	InterlockedExchange(&epic12_xenos_notice, status);
}

const char *epic12_gpu_take_message()
{
	switch (InterlockedExchange(&epic12_xenos_notice, 0)) {
		case 1: return "CV1000 GPU: active (sprite batching)";
		case 2: return "CV1000 GPU: off - CPU rendering";
		case 3: return "CV1000 GPU: CPU fallback - initialization failed";
		case 4: return "CV1000 GPU: CPU fallback - pixel self-test failed";
		case 5: return "CV1000 GPU: CPU fallback - display unavailable";
		case 6: return "CV1000 GPU: CPU fallback - transfer failed";
		case 7: return "CV1000 GPU: active (compatible batching)";
	}
	return NULL;
}
static bool epic12_xenos_failed, epic12_xenos_validated;
static Epic12GpuPageCache epic12_xenos_cache;
static unsigned epic12_gpu_batch_page_capacity()
{
	// Keep the proven cross-batch 256-slot cache, but retire the unproven
	// 256-page batch experiment after the user's frame-rate regression.
	return epic12_xenos_cache.capacity < 128 ? epic12_xenos_cache.capacity : 128;
}
static int epic12_xenos_atlas_height = 1024;
static UINT64 epic12_xenos_cache_hits, epic12_xenos_upload_pages, epic12_xenos_input_pixels;

// Positions are pixel boundaries in a 512x512 target. D3D9 pixel centres
// lie on integers; the half-pixel adjustment is explicit in this shader.
static const char epic12_xenos_shader[] =
"struct V { float2 p:POSITION; float2 uv:TEXCOORD0; float4 tint:TEXCOORD1; float4 mode:TEXCOORD2; };\n"
"struct U { float2 p:POSITION; float2 uv:TEXCOORD0; };\n"
"struct P { float4 p:POSITION; float2 uv:TEXCOORD0; float2 dst:TEXCOORD1; float4 tint:TEXCOORD2; float4 mode:TEXCOORD3; };\n"
"float4 atlasScale:register(c0); // vertex shader constants are separate from pixel constants\n"
"P vs_main(V v) { P o; o.p=float4((v.p.x-0.5)/256.0-1.0,1.0-(v.p.y-0.5)/256.0,0,1); o.uv=v.uv*atlasScale.xy; o.dst=v.p/512.0; o.tint=v.tint; o.mode=v.mode; return o; }\n"
"P vs_uniform(U v) { P o; o.p=float4((v.p.x-0.5)/256.0-1.0,1.0-(v.p.y-0.5)/256.0,0,1); o.uv=v.uv; o.dst=v.p/512.0; o.tint=0; o.mode=0; return o; }\n"
"sampler2D source:register(s0); sampler2D destination:register(s1);\n"
"float4 uniformTint:register(c0); // tint RGB (0..63), source alpha (0..31)\n"
"float4 uniformMode:register(c1); // transparency, blend, destination mode, destination alpha\n"
"float4 copy_main(P p):COLOR0 { return tex2D(source,p.uv); }\n"
"float4 shade(P p, float4 tintAlpha, float4 mode) {\n"
" float4 raw=tex2D(source,p.uv);\n"
" if (mode.x>0.5) clip(raw.a-0.0625);\n"
" float3 s=floor(raw.rgb*(255.0/8.0)+0.001);\n"
" s=min(31.0,floor(s*tintAlpha.rgb/31.0+0.0001));\n"
" float3 outc=s;\n"
" if(mode.y>0.5) {\n"
"  outc=floor(s*tintAlpha.a/31.0+0.0001);\n"
"  if(mode.z>0.5 || (mode.w>0.5 && mode.w<30.5)) {\n"
"   float3 d=floor(tex2D(destination,p.dst).rgb*(255.0/8.0)+0.001);\n"
"   float3 factor=mode.w;\n"
"   if(mode.z>0.5) factor=s; // mode 4 was normalized on the CPU\n"
"   outc=min(31.0,outc+floor(d*factor/31.0+0.0001));\n"
"  }\n"
" }\n"
" return float4(outc*(8.0/255.0),raw.a);\n"
"}\n"
"float4 ps_main(P p):COLOR0 { return shade(p,floor(p.tint+0.5),floor(p.mode+0.5)); }\n"
"float4 ps_uniform(P p):COLOR0 { return shade(p,uniformTint,uniformMode); }\n"
// Keep destination fetch/branch instructions out of the common plain and
// additive shaders. The CPU planner already separates feedback groups.
"float4 shade_fast(P p, float4 tintAlpha, float4 mode) {\n"
" float4 raw=tex2D(source,p.uv);\n"
" if(mode.x>0.5) clip(raw.a-0.0625);\n"
" float3 s=floor(raw.rgb*(255.0/8.0)+0.001);\n"
" s=min(31.0,floor(s*tintAlpha.rgb/31.0+0.0001));\n"
" float3 outc=s;\n"
" if(mode.y>0.5) outc=floor(s*tintAlpha.a/31.0+0.0001);\n"
" return float4(outc*(8.0/255.0),raw.a);\n"
"}\n"
"float4 shade_feedback(P p, float4 tintAlpha, float4 mode) {\n"
" float4 raw=tex2D(source,p.uv);\n"
" if(mode.x>0.5) clip(raw.a-0.0625);\n"
" float3 s=floor(raw.rgb*(255.0/8.0)+0.001);\n"
" s=min(31.0,floor(s*tintAlpha.rgb/31.0+0.0001));\n"
" float3 outc=floor(s*tintAlpha.a/31.0+0.0001);\n"
" float3 d=floor(tex2D(destination,p.dst).rgb*(255.0/8.0)+0.001);\n"
" float3 factor=mode.w;\n"
" if(mode.z>0.5) factor=s;\n"
" outc=min(31.0,outc+floor(d*factor/31.0+0.0001));\n"
" return float4(outc*(8.0/255.0),raw.a);\n"
"}\n"
"float4 ps_fast(P p):COLOR0 { return shade_fast(p,floor(p.tint+0.5),floor(p.mode+0.5)); }\n"
"float4 ps_feedback(P p):COLOR0 { return shade_feedback(p,floor(p.tint+0.5),floor(p.mode+0.5)); }\n";

static void epic12_xenos_log(const char *message)
{
	// A separate file works even when frontend logging is disabled.
	FILE *f = fopen("game:\\cv1000-gpu.log", "a");
	// The libretro VFS maps append to UPDATE_EXISTING, which cannot create
	// the first log file. Retry in create mode on its first use.
	if (!f) f = fopen("game:\\cv1000-gpu.log", "w");
	if (f) { fprintf(f,"CV1000 GPU: %s (batches=%u)\n",message,epic12_xenos_batches); fclose(f); }
	bprintf(0,_T("CV1000 GPU: %hs\n"),message);
}

static void epic12_xenos_report()
{
	if (epic12_xenos_batches == epic12_xenos_last_report) return;
	char message[512];
	sprintf(message,"session renderer=%s commands=%I64u pixels=%I64u readback_pixels=%I64u",
		epic12_xenos_attributes?"sprite-attributes":"uniform-compatible",
		epic12_xenos_commands,epic12_xenos_pixels,epic12_xenos_readback);
	epic12_xenos_log(message);
	sprintf(message,"pipeline shaders=%s transfer=%s alpha_masks=%s page_upload=%s",
		epic12_xenos_attributes?(epic12_xenos_specialized?"split":"unified"):"uniform",
		epic12_xenos_fast_transfer?"vmx-fused":"sdk-compatible",epic12_alpha_vector_enabled?"vmx-bitpack":"scalar",
		epic12_xenos_tiled && epic12_tile_alpha_enabled && epic12_alpha_vector_enabled && epic12_xenos_alpha_trim?"fused-alpha":"separate");
	epic12_xenos_log(message);
	sprintf(message,"work alpha=%s snapshots=%s atlas_layout=%s raster_commands=%I64u raster_pixels=%I64u",
		epic12_xenos_alpha_trim?"cropped":"full",epic12_xenos_snapshot_reuse?"lookahead":"adjacent",
		epic12_xenos_tiled?"tiled":"linear",epic12_xenos_raster_commands,epic12_xenos_raster_pixels);
	epic12_xenos_log(message);
	sprintf(message,"ordering=%s moved_commands=%I64u",
		epic12_xenos_attributes && epic12_xenos_reorder?"disjoint-lookahead8":"original",epic12_xenos_reordered);
	epic12_xenos_log(message);
	sprintf(message,"submission draws=%I64u feedback_resolves=%I64u feedback_pixels=%I64u",
		epic12_xenos_draws,epic12_xenos_feedbacks,epic12_xenos_feedback_pixels);
	epic12_xenos_log(message);
	sprintf(message,"atlas slots=%d batch_page_limit=%u hits=%I64u upload_pages=%I64u upload_bytes=%I64u input_pixels=%I64u",
		epic12_xenos_cache.capacity,epic12_gpu_batch_page_capacity(),epic12_xenos_cache_hits,epic12_xenos_upload_pages,
		epic12_xenos_upload_pages*65536,epic12_xenos_input_pixels);
	epic12_xenos_log(message);
	LARGE_INTEGER frequency;
	sprintf(message,"flushes end=%u source_pages=%u region=%u source_dependency=%u command_limit=%u cpu_upload=%u cpu_draw=%u other=%u small_cpu=%u gpu_fallback=%u",
		epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_END_LIST],
		epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_SOURCE_PAGES],
		epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_REGION],
		epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_SOURCE_DEPENDENCY],
		epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_COMMAND_LIMIT],
		epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_CPU_UPLOAD],
		epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_CPU_DRAW],
		epic12_gpu_flush_reasons[EPIC12_GPU_FLUSH_OTHER],
		epic12_gpu_flush_small_cpu,epic12_gpu_flush_render_fallback);
	epic12_xenos_log(message);
	if (epic12_xenos_samples && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
		double ms=1000.0/((double)frequency.QuadPart*epic12_xenos_samples);
		sprintf(message,"sampled_batch_cpu_ms samples=%u lock=%.3f upload=%.3f alpha_plan=%.3f submit=%.3f readback_wait=%.3f untile_mask=%.3f peak_total=%.3f",
			epic12_xenos_samples,epic12_xenos_ticks[0]*ms,(epic12_xenos_ticks[1]+epic12_xenos_ticks[3])*ms,
			epic12_xenos_ticks[2]*ms,epic12_xenos_ticks[4]*ms,epic12_xenos_ticks[5]*ms,epic12_xenos_ticks[6]*ms,
			(double)epic12_xenos_peak_ticks*1000.0/(double)frequency.QuadPart);
		epic12_xenos_log(message);
	}
	if (thready.diag_wait_samples && QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
		double wait_ms=1000.0/((double)frequency.QuadPart*thready.diag_wait_samples);
		sprintf(message,"worker_wait sampling=random-1-in-64 jobs=%u waits=%u samples=%u avg_ms=%.3f peak_ms=%.3f",
			thready.diag_jobs,thready.diag_wait_calls,thready.diag_wait_samples,
			thready.diag_wait_ticks*wait_ms,(double)thready.diag_wait_peak*1000.0/(double)frequency.QuadPart);
		epic12_xenos_log(message);
	}
	sprintf(message,"presentation=%s sampling=random-1-in-64",
        epic12_xenos_present_mode==1?"async-vsync":(epic12_xenos_present_mode==2?"sync-fallback":"normal"));
    epic12_xenos_log(message);
	unsigned presentStats[8];
	SDL_XBOX_GetCoreGpuPresentStats(presentStats);
	sprintf(message,"present_state requested=%u active=%u failed=%u retained=%u sequence=%u activations=%u fallbacks=%u timeouts=%u",
		presentStats[0],presentStats[1],presentStats[2],presentStats[3],
		presentStats[4],presentStats[5],presentStats[6],presentStats[7]);
	epic12_xenos_log(message);
	unsigned recoveryStats[6];
	SDL_XBOX_GetCoreGpuRecoveryStats(recoveryStats);
	sprintf(message,"present_recovery reason=%u forced=%u attempts=%u last_epoch=%u ui_state=%u listener=%u",
		recoveryStats[0],recoveryStats[1],recoveryStats[2],recoveryStats[3],recoveryStats[4],recoveryStats[5]);
	epic12_xenos_log(message);
    if(QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0) {
        for(int i=0;i<4;++i) {
            const Epic12GpuTimingBucket &bucket=epic12_xenos_timing_buckets[i];
            if(!bucket.batches) continue;
            double ms=bucket.samples?1000.0/((double)frequency.QuadPart*bucket.samples):0;
            sprintf(message,"batch_timing mode=%s position=%s batches=%u samples=%u lock=%.3f source_upload=%.3f plan=%.3f input_upload=%.3f submit=%.3f readback_wait=%.3f untile_mask=%.3f peak_total=%.3f",
                i<2?"normal":"async",(i&1)?"later":"after-present",bucket.batches,bucket.samples,
                bucket.ticks[0]*ms,bucket.ticks[1]*ms,bucket.ticks[2]*ms,bucket.ticks[3]*ms,
                bucket.ticks[4]*ms,bucket.ticks[5]*ms,bucket.ticks[6]*ms,
                (double)bucket.peak*1000.0/(double)frequency.QuadPart);
            epic12_xenos_log(message);
        }
    }
	epic12_xenos_last_report=epic12_xenos_batches;
}

// Registered/unregistered on the emulation thread. Pause callbacks run there
// too. Join before touching counters, without holding SDL's GPU lock.
static void epic12_xenos_pause_diagnostics()
{
	thready.notify_wait();
	// This is independent of successful GPU batches, including GPU-Off runs.
	cv1k_review_report(epic12_xenos_log);
	epic12_xenos_report();
}

static void epic12_gpu_invalidate(const rectangle &r)
{
	epic12_xenos_cache.invalidate(r.min_x,r.min_y,r.max_x,r.max_y);
}

static void epic12_gpu_reset()
{
	epic12_xenos_cache.clear();
}

static void epic12_xenos_release()
{
#define EPIC12_RELEASE(p) if(p) { (p)->Release(); (p)=NULL; }
	EPIC12_RELEASE(epic12_xenos_state);
	EPIC12_RELEASE(epic12_xenos_decl);
	EPIC12_RELEASE(epic12_xenos_uniform_decl);
	EPIC12_RELEASE(epic12_xenos_vs);
	EPIC12_RELEASE(epic12_xenos_uniform_vs);
	EPIC12_RELEASE(epic12_xenos_ps);
	EPIC12_RELEASE(epic12_xenos_fast_ps);
	EPIC12_RELEASE(epic12_xenos_feedback_ps);
	EPIC12_RELEASE(epic12_xenos_uniform_ps);
	EPIC12_RELEASE(epic12_xenos_copy_ps);
	EPIC12_RELEASE(epic12_xenos_atlas);
	EPIC12_RELEASE(epic12_xenos_input);
	EPIC12_RELEASE(epic12_xenos_result);
	EPIC12_RELEASE(epic12_xenos_target);
#undef EPIC12_RELEASE
	epic12_xenos_validated=false;
	epic12_gpu_reset();
}

static void epic12_gpu_exit()
{
	SDL_XBOX_SetCoreGpuDiagnostics(NULL);
	SDL_XBOX_SetCoreGpuAsync(0);
	epic12_xenos_report();
	unsigned generation;
	D3DDevice *d=(D3DDevice*)SDL_XBOX_AcquireCoreGpu(&generation);
	if (d) {
		d->BlockUntilIdle();
		epic12_xenos_release();
		SDL_XBOX_ReleaseCoreGpu();
	}
	epic12_xenos_device=NULL;
	epic12_xenos_failed=false;
	epic12_xenos_batches=0;
	epic12_xenos_commands=epic12_xenos_pixels=epic12_xenos_readback=0;
	epic12_xenos_draws=epic12_xenos_feedbacks=epic12_xenos_feedback_pixels=0;
	epic12_xenos_cache_hits=epic12_xenos_upload_pages=epic12_xenos_input_pixels=0;
	epic12_xenos_raster_commands=epic12_xenos_raster_pixels=0; epic12_xenos_reordered=0;
	memset(epic12_xenos_ticks,0,sizeof(epic12_xenos_ticks));
	epic12_xenos_peak_ticks=0; epic12_xenos_samples=epic12_xenos_last_report=0;
    memset(epic12_xenos_timing_buckets,0,sizeof(epic12_xenos_timing_buckets));
    epic12_xenos_sample_rng=0x4713bc29u; epic12_xenos_present_known=false;
    epic12_xenos_present_mode=epic12_xenos_present_sequence=0;
	epic12_xenos_status=epic12_xenos_error=0;
	InterlockedExchange(&epic12_xenos_notice,0);
}

static bool epic12_xenos_compile(const char *entry, const char *profile, ID3DXBuffer **code)
{
	ID3DXBuffer *error=NULL;
	HRESULT hr=D3DXCompileShader(epic12_xenos_shader,(UINT)strlen(epic12_xenos_shader),NULL,NULL,
		entry,profile,0,code,&error,NULL);
	if (error) { if (FAILED(hr)) epic12_xenos_log((const char*)error->GetBufferPointer()); error->Release(); }
	return SUCCEEDED(hr) && *code;
}

static bool epic12_xenos_create_pixel_shader(const char *entry, D3DPixelShader **shader)
{
	ID3DXBuffer *code=NULL;
	if(!epic12_xenos_compile(entry,"ps_3_0",&code)) return false;
	HRESULT hr=epic12_xenos_device->CreatePixelShader((DWORD*)code->GetBufferPointer(),shader);
	code->Release();
	return SUCCEEDED(hr);
}

// Only source-cache capacity changes: each batch still uses at most 128 pages.
// Allocate after mandatory GPU resources; low-memory machines retain 128 slots.
static bool epic12_xenos_create_atlas(int capacity)
{
	if(epic12_xenos_atlas) { epic12_xenos_atlas->Release(); epic12_xenos_atlas=NULL; }
	epic12_xenos_cache.reset(capacity);
	epic12_xenos_atlas_height=capacity*8;
	return SUCCEEDED(epic12_xenos_device->CreateTexture(2048,epic12_xenos_atlas_height,1,0,
		(epic12_xenos_tiled?D3DFMT_A8R8G8B8:D3DFMT_LIN_A8R8G8B8),D3DPOOL_DEFAULT,&epic12_xenos_atlas,NULL));
}

static bool epic12_xenos_create()
{
	D3DDevice *d=epic12_xenos_device;
	ID3DXBuffer *code=NULL;
	HRESULT hr;
	if (!epic12_xenos_compile("vs_main","vs_3_0",&code)) return false;
	hr=d->CreateVertexShader((DWORD*)code->GetBufferPointer(),&epic12_xenos_vs); code->Release(); code=NULL;
	if (FAILED(hr)) return false;
	if (!epic12_xenos_compile("vs_uniform","vs_3_0",&code)) return false;
	hr=d->CreateVertexShader((DWORD*)code->GetBufferPointer(),&epic12_xenos_uniform_vs); code->Release(); code=NULL;
	if (FAILED(hr)) return false;
	if (!epic12_xenos_compile("ps_uniform","ps_3_0",&code)) return false;
	hr=d->CreatePixelShader((DWORD*)code->GetBufferPointer(),&epic12_xenos_uniform_ps); code->Release(); code=NULL;
	if (FAILED(hr)) return false;
	// Keep the already tested uniform path if the new shader cannot be used.
	epic12_xenos_attributes=false;
	if (epic12_xenos_compile("ps_main","ps_3_0",&code)) {
		hr=d->CreatePixelShader((DWORD*)code->GetBufferPointer(),&epic12_xenos_ps); code->Release(); code=NULL;
		epic12_xenos_attributes=SUCCEEDED(hr);
	}
	epic12_xenos_specialized=epic12_xenos_attributes &&
		epic12_xenos_create_pixel_shader("ps_fast",&epic12_xenos_fast_ps) &&
		epic12_xenos_create_pixel_shader("ps_feedback",&epic12_xenos_feedback_ps);
	if (!epic12_xenos_compile("copy_main","ps_3_0",&code)) return false;
	hr=d->CreatePixelShader((DWORD*)code->GetBufferPointer(),&epic12_xenos_copy_ps); code->Release();
	if (FAILED(hr)) return false;
	D3DVERTEXELEMENT9 elements[]={
		{0,0,D3DDECLTYPE_USHORT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
		{0,4,D3DDECLTYPE_USHORT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
		{0,8,D3DDECLTYPE_UBYTE4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
		{0,12,D3DDECLTYPE_UBYTE4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,2},D3DDECL_END()};
	if (FAILED(d->CreateVertexDeclaration(elements,&epic12_xenos_decl))) return false;
	D3DVERTEXELEMENT9 uniformElements[]={
		{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
		{0,8,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
	if (FAILED(d->CreateVertexDeclaration(uniformElements,&epic12_xenos_uniform_decl))) return false;
	if (FAILED(d->CreateStateBlock(D3DSBT_ALL,&epic12_xenos_state))) return false;
	if (FAILED(d->CreateTexture(512,512,1,0,D3DFMT_LIN_A8R8G8B8,D3DPOOL_DEFAULT,&epic12_xenos_input,NULL))) return false;
	// Xenos Resolve requires a tiled destination. Readonly LockRect provides
	// synchronization/cache coherency; untile only the batch's output region.
	if (FAILED(d->CreateTexture(512,512,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&epic12_xenos_result,NULL))) return false;
	// Present has completed under the shared lock. Borrow EDRAM base 0;
	// SDL clears/redraws its backbuffer at its next Present. This avoids
	// depending on spare EDRAM at higher output resolutions.
	D3DSURFACE_PARAMETERS surface={0};
	if (FAILED(d->CreateRenderTarget(512,512,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&epic12_xenos_target,&surface))) return false;
	// Validate real VMX loads/stores and all 512x512 tile addresses once,
	// including unaligned destination rows, before using the fused transfer.
	epic12_xenos_fast_transfer=epic12_gpu_untile_selftest();
	epic12_xenos_log(epic12_xenos_fast_transfer?
		"fused VMX transfer self-test passed":"fused transfer unavailable; using SDK transfer");
	epic12_xenos_alpha_trim=true; epic12_xenos_snapshot_reuse=true; epic12_xenos_reorder=true;
	bool alphaVector=epic12_gpu_alpha_vector_selftest();
	epic12_xenos_log(alphaVector?"VMX alpha mask self-test passed":"VMX alpha mask unavailable; using scalar masks");
	epic12_xenos_tiled=epic12_gpu_tile_selftest();
	epic12_xenos_log(epic12_xenos_tiled?"tiled atlas layout self-test passed":"tiled atlas unavailable; using linear layout");
	bool fusedAlpha=epic12_xenos_tiled && alphaVector && epic12_gpu_tile_alpha_selftest();
	epic12_xenos_log(fusedAlpha?"fused atlas/alpha self-test passed":"fused atlas/alpha unavailable; using separate passes");
	if(!epic12_xenos_create_atlas(256) && !epic12_xenos_create_atlas(128)) {
		if(!epic12_xenos_tiled) return false;
		epic12_xenos_tiled=false;
		if(!epic12_xenos_create_atlas(256) && !epic12_xenos_create_atlas(128)) return false;
	}
	return true;
}

static void epic12_xenos_quad(float x,float y,float w,float h,float u0,float v0,float u1,float v1)
{
	Epic12GpuUniformVertex v[3]={{x,y,u0,v0},{x+w,y,u1,v0},{x,y+h,u0,v1}};
	epic12_xenos_device->DrawPrimitiveUP(D3DPT_RECTLIST,1,v,sizeof(v[0]));
}

static void epic12_xenos_draw(int vertices)
{
	if (!vertices) return;
	if(epic12_xenos_attributes) {
		epic12_xenos_device->DrawPrimitiveUP(D3DPT_RECTLIST,vertices/3,
			epic12_xenos_vertices,sizeof(Epic12GpuVertex));
	} else {
		for(int i=0;i<vertices;++i) {
			const Epic12GpuVertex &v=epic12_xenos_vertices[i];
			Epic12GpuUniformVertex &u=epic12_xenos_uniform_vertices[i];
			u.x=(float)v.x; u.y=(float)v.y; u.u=v.u/2048.0f; u.v=v.v/(float)epic12_xenos_atlas_height;
		}
		epic12_xenos_device->DrawPrimitiveUP(D3DPT_RECTLIST,vertices/3,
			epic12_xenos_uniform_vertices,sizeof(Epic12GpuUniformVertex));
	}
	++epic12_xenos_draws;
}

static void epic12_xenos_resolve(int x,int y,int w,int h)
{
	// Resolve requires 8-pixel alignment. Keep source/destination origins
	// equal so feedback can sample the same screen coordinates. In particular,
	// don't copy the whole 512x512 surface for each small translucent sprite.
	D3DRECT rect={x&~7,y&~7,(x+w+7)&~7,(y+h+7)&~7};
	D3DPOINT point={rect.x1,rect.y1};
	epic12_xenos_device->Resolve(D3DRESOLVE_RENDERTARGET0,&rect,epic12_xenos_result,&point,0,0,NULL,0,0,NULL);
}

static bool epic12_xenos_run(UINT32 *vram,const Epic12GpuCommand *cmd,int count,const rectangle &originalBounds,
	Epic12GpuTiming *timing=NULL)
{
	D3DDevice *d=epic12_xenos_device;
	bool needed[2048]={false};
	int slots[2048];
	int pages=0;
	for (int i=0;i<count;++i) {
		const Epic12GpuCommand &c=cmd[i];
		for (int y=c.sy/128;y<=(c.sy+c.h-1)/128;++y)
			for (int x=c.sx/128;x<=(c.sx+c.w-1)/128;++x) {
				int p=y*64+x;
				if (!needed[p]) { needed[p]=true; if (++pages>(int)epic12_gpu_batch_page_capacity()) return false; }
			}
	}
	epic12_xenos_cache_hits+=epic12_xenos_cache.begin_batch(needed,slots);
	D3DLOCKED_RECT lr;
	bool locked=false;
	for (int p=0;p<2048;++p) if (needed[p] && slots[p]<0) {
		int next=epic12_xenos_cache.allocate(p);
		if(next<0) { if(locked) epic12_xenos_atlas->UnlockRect(0); return false; }
		if (!locked) {
			if (FAILED(epic12_xenos_atlas->LockRect(0,&lr,NULL,0))) return false;
			locked=true;
		}
		int sx=(p&63)*128, sy=(p>>6)*128;
		int ax=(next&15)*128, ay=(next>>4)*128;
		const UINT32 *source=vram+sy*8192+sx;
		if(epic12_xenos_tiled) {
			if(!epic12_xenos_alpha_trim || !epic12_gpu_tile_alpha_page(lr.pBits,source,next,epic12_xenos_alpha[next])) {
				epic12_gpu_tile_page(lr.pBits,source,next);
				if(epic12_xenos_alpha_trim) epic12_xenos_alpha[next].build(source,8192);
			}
		} else for(int y=0;y<128;++y) {
			memcpy((BYTE*)lr.pBits+(ay+y)*lr.Pitch+ax*4,source+y*8192,128*4);
			if(epic12_xenos_alpha_trim) epic12_xenos_alpha[next].build_row(y,source+y*8192);
		}
		slots[p]=next; ++epic12_xenos_upload_pages;
	}
	if (locked) epic12_xenos_atlas->UnlockRect(0);
	if(timing) QueryPerformanceCounter(&timing->stamp[2]);
	rectangle bounds=originalBounds;
	if(epic12_xenos_alpha_trim) {
		int cropped=epic12_gpu_alpha_crop_batch(cmd,count,epic12_xenos_cropped,
			epic12_xenos_alpha,slots,epic12_xenos_cache.capacity,bounds);
		if(cropped<0) return false;
		cmd=epic12_xenos_cropped; count=cropped;
	}
	if(epic12_xenos_attributes && epic12_xenos_reorder) {
		epic12_xenos_reordered+=epic12_gpu_reorder(cmd,count,epic12_xenos_cropped);
		cmd=epic12_xenos_cropped;
	}
	epic12_xenos_raster_commands+=count;
	for(int i=0;i<count;++i) epic12_xenos_raster_pixels+=cmd[i].w*cmd[i].h;
	if(timing) QueryPerformanceCounter(&timing->stamp[3]);
	if(!count) {
		if(timing) for(int i=4;i<8;++i) timing->stamp[i]=timing->stamp[3];
		return true;
	}
	int width=bounds.max_x-bounds.min_x+1, height=bounds.max_y-bounds.min_y+1;
	if (FAILED(epic12_xenos_input->LockRect(0,&lr,NULL,0))) return false;
	for (int y=0;y<height;++y) memcpy((BYTE*)lr.pBits+y*lr.Pitch,vram+(bounds.min_y+y)*8192+bounds.min_x,width*4);
	epic12_xenos_input->UnlockRect(0);
	epic12_xenos_input_pixels+=width*height;
	if(timing) QueryPerformanceCounter(&timing->stamp[4]);

	D3DSurface *oldTarget=NULL,*oldDepth=NULL;
	d->GetRenderTarget(0,&oldTarget); d->GetDepthStencilSurface(&oldDepth);
	epic12_xenos_state->Capture();
	float atlasScale[4]={1.0f/2048.0f,1.0f/(float)epic12_xenos_atlas_height,0,0};
	d->SetVertexShaderConstantF(0,atlasScale,1);
	d->SetDepthStencilSurface(NULL);
	d->SetRenderTarget(0,epic12_xenos_target);
	D3DVIEWPORT9 vp={0,0,512,512,0.0f,1.0f}; d->SetViewport(&vp);
	d->SetRenderState(D3DRS_VIEWPORTENABLE,TRUE);
	d->SetRenderState(D3DRS_HALFPIXELOFFSET,FALSE);
	d->SetRenderState(D3DRS_ZENABLE,FALSE); d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
	d->SetRenderState(D3DRS_STENCILENABLE,FALSE); d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
	d->SetRenderState(D3DRS_ALPHATOMASKENABLE,FALSE); d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
	d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE); d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
	d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
	d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
	d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff);
	d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
	d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);
	d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);
	d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_ZERO);
	d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
	d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE); d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
	d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
	d->SetVertexDeclaration(epic12_xenos_uniform_decl); d->SetVertexShader(epic12_xenos_uniform_vs);
	for (int s=0;s<2;++s) {
		d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT); d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
		d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
		d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP); d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
	}
	d->SetTexture(0,epic12_xenos_input); d->SetTexture(1,NULL);
	d->SetPixelShader(epic12_xenos_copy_ps);
	epic12_xenos_quad(0,0,(float)width,(float)height,0,0,width/512.0f,height/512.0f);
	if(epic12_xenos_attributes) {
		d->SetVertexDeclaration(epic12_xenos_decl); d->SetVertexShader(epic12_xenos_vs);
	}
	d->SetTexture(0,epic12_xenos_atlas);
	D3DPixelShader *activePs=epic12_xenos_attributes?epic12_xenos_ps:epic12_xenos_uniform_ps;
	d->SetPixelShader(activePs);
	int snapshotEnd=0;
	for (int first=0;first<count;) {
		const Epic12GpuCommand &c=cmd[first];
		rectangle snapshot;
		int end=epic12_gpu_group_end(cmd,first,count,bounds,snapshot,epic12_xenos_attributes);
		bool feedback=epic12_gpu_feedback(c);
		if (epic12_xenos_attributes && epic12_xenos_specialized) {
			D3DPixelShader *wanted=feedback?epic12_xenos_feedback_ps:epic12_xenos_fast_ps;
			if(wanted!=activePs) { d->SetPixelShader(wanted); activePs=wanted; }
		}
		if (feedback && first>=snapshotEnd) {
			snapshotEnd=epic12_xenos_snapshot_reuse?
				epic12_gpu_snapshot_end(cmd,first,end,count,bounds,snapshot,epic12_xenos_attributes):end;
			d->SetTexture(1,NULL);
			epic12_xenos_resolve(snapshot.min_x,snapshot.min_y,
				snapshot.max_x-snapshot.min_x+1,snapshot.max_y-snapshot.min_y+1);
			d->SetTexture(1,epic12_xenos_result);
			++epic12_xenos_feedbacks;
			epic12_xenos_feedback_pixels+=epic12_gpu_rect_area(snapshot);
		}
		d->SetRenderState(D3DRS_ALPHABLENDENABLE,epic12_gpu_additive(c));
		if (!epic12_xenos_attributes) {
			float constants[8]={(float)c.tint.r,(float)c.tint.g,(float)c.tint.b,(float)c.sa,
				(float)c.transparent,(float)c.blend,(float)epic12_gpu_dest_mode(c),(float)epic12_gpu_dest_alpha(c)};
			d->SetPixelShaderConstantF(0,constants,2);
		}
		int vertices=0;
		for (int i=first;i<end;++i) {
			if (vertices+epic12_gpu_vertex_count(cmd[i])>EPIC12_GPU_DRAW_RECTS*3) {
				epic12_xenos_draw(vertices); vertices=0;
			}
			vertices+=epic12_gpu_vertices(cmd[i],bounds,slots,epic12_xenos_vertices+vertices);
		}
		epic12_xenos_draw(vertices);
		first=end;
	}
	d->SetTexture(1,NULL);
	epic12_xenos_resolve(0,0,width,height);
	d->SetRenderTarget(0,oldTarget); d->SetDepthStencilSurface(oldDepth);
	epic12_xenos_state->Apply();
	if(oldTarget) oldTarget->Release(); if(oldDepth) oldDepth->Release();
	RECT sourceRect={0,0,width,height}; POINT destPoint={bounds.min_x,bounds.min_y};
	if(timing) QueryPerformanceCounter(&timing->stamp[5]);
	if (FAILED(epic12_xenos_result->LockRect(0,&lr,&sourceRect,D3DLOCK_READONLY))) return false;
	if(timing) QueryPerformanceCounter(&timing->stamp[6]);
	if(epic12_xenos_fast_transfer) {
		epic12_gpu_untile_mask(lr.pBits,vram,bounds.min_x,bounds.min_y,width,height);
	} else {
		XGUntileTextureLevel(512,512,0,XGGetGpuFormat(D3DFMT_A8R8G8B8),0,
			vram,8192*4,&destPoint,lr.pBits,&sourceRect);
		for (int y=0;y<height;++y) {
			UINT32 *out=vram+(bounds.min_y+y)*8192+bounds.min_x;
			// ONE+ONE blending clamps at 255. Masking recovers EPIC12's clamp
			// at 31*8 exactly, also after repeated additive saturation.
			for (int x=0;x<width;++x) out[x]&=0x20f8f8f8;
		}
	}
	epic12_xenos_result->UnlockRect(0);
	epic12_xenos_readback+=width*height;
	if(timing) QueryPerformanceCounter(&timing->stamp[7]);
	return true;
}

static bool epic12_xenos_selftest()
{
	UINT32 *scratch=(UINT32*)calloc(128*8192,sizeof(UINT32));
	UINT32 result[64*64];
	if (!scratch) return false;
	// Exercise the final atlas row, including slots 254/255 on the large
	// texture. Dummy tags are never sampled and are cleared after this test.
	bool needed[2048]={false}; int slots[2048];
	epic12_xenos_cache.clear(); epic12_xenos_cache.begin_batch(needed,slots);
	for(int i=0;i<epic12_xenos_cache.capacity-2;++i) epic12_xenos_cache.allocate(128+i);
	for(int y=0;y<128;++y) for(int x=0;x<256;++x)
		scratch[y*8192+x]=((x+y)&1?0x20000000:0)|((x&31)<<19)|((y&31)<<11)|(((x+y)&31)<<3);
	Epic12GpuCommand commands[224];
	for(int i=0;i<32;++i) {
		Epic12GpuCommand &c=commands[i];
		c.sx=128+i; c.sy=i; c.x=(i*7)%33; c.y=(i*11)%33; c.w=32; c.h=32;
		c.flipx=i&1; c.flipy=(i>>1)&1; c.transparent=(i>>2)&1;
		c.blend=i%5!=0; c.dmode=i%5==3?1:(i%5==4?4:0);
		c.sa=i; c.da=i==2?0:(i%5==1?31:31-i);
		c.tint.r=(i*13)&63; c.tint.g=i&1?32:63; c.tint.b=(i*7)&63;
	}
	for(int i=32;i<128;++i) {
		Epic12GpuCommand &c=commands[i]; int group=(i-32)/8;
		c.sx=120+(i&1)*64; c.sy=80; c.x=(i&3)*16; c.y=((i>>2)&1)*16+16;
		c.w=c.h=16; c.flipx=i&1; c.flipy=(i>>1)&1; c.transparent=group&1;
		c.blend=group%4!=0; c.dmode=group%4==1?1:(group%4==3?4:0);
		c.sa=17; c.da=group<4?31:16; c.tint.r=32; c.tint.g=27; c.tint.b=45;
	}
	// Adjacent rectangles with different tint/alpha/transparency in the same
	// submission; include non-feedback, additive and destination-dependent mixes.
	for(int i=128;i<192;++i) {
		Epic12GpuCommand &c=commands[i]; int group=(i-128)/8;
		c=commands[32+(i&7)]; c.transparent=i&1; c.blend=1;
		c.tint.r=(i*13)&63; c.tint.g=(i*7)&63; c.tint.b=(i*23)&63; c.sa=i&31;
		c.dmode=group%3==2 && (i&1)?1:0;
		c.da=group%3==0?0:(group%3==1?31:1+(i%30));
		if(group%3==0 && (i&1)) c.blend=0;
	}
	// Blank and sparse source borders exercise crop/skip, both flips, and
	// opaque draws that must retain transparent-looking pixels. Page 2 is
	// outside the original two sources and also exercises cache replacement.
	for(int y=85;y<100;++y) for(int x=327;x<342;++x) if((x+y)%3)
		scratch[y*8192+x]=0x20000000|((x&31)<<19)|((y&31)<<11)|(((x+y)&31)<<3);
	for(int i=192;i<208;++i) {
		Epic12GpuCommand &c=commands[i]; c=commands[i-192];
		c.sx=i<200?256:320; c.sy=80; c.w=c.h=32;
		c.x=(i&1)*24; c.y=((i>>1)&1)*24;
		c.flipx=i&1; c.flipy=(i>>1)&1; c.transparent=i<196 || i>=200;
	}
	// Feedback, unrelated plain write, then adjacent feedback: the two
	// destination-dependent groups can reuse a snapshot without reordering.
	for(int i=208;i<224;++i) {
		Epic12GpuCommand &c=commands[i]; c=commands[200]; int part=i&3;
		c.sx=328; c.sy=88; c.w=c.h=8; c.transparent=1;
		c.x=part==0?0:(part==1?40:(part==2?8:24)); c.y=part==1?40:(part==3?24:0);
		c.blend=part!=1; c.dmode=0; c.da=part==3?31:16; c.sa=17;
	}
	rectangle bounds(0,63,0,63);
	bool ok=epic12_xenos_run(scratch,commands,224,bounds);
	if(ok) {
		for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
			result[y*64+x]=scratch[y*8192+x];
			scratch[y*8192+x]=((x+y)&1?0x20000000:0)|((x&31)<<19)|((y&31)<<11)|(((x+y)&31)<<3);
		}
		UINT32 *saved=m_bitmaps; UINT64 delay=epic12_device_blit_delay; m_bitmaps=scratch;
		for(int i=0;i<224;++i) epic12_gpu_replay(commands[i]);
		m_bitmaps=saved; epic12_device_blit_delay=delay;
		for(int y=0;y<64 && ok;++y) for(int x=0;x<64;++x) if(result[y*64+x]!=scratch[y*8192+x]) {
			char message[160];
			sprintf(message,"self-test mismatch x=%d y=%d GPU=%08x CPU=%08x",x,y,result[y*64+x],scratch[y*8192+x]);
			epic12_xenos_log(message); ok=false; break;
		}
	}
	free(scratch); epic12_gpu_reset();
	return ok;
}

static bool epic12_gpu_render(const Epic12GpuCommand *cmd,int count,const rectangle &bounds)
{
	Epic12GpuTiming sample;
	bool sampled=epic12_xenos_sample_batch();
	Epic12GpuTiming *timing=epic12_xenos_validated && sampled?&sample:NULL;
	if(timing) QueryPerformanceCounter(&timing->stamp[0]);
	unsigned generation;
	D3DDevice *d=(D3DDevice*)SDL_XBOX_AcquireCoreGpu(&generation);
	if(timing) QueryPerformanceCounter(&timing->stamp[1]);
	if(!d) {
		if(epic12_xenos_status!=5) epic12_xenos_log("display unavailable; using CPU");
		epic12_xenos_set_status(5); return false;
	}
	if(d!=epic12_xenos_device || generation!=epic12_xenos_generation) {
        SDL_XBOX_SetCoreGpuAsync(0); epic12_xenos_present_known=false;
		epic12_xenos_release(); epic12_xenos_device=d; epic12_xenos_generation=generation; epic12_xenos_failed=false; epic12_xenos_error=0;
		timing=NULL; // exclude shader creation and self-test from timings
	}
    unsigned presentSequence=0;
    epic12_xenos_present_mode=SDL_XBOX_GetCoreGpuFrameInfo(&presentSequence);
    bool firstAfterPresent=!epic12_xenos_present_known || presentSequence!=epic12_xenos_present_sequence;
    unsigned timingBucket=(epic12_xenos_present_mode==1?2:0)+(firstAfterPresent?0:1);
    epic12_xenos_present_sequence=presentSequence; epic12_xenos_present_known=true;
	bool ok=false;
	if(!epic12_xenos_failed) {
		if(!epic12_xenos_target && !epic12_xenos_create()) {
			epic12_xenos_log("resource/shader initialization failed; using CPU"); epic12_xenos_failed=true; epic12_xenos_error=3;
		}
		if(!epic12_xenos_failed && !epic12_xenos_validated) {
			UINT64 draws=epic12_xenos_draws, feedbacks=epic12_xenos_feedbacks, pixels=epic12_xenos_feedback_pixels;
			UINT64 hits=epic12_xenos_cache_hits, uploads=epic12_xenos_upload_pages, input=epic12_xenos_input_pixels;
			UINT64 readback=epic12_xenos_readback, rasterCommands=epic12_xenos_raster_commands, rasterPixels=epic12_xenos_raster_pixels;
			UINT64 reordered=epic12_xenos_reordered;
			epic12_xenos_validated=epic12_xenos_selftest();
			if(!epic12_xenos_failed && !epic12_xenos_validated && epic12_xenos_reorder) {
				epic12_xenos_log("draw reordering validation failed; checking original order");
				epic12_xenos_reorder=false;
				epic12_xenos_validated=epic12_xenos_selftest();
			}
			if(!epic12_xenos_validated && epic12_xenos_tiled) {
				epic12_xenos_log("tiled atlas pixel validation failed; checking linear atlas");
				epic12_xenos_tiled=false;
				int capacity=epic12_xenos_cache.capacity;
				if(epic12_xenos_create_atlas(capacity) || (capacity>128 && epic12_xenos_create_atlas(128)))
					epic12_xenos_validated=epic12_xenos_selftest();
				else epic12_xenos_failed=true;
			}
			if(!epic12_xenos_failed && !epic12_xenos_validated && epic12_xenos_snapshot_reuse) {
				epic12_xenos_log("snapshot reuse validation failed; checking adjacent groups");
				epic12_xenos_snapshot_reuse=false;
				epic12_xenos_validated=epic12_xenos_selftest();
			}
			if(!epic12_xenos_failed && !epic12_xenos_validated && epic12_xenos_alpha_trim) {
				epic12_xenos_log("alpha crop validation failed; checking full sprites");
				epic12_xenos_alpha_trim=false;
				epic12_xenos_validated=epic12_xenos_selftest();
			}
			if(!epic12_xenos_failed && !epic12_xenos_validated && epic12_xenos_cache.capacity==256) {
				epic12_xenos_log("large atlas validation unavailable; checking 128-slot cache");
				if(epic12_xenos_create_atlas(128)) epic12_xenos_validated=epic12_xenos_selftest();
				else epic12_xenos_failed=true;
			}
			if(!epic12_xenos_failed && !epic12_xenos_validated && epic12_xenos_specialized) {
				epic12_xenos_log("split shader self-test failed; checking unified shader");
				epic12_xenos_specialized=false;
				epic12_xenos_validated=epic12_xenos_selftest();
			}
			if(!epic12_xenos_failed && !epic12_xenos_validated && epic12_xenos_fast_transfer) {
				epic12_xenos_log("GPU transfer self-test failed; checking SDK transfer");
				epic12_xenos_fast_transfer=false;
				epic12_xenos_validated=epic12_xenos_selftest();
			}
			if(!epic12_xenos_failed && !epic12_xenos_validated && epic12_xenos_attributes) {
				epic12_xenos_log("sprite-attribute self-test failed; checking compatible GPU path");
				epic12_xenos_attributes=false;
				epic12_xenos_validated=epic12_xenos_selftest();
			}
			epic12_xenos_draws=draws; epic12_xenos_feedbacks=feedbacks; epic12_xenos_feedback_pixels=pixels;
			epic12_xenos_cache_hits=hits; epic12_xenos_upload_pages=uploads; epic12_xenos_input_pixels=input;
			epic12_xenos_readback=readback; epic12_xenos_raster_commands=rasterCommands; epic12_xenos_raster_pixels=rasterPixels;
			epic12_xenos_reordered=reordered;
			if(!epic12_xenos_validated) {
				epic12_xenos_log("pixel self-test failed; using CPU"); epic12_xenos_failed=true; epic12_xenos_error=4;
			} else epic12_xenos_log(epic12_xenos_attributes?
				"pixel self-test passed; GPU compositor active (sprite attributes)":
				"pixel self-test passed; GPU compositor active (uniform compatible)");
		}
		if(!epic12_xenos_failed) {
            SDL_XBOX_SetCoreGpuAsync(1);
			ok=epic12_xenos_run(m_bitmaps,cmd,count,bounds,timing);
			if(ok) {
				++epic12_xenos_batches; epic12_xenos_commands+=count;
				for(int i=0;i<count;++i) epic12_xenos_pixels+=cmd[i].w*cmd[i].h;
                Epic12GpuTimingBucket &bucket=epic12_xenos_timing_buckets[timingBucket];
                ++bucket.batches;
				if(timing) {
					++epic12_xenos_samples; ++bucket.samples;
					for(int i=0;i<7;++i) epic12_xenos_ticks[i]+=timing->stamp[i+1].QuadPart-timing->stamp[i].QuadPart;
					UINT64 total=timing->stamp[7].QuadPart-timing->stamp[0].QuadPart;
					if(total>epic12_xenos_peak_ticks) epic12_xenos_peak_ticks=total;
                    for(int i=0;i<7;++i) bucket.ticks[i]+=timing->stamp[i+1].QuadPart-timing->stamp[i].QuadPart;
                    if(total>bucket.peak) bucket.peak=total;
				}
			}
		}
	}
	if (!ok && !epic12_xenos_failed) {
		epic12_xenos_log("transfer failed; using CPU for this session");
		epic12_xenos_failed=true; epic12_xenos_error=6;
	}
	if(!ok) SDL_XBOX_SetCoreGpuAsync(0);
	epic12_xenos_set_status(ok?(epic12_xenos_attributes?1:7):epic12_xenos_error);
	SDL_XBOX_ReleaseCoreGpu();
	return ok;
}
#endif
