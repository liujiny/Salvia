// CPU model of the GPU's UNORM render target and shader arithmetic. This
// tests command ordering/readback barriers without pretending to emulate
// Xenos driver behavior or predict console performance.
static unsigned gpu_test_batches, gpu_test_commands;
static UINT64 gpu_test_pixels, gpu_test_readback, gpu_test_feedback;
static UINT64 gpu_test_draws, gpu_test_old_draws, gpu_test_old_feedback, gpu_test_feedback_pixels;
static bool gpu_test_fail;
static bool gpu_test_attributes=true;
static bool gpu_test_specialized=true;
static UINT64 gpu_test_fast_groups, gpu_test_feedback_groups, gpu_test_unified_groups;
#include "epic12_gpu_cache.h"
#include "epic12_gpu_alpha.h"
static Epic12GpuAlphaPage gpu_test_alpha[256];
static bool gpu_test_alpha_trim=true, gpu_test_snapshot_reuse=true, gpu_test_reorder=true;
static UINT64 gpu_test_reorder_batches, gpu_test_reorder_moved;
static UINT64 gpu_test_kept_commands, gpu_test_raster_pixels, gpu_test_original_readback, gpu_test_snapshot_reused;
#ifndef EPIC12_GPU_TEST_CACHE_CAPACITY
#define EPIC12_GPU_TEST_CACHE_CAPACITY 256
#endif
static Epic12GpuPageCache gpu_test_cache;
static bool gpu_test_cache_initialized;
static int gpu_test_legacy_pages[128];
static UINT64 gpu_test_legacy_uploads;
static void gpu_test_clear_legacy() {
	for(int i=0;i<128;++i) gpu_test_legacy_pages[i]=-1;
}
static UINT32 gpu_test_texture[256][128*128];
static UINT64 gpu_test_cache_hits, gpu_test_uploads, gpu_test_invalidated_pages, gpu_test_cache_resets;
static void gpu_test_ensure_cache() {
	if (!gpu_test_cache_initialized) {
		gpu_test_cache.reset(EPIC12_GPU_TEST_CACHE_CAPACITY); gpu_test_cache_initialized=true;
		gpu_test_clear_legacy();
	}
}
static void gpu_test_set_capacity(int capacity) {
	gpu_test_cache.reset(capacity); gpu_test_cache_initialized=true; gpu_test_clear_legacy();
}
static unsigned epic12_gpu_batch_page_capacity() {
	gpu_test_ensure_cache();
	return (unsigned)(gpu_test_cache.capacity < 128 ? gpu_test_cache.capacity : 128);
}
static void epic12_gpu_invalidate(const rectangle &r) {
	gpu_test_ensure_cache();
	for(int slot=0;slot<gpu_test_cache.capacity;++slot) {
		int page=gpu_test_cache.pages[slot];
		if(page>=0 && epic12_gpu_intersects((page&63)*128,(page>>6)*128,128,128,r))
			++gpu_test_invalidated_pages;
	}
	for(int slot=0;slot<128;++slot) {
		int page=gpu_test_legacy_pages[slot];
		if(page>=0 && epic12_gpu_intersects((page&63)*128,(page>>6)*128,128,128,r))
			gpu_test_legacy_pages[slot]=-1;
	}
	gpu_test_cache.invalidate(r.min_x,r.min_y,r.max_x,r.max_y);
}
static void epic12_gpu_reset() {
	gpu_test_ensure_cache(); gpu_test_cache.clear(); gpu_test_clear_legacy(); ++gpu_test_cache_resets;
}
static void epic12_gpu_exit() {
	printf("GPU MODEL batches=%u commands=%u pixels=%llu readback=%llu feedback=%llu\n",
		gpu_test_batches,gpu_test_commands,(unsigned long long)gpu_test_pixels,
		(unsigned long long)gpu_test_readback,(unsigned long long)gpu_test_feedback);
	printf("SUBMISSION draws=%llu old_draws=%llu resolves=%llu old_resolves=%llu resolve_pixels=%llu\n",
		(unsigned long long)gpu_test_draws,(unsigned long long)gpu_test_old_draws,
		(unsigned long long)gpu_test_feedback,(unsigned long long)gpu_test_old_feedback,
		(unsigned long long)gpu_test_feedback_pixels);
	printf("CACHE capacity=%d hits=%llu uploads=%llu invalidated_pages=%llu resets=%llu legacy128_uploads=%llu\n",
		gpu_test_cache.capacity,(unsigned long long)gpu_test_cache_hits,
		(unsigned long long)gpu_test_uploads,(unsigned long long)gpu_test_invalidated_pages,
		(unsigned long long)gpu_test_cache_resets,(unsigned long long)gpu_test_legacy_uploads);
	printf("REORDER batches=%llu moved_commands=%llu\n",
		(unsigned long long)gpu_test_reorder_batches,(unsigned long long)gpu_test_reorder_moved);
	printf("ALPHA kept_commands=%llu raster_pixels=%llu original_readback=%llu snapshot_reused=%llu\n",
		(unsigned long long)gpu_test_kept_commands,(unsigned long long)gpu_test_raster_pixels,
		(unsigned long long)gpu_test_original_readback,(unsigned long long)gpu_test_snapshot_reused);
#ifdef EPIC12_GPU_ALPHA_PROFILE
	printf("ALPHA QUERY built_pixels=%llu summaries=%llu rows=%llu pages=%llu commands=%llu empty=%llu cache_hits=%llu cache_misses=%llu\n",
		(unsigned long long)epic12_gpu_alpha_stats.builtPixels,(unsigned long long)epic12_gpu_alpha_stats.summaryReads,
		(unsigned long long)epic12_gpu_alpha_stats.rowReads,(unsigned long long)epic12_gpu_alpha_stats.queryPages,
		(unsigned long long)epic12_gpu_alpha_stats.trimCommands,(unsigned long long)epic12_gpu_alpha_stats.emptyCommands,
		(unsigned long long)epic12_gpu_alpha_stats.cacheHits,(unsigned long long)epic12_gpu_alpha_stats.cacheMisses);
#endif
}

static UINT32 gpu_test_pixel(UINT32 raw, UINT32 dest, const Epic12GpuCommand &c)
{
	UINT32 result=raw&0x20000000;
	int tint[3]={c.tint.b,c.tint.g,c.tint.r};
	for(int ch=0;ch<3;++ch) {
		int shift=ch*8;
#ifdef EPIC12_GPU_ORDER_TEST
		// Whole-game ordering tests use integer equivalents; the unit test
		// above them independently exhausts the floating shader arithmetic.
		unsigned s=(((raw>>shift)&255)/8)*tint[ch]/31; if(s>31) s=31;
		unsigned byte=s*8;
		if(c.blend) {
			unsigned out=s*c.sa/31;
			if(c.dmode!=0 || c.da!=31) {
				unsigned factor=c.dmode==4?31-c.da:(c.dmode==1?s:c.da);
				out+=(((dest>>shift)&255)/8)*factor/31; if(out>31) out=31;
			}
			byte=out*8;
		}
#else
		float sample=((raw>>shift)&255)/255.0f;
		float s=floorf(sample*(255.0f/8.0f)+0.001f);
		s=floorf(s*tint[ch]/31.0f+0.0001f); if(s>31) s=31;
		float out=s;
		if(c.blend) {
			out=floorf(s*c.sa/31.0f+0.0001f);
			if(c.dmode!=0 || c.da!=31) {
				float d=floorf((((dest>>shift)&255)/255.0f)*(255.0f/8.0f)+0.001f);
				float factor=c.dmode==4?31-c.da:(c.dmode==1?s:c.da);
				out+=floorf(d*factor/31.0f+0.0001f); if(out>31) out=31;
			}
		}
		unsigned byte=(unsigned)floorf((out*(8.0f/255.0f))*255.0f+0.5f);
#endif
		if(c.blend && c.dmode==0 && c.da==31) { byte+=(dest>>shift)&255; if(byte>255) byte=255; }
		result|=byte<<shift;
	}
	return result;
}


// Models the actual fast/feedback shader split. The shader snapshot and the
// hardware blend target are separate inputs: fast shaders must never fetch a
// destination texture, while additive blending still reads the live target.
static UINT32 gpu_test_pixel_specialized(UINT32 raw, UINT32 snapshot, UINT32 target,
	const Epic12GpuCommand &c, bool feedback)
{
	UINT32 result=raw&0x20000000;
	int tint[3]={c.tint.b,c.tint.g,c.tint.r};
	for(int ch=0;ch<3;++ch) {
		int shift=ch*8;
#ifdef EPIC12_GPU_ORDER_TEST
		unsigned s=(((raw>>shift)&255)/8)*tint[ch]/31; if(s>31) s=31;
		unsigned out=s;
		if(feedback) {
			unsigned d=((snapshot>>shift)&255)/8;
			unsigned factor=c.dmode==1?s:c.da;
			out=s*c.sa/31+d*factor/31; if(out>31) out=31;
		} else if(c.blend) out=s*c.sa/31;
		unsigned byte=out*8;
#else
		float sample=((raw>>shift)&255)/255.0f;
		float s=floorf(sample*(255.0f/8.0f)+0.001f);
		s=floorf(s*tint[ch]/31.0f+0.0001f); if(s>31) s=31;
		float out=s;
		if(feedback) {
			out=floorf(s*c.sa/31.0f+0.0001f);
			float d=floorf((((snapshot>>shift)&255)/255.0f)*(255.0f/8.0f)+0.001f);
			float factor=c.dmode==1?s:c.da;
			out+=floorf(d*factor/31.0f+0.0001f); if(out>31) out=31;
		} else if(c.blend) out=floorf(s*c.sa/31.0f+0.0001f);
		unsigned byte=(unsigned)floorf((out*(8.0f/255.0f))*255.0f+0.5f);
#endif
		if(epic12_gpu_additive(c)) { byte+=(target>>shift)&255; if(byte>255) byte=255; }
		result|=byte<<shift;
	}
	return result;
}

static bool epic12_gpu_render(const Epic12GpuCommand *cmd,int count,const rectangle &originalBounds)
{
	rectangle r=originalBounds;
	if(gpu_test_fail) return false;
	gpu_test_ensure_cache();
	int slots[2048]; bool needed[2048]={false}; unsigned pageCount=0;
	for(int i=0;i<count;++i) {
		const Epic12GpuCommand &c=cmd[i];
		for(int y=c.sy/128;y<=(c.sy+c.h-1)/128;++y)
			for(int x=c.sx/128;x<=(c.sx+c.w-1)/128;++x)
				if(!needed[y*64+x]) {
					if(pageCount==epic12_gpu_batch_page_capacity()) {puts("FAIL GPU batch source capacity"); abort();}
					needed[y*64+x]=true; ++pageCount;
				}
	}
	// Shadow only the old 128-slot replacement tags while one batch also fits
	// that legacy limit. A >128-page batch is a capability the old policy could
	// not represent without splitting, so reset the shadow and count its source
	// pages as uploads instead of aborting the correctness model.
	if(pageCount<=128) {
		bool oldPinned[128]={false},oldResident[2048]={false};
		for(int slot=0;slot<128;++slot) {
			int page=gpu_test_legacy_pages[slot];
			if(page>=0 && needed[page]) { oldPinned[slot]=true; oldResident[page]=true; }
		}
		for(int page=0,next=0;page<2048;++page) if(needed[page] && !oldResident[page]) {
			while(next<128 && oldPinned[next]) ++next;
			if(next>=128) {puts("FAIL legacy shadow capacity"); abort();}
			gpu_test_legacy_pages[next]=page; oldPinned[next]=true; ++gpu_test_legacy_uploads;
		}
	} else {
		gpu_test_clear_legacy();
		gpu_test_legacy_uploads+=pageCount;
	}
	gpu_test_cache_hits+=gpu_test_cache.begin_batch(needed,slots);
	for(int page=0;page<2048;++page) if(needed[page] && slots[page]<0) {
		int slot=gpu_test_cache.allocate(page);
		if(slot<0) {puts("FAIL GPU cache allocation"); abort();}
		slots[page]=slot;
		for(int y=0;y<128;++y) memcpy(gpu_test_texture[slot]+y*128,
			m_bitmaps+((page>>6)*128+y)*8192+(page&63)*128,128*sizeof(UINT32));
		gpu_test_alpha[slot].build(gpu_test_texture[slot],128);
		++gpu_test_uploads;
	}
	++gpu_test_batches; gpu_test_commands+=count;
	gpu_test_original_readback+=epic12_gpu_rect_area(originalBounds);
	for(int i=0;i<count;++i) {
		gpu_test_pixels+=cmd[i].w*cmd[i].h;
		gpu_test_old_draws+=epic12_gpu_vertex_count(cmd[i])/3;
		if(cmd[i].blend && !(cmd[i].dmode==0 && (cmd[i].da==31 || cmd[i].da==0))) ++gpu_test_old_feedback;
	}
	static Epic12GpuCommand cropped[1024];
	if(gpu_test_alpha_trim) {
		int kept=epic12_gpu_alpha_crop_batch(cmd,count,cropped,gpu_test_alpha,slots,gpu_test_cache.capacity,r);
		if(kept<0) {puts("FAIL source alpha metadata/slot mapping"); abort();}
		cmd=cropped; count=kept;
	}
	if(gpu_test_attributes && gpu_test_reorder) {
		unsigned moved=epic12_gpu_reorder(cmd,count,cropped);
		cmd=cropped;
		gpu_test_reorder_moved+=moved;
		if(moved) ++gpu_test_reorder_batches;
	}
	gpu_test_kept_commands+=count;
	if(!count) return true;

	static UINT32 frame[512*512],snapshot[512*512];
	int w=r.max_x-r.min_x+1,h=r.max_y-r.min_y+1;
	gpu_test_readback+=w*h;
	memset(frame,0,sizeof(frame)); memset(snapshot,0xcd,sizeof(snapshot));
	for(int y=0;y<h;++y) memcpy(frame+y*512,m_bitmaps+(r.min_y+y)*8192+r.min_x,w*4);
	int snapshotEnd=0;
	for(int first=0;first<count;) {
		rectangle copy;
		int end=epic12_gpu_group_end(cmd,first,count,r,copy,gpu_test_attributes);
		bool feedback=epic12_gpu_feedback(cmd[first]);
		bool specialized=gpu_test_attributes && gpu_test_specialized;
		if(!specialized) ++gpu_test_unified_groups;
		else if(feedback) ++gpu_test_feedback_groups;
		else ++gpu_test_fast_groups;
		if(feedback) {
			if(!gpu_test_snapshot_reuse || first>=snapshotEnd) {
				snapshotEnd=gpu_test_snapshot_reuse?epic12_gpu_snapshot_end(cmd,first,end,count,r,copy,gpu_test_attributes):end;
				++gpu_test_feedback; gpu_test_feedback_pixels+=epic12_gpu_rect_area(copy);
				for(int y=copy.min_y;y<=copy.max_y;++y)
					memcpy(snapshot+y*512+copy.min_x,frame+y*512+copy.min_x,(copy.max_x-copy.min_x+1)*4);
			} else {
				if(end>snapshotEnd) {puts("FAIL partial feedback group snapshot coverage"); abort();}
				++gpu_test_snapshot_reused;
			}
		}
		int groupVertices=0;
		for(int i=first;i<end;++i) {
			const Epic12GpuCommand &c=cmd[i];
			gpu_test_raster_pixels+=c.w*c.h;
			Epic12GpuVertex vertices[75];
			int nv=epic12_gpu_vertices(c,r,slots,vertices);
			if(nv!=epic12_gpu_vertex_count(c) || nv>75) {puts("FAIL vertex count"); abort();}
			if(groupVertices+nv>EPIC12_GPU_DRAW_RECTS*3) {++gpu_test_draws; groupVertices=0;}
			groupVertices+=nv;
			// Decode the packed vertex attributes, not the original command.
			// The compatible path instead uses the first command's uniforms.
			// Generated UVs and the shared snapshot exercise the native planner.
			Epic12GpuCommand shader=cmd[first];
			shader.dmode=epic12_gpu_dest_mode(cmd[first]); shader.da=epic12_gpu_dest_alpha(cmd[first]);
			for(int q=0;q<nv;q+=3) {
				const Epic12GpuVertex *v=vertices+q;
				if(gpu_test_attributes) {
					for(int k=1;k<3;++k) if(v[k].tintAlpha!=v[0].tintAlpha || v[k].mode!=v[0].mode) {
						puts("FAIL nonconstant rectangle attributes"); abort();
					}
					UINT32 t=v[0].tintAlpha,m=v[0].mode;
					shader.tint.r=t&255; shader.tint.g=(t>>8)&255; shader.tint.b=(t>>16)&255; shader.sa=t>>24;
					shader.transparent=m&255; shader.blend=(m>>8)&255; shader.dmode=(m>>16)&255; shader.da=m>>24;
				}
				int x0=(int)v[0].x,y0=(int)v[0].y,rw=(int)(v[1].x-v[0].x),rh=(int)(v[2].y-v[0].y);
				for(int y=0;y<rh;++y) for(int x=0;x<rw;++x) {
					int u=(int)(v[0].u+(v[1].u-v[0].u)*((x+0.5f)/rw));
					int t=(int)(v[0].v+(v[2].v-v[0].v)*((y+0.5f)/rh));
					int slot=(t/128)*16+u/128;
					if(slot>=gpu_test_cache.capacity || gpu_test_cache.pages[slot]<0) {
						puts("FAIL invalid atlas coordinate/cache slot"); abort();
					}
					// Read the last uploaded texture bytes. Reading m_bitmaps here
					// would hide missing invalidation after CPU/GPU source writes.
					UINT32 pen=gpu_test_texture[slot][(t&127)*128+(u&127)];
					if(shader.transparent && !(pen&0x20000000)) continue;
					int pos=(y0+y)*512+x0+x;
					if(specialized) frame[pos]=gpu_test_pixel_specialized(pen,
						feedback?snapshot[pos]:0xcdcdcdcd,frame[pos],shader,feedback);
					else frame[pos]=gpu_test_pixel(pen,feedback?snapshot[pos]:frame[pos],shader);
				}
			}
		}
		if(groupVertices) ++gpu_test_draws;
		first=end;
	}
	for(int y=0;y<h;++y) for(int x=0;x<w;++x) m_bitmaps[(r.min_y+y)*8192+r.min_x+x]=frame[y*512+x]&0x20f8f8f8;
	return true;
}
