#define EPIC12_BLIT_TEST
#define EPIC12_GPU_TEST
#include "../../src/burn/devices/epic12.cpp"
#include <stdio.h>
#include <stdlib.h>
static unsigned seed=0x51984ad2;
static unsigned rnd(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static unsigned clamp(unsigned v){return v>31?31:v;}


static bool cache_draw_matches(UINT32 *ref, UINT32 *got, const Epic12GpuCommand &c, bool queued=false)
{
	m_bitmaps=ref; epic12_gpu_replay(c);
	m_bitmaps=got;
	rectangle bounds(c.x,c.x+c.w-1,c.y,c.y+c.h-1);
	if(queued) {
		if(!epic12_gpu_submit(c.flipx,c.transparent,c.blend,0,c.dmode,&bounds,got,
			c.sx,c.sy,c.x,c.y,c.w,c.h,c.flipy,c.sa,c.da,&c.tint)) return false;
		epic12_gpu_flush();
	} else {
		if(!epic12_gpu_render(&c,1,bounds)) return false;
		epic12_gpu_invalidate(bounds); // same successful batch completion as flush
	}
	for(int y=c.y;y<c.y+c.h;++y) for(int x=c.x;x<c.x+c.w;++x)
		if(ref[y*8192+x]!=got[y*8192+x]) return false;
	return true;
}

static bool cache_coherency(UINT32 *ref, UINT32 *got)
{
	for(int capacity=128;capacity<=256;capacity+=128) {
		gpu_test_set_capacity(capacity); gpu_test_fail=false; epic12_gpu_enabled=true;
		for(int page=64;page<208;++page) for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
			unsigned pos=((page>>6)*128+y)*8192+(page&63)*128+x;
			UINT32 pen=0x20000000|(((page+x)&31)<<19)|((((page>>5)+y)&31)<<11)|(((x+y)&31)<<3);
			ref[pos]=got[pos]=pen;
		}
		Epic12GpuCommand c; memset(&c,0,sizeof(c));
		c.w=c.h=8; c.tint.r=c.tint.g=c.tint.b=32;
		UINT64 before=gpu_test_uploads;
		for(int round=0;round<3;++round) {
			gpu_test_attributes=round!=0; gpu_test_specialized=round==2;
			for(int page=64;page<208;++page) {
				c.sx=(page&63)*128; c.sy=(page>>6)*128;
				if(!cache_draw_matches(ref,got,c)) return false;
			}
		}
		UINT64 uploaded=gpu_test_uploads-before;
		if(uploaded!=(capacity==128?432:144)) return false;
		// CPU writes must replace cached texture bytes, not just update VRAM.
		c.sx=0; c.sy=128;
		if(!cache_draw_matches(ref,got,c)) return false;
		before=gpu_test_uploads;
		epic12_gpu_cpu_write(1,130,1,1);
		ref[130*8192+1]=got[130*8192+1]=0x20f808e8;
		if(!cache_draw_matches(ref,got,c) || gpu_test_uploads!=before+1) return false;
		// A later GPU batch writes a resident source page. Flush must invalidate
		// that page even though the original source texture remains cached.
		Epic12GpuCommand write=c; write.sx=128; write.x=0; write.y=128; write.w=write.h=128;
		if(!cache_draw_matches(ref,got,write,true)) return false;
		before=gpu_test_uploads;
		if(!cache_draw_matches(ref,got,c) || gpu_test_uploads!=before+1) return false;
		before=gpu_test_uploads; epic12_gpu_reset();
		if(!cache_draw_matches(ref,got,c) || gpu_test_uploads!=before+1) return false;
		printf("PASS cache %d slots: 144 pages/3 passes uploads=%llu; CPU/GPU source writes and reset coherent\n",
			capacity,(unsigned long long)uploaded);
	}
	return true;
}

static bool batch_matches(UINT32 *ref, UINT32 *got, const Epic12GpuCommand *commands,
	int count, const rectangle &bounds)
{
	m_bitmaps=ref;
	for(int i=0;i<count;++i) epic12_gpu_replay(commands[i]);
	m_bitmaps=got;
	UINT64 delay=epic12_device_blit_delay;
	if(!epic12_gpu_render(commands,count,bounds) || epic12_device_blit_delay!=delay) return false;
	epic12_gpu_invalidate(bounds);
	for(int y=bounds.min_y;y<=bounds.max_y;++y) for(int x=bounds.min_x;x<=bounds.max_x;++x)
		if(ref[y*8192+x]!=got[y*8192+x]) return false;
	return true;
}

static bool reorder_invariants()
{
	Epic12GpuCommand original[1024], saved[1024], out[1024], inplace[1024];
	memset(original,0,sizeof(original));
	for(int i=0;i<10;++i) {
		original[i].sx=i; original[i].x=i*16; original[i].w=original[i].h=8;
		original[i].blend=i!=0; original[i].sa=17; original[i].da=31;
		original[i].tint.r=32; original[i].tint.g=27; original[i].tint.b=45;
	}
	// Eight entries from position 1 include index 8, but exclude index 9.
	original[8].blend=0;
	if(!epic12_gpu_reorder(original,10,out) || out[1].sx!=8) return false;
	original[8].blend=1; original[9].blend=0;
	if(epic12_gpu_reorder(original,10,out)!=0 || memcmp(out,original,10*sizeof(out[0]))) return false;
	// Touching edges are independent; a single overlapping pixel is a barrier.
	original[1].x=16; original[2].x=23; original[2].blend=0;
	original[3].x=31; original[3].blend=0;
	if(epic12_gpu_reorder(original,4,out)!=1 || out[1].sx!=3 || out[2].sx!=1 || out[3].sx!=2) return false;
	if(epic12_gpu_reorder(original,0,out)!=0 || epic12_gpu_reorder(original,1,out)!=0 || memcmp(out,original,sizeof(out[0]))) return false;
	unsigned allMoved=0;
	for(int test=0;test<24;++test) {
		int count=test%3==0?1024:(test%3==1?128:9);
		for(int i=0;i<count;++i) {
			Epic12GpuCommand &c=original[i]; memset(&c,0,sizeof(c));
			c.sx=i; c.sy=rnd()%4096; c.x=rnd()%240; c.y=rnd()%224;
			c.w=1+rnd()%32; c.h=1+rnd()%32; c.flipx=rnd()&1; c.flipy=rnd()&1;
			c.transparent=rnd()&1; c.blend=i%3!=0; c.dmode=i%7==0?1:(i%5==0?4:0);
			c.sa=rnd()%32; c.da=i%3==1?31:rnd()%32;
			c.tint.r=rnd()%64; c.tint.g=rnd()%64; c.tint.b=rnd()%64; c.tint.t=rnd()%256;
		}
		memcpy(saved,original,count*sizeof(original[0]));
		memcpy(inplace,original,count*sizeof(original[0]));
		unsigned moved=epic12_gpu_reorder(original,count,out); allMoved+=moved;
		if(memcmp(original,saved,count*sizeof(original[0])) || epic12_gpu_reorder(inplace,count,inplace)!=moved ||
			memcmp(out,inplace,count*sizeof(original[0]))) return false;
		int positions[1024]; for(int i=0;i<count;++i) positions[i]=-1;
		for(int i=0;i<count;++i) {
			int id=out[i].sx;
			if(id<0 || id>=count || positions[id]>=0 || memcmp(&out[i],&saved[id],sizeof(out[i]))) return false;
			positions[id]=i;
		}
		for(int i=0;i<count;++i) for(int j=i+1;j<count;++j) {
			const Epic12GpuCommand &a=saved[i],&b=saved[j];
			if(a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h && positions[i]>positions[j]) return false;
		}
	}
	if(!allMoved) return false;
	puts("PASS reorder: eight-command boundary, edge/overlap barriers, full-field permutation, untouched input and in-place equivalence");
	return true;
}

static bool snapshot_boundaries()
{
	Epic12GpuCommand c[80]; memset(c,0,sizeof(c));
	for(int i=0;i<80;++i) {
		c[i].x=(i&7)*8; c[i].y=(i>>3)*8; c[i].w=c[i].h=8;
		c[i].blend=1; c[i].sa=c[i].da=16;
		c[i].tint.r=c[i].tint.g=c[i].tint.b=32;
	}
	rectangle bounds(0,511,0,511),snapshot;
	Epic12GpuCommand probe[3]={c[0],c[1],c[2]};
	probe[1].blend=0; probe[1].x=32; probe[2].x=8;
	for(int attributes=0;attributes<2;++attributes) {
		int end=epic12_gpu_group_end(probe,0,3,bounds,snapshot,attributes!=0);
		if(end!=1 || epic12_gpu_snapshot_end(probe,0,end,3,bounds,snapshot,attributes!=0)!=3) return false;
		if(snapshot.min_x!=0 || snapshot.max_x!=15 || snapshot.min_y!=0 || snapshot.max_y!=7) return false;
		// Every preceding write matters, including plain and additive groups.
		probe[1].x=8;
		for(int additive=0;additive<2;++additive) {
			probe[1].blend=additive; probe[1].da=31;
			end=epic12_gpu_group_end(probe,0,3,bounds,snapshot,attributes!=0);
			if(epic12_gpu_snapshot_end(probe,0,end,3,bounds,snapshot,attributes!=0)!=1) return false;
		}
		probe[1].blend=0; probe[1].x=32; probe[2].x=128;
		end=epic12_gpu_group_end(probe,0,3,bounds,snapshot,attributes!=0);
		if(epic12_gpu_snapshot_end(probe,0,end,3,bounds,snapshot,attributes!=0)!=1) return false;
		probe[2].x=8;
	}
	// The group beginning at 31 ends at 33. A 32-command window must not
	// mark only its first command covered and then skip the group's Resolve.
	for(int i=1;i<31;++i) {c[i].blend=0; c[i].x=300; c[i].y=0;}
	c[31]=c[0]; c[31].x=8; c[32]=c[0]; c[32].x=16;
	for(int attributes=0;attributes<2;++attributes) {
		int end=epic12_gpu_group_end(c,0,33,bounds,snapshot,attributes!=0);
		if(epic12_gpu_snapshot_end(c,0,end,33,bounds,snapshot,attributes!=0)!=1) return false;
	}
	// A uniform-state boundary legitimately ends the future group at 32.
	c[32].tint.r=17;
	int end=epic12_gpu_group_end(c,0,33,bounds,snapshot,false);
	if(epic12_gpu_snapshot_end(c,0,end,33,bounds,snapshot,false)!=32) return false;
	puts("PASS snapshot lookahead: exact groups, all preceding writes, area and 32-command boundaries");
	return true;
}

static bool alpha_snapshot_integration(UINT32 *ref, UINT32 *got)
{
	for(int capacity=128;capacity<=256;capacity+=128) {
		gpu_test_set_capacity(capacity); gpu_test_fail=false; epic12_gpu_enabled=true;
		// Earlier atlas-capacity stress intentionally renders only into got.
		// These destination-dependent tests need identical starting targets.
		for(int y=0;y<512;++y) for(int x=0;x<512;++x) {
			UINT32 pen=(((x+y)&1)?0x20000000:0)|((x&31)<<19)|((y&31)<<11)|(((x+y)&31)<<3);
			ref[y*8192+x]=got[y*8192+x]=pen;
		}
		for(int y=112;y<160;++y) for(int x=112;x<160;++x) {
			UINT32 pen=0x00a848d0; // Nonzero RGB can still be entirely transparent.
			if(x>=124 && x<=142 && y>=123 && y<=133) pen|=0x20000000;
			ref[y*8192+x]=got[y*8192+x]=pen;
		}
		Epic12GpuCommand commands[12]; memset(commands,0,sizeof(commands));
		for(int i=0;i<12;++i) {
			Epic12GpuCommand &c=commands[i];
			c.sx=c.sy=112; c.x=256+(i&3)*32; c.y=256+(i>>2)*40; c.w=c.h=48;
			c.flipx=i&1; c.flipy=(i>>1)&1; c.transparent=i%5!=0; c.blend=i%3!=0;
			c.dmode=i%4==0?1:(i%4==1?4:0); c.sa=19; c.da=i%3==2?31:16;
			c.tint.r=32; c.tint.g=27; c.tint.b=45;
		}
		rectangle bounds(256,399,256,383);
		for(int shader=0;shader<3;++shader) for(int flags=0;flags<8;++flags) {
			gpu_test_attributes=shader!=0; gpu_test_specialized=shader==2;
			gpu_test_alpha_trim=(flags&1)!=0; gpu_test_snapshot_reuse=(flags&2)!=0; gpu_test_reorder=(flags&4)!=0;
			UINT64 moves=gpu_test_reorder_moved;
			if(!batch_matches(ref,got,commands,12,bounds)) return false;
			if((!gpu_test_attributes || !gpu_test_reorder) && moves!=gpu_test_reorder_moved) return false;
		}
		gpu_test_attributes=gpu_test_specialized=gpu_test_alpha_trim=gpu_test_snapshot_reuse=gpu_test_reorder=true;
		// Invalidate/rebuild alpha metadata when cached source transparency
		// changes. Empty commands must perform neither draws nor readback.
		epic12_gpu_cpu_write(112,112,48,48);
		for(int y=112;y<160;++y) for(int x=112;x<160;++x)
			ref[y*8192+x]=got[y*8192+x]=0x00a848d0;
		Epic12GpuCommand c=commands[0]; c.transparent=c.blend=1; c.dmode=0; c.da=16;
		UINT64 draws=gpu_test_draws,readback=gpu_test_readback;
		if(!cache_draw_matches(ref,got,c) || gpu_test_draws!=draws || gpu_test_readback!=readback) return false;
		epic12_gpu_cpu_write(135,127,1,1);
		ref[127*8192+135]=got[127*8192+135]=0x20f848d0;
		UINT64 raster=gpu_test_raster_pixels;
		if(!cache_draw_matches(ref,got,c) || gpu_test_raster_pixels!=raster+1 || gpu_test_readback!=readback+1) return false;
		// A GPU write replacing the source page must invalidate its alpha
		// metadata too; the lone visible pixel above disappears afterwards.
		epic12_gpu_cpu_write(256,0,128,128);
		for(int y=0;y<128;++y) for(int x=256;x<384;++x) ref[y*8192+x]=got[y*8192+x]=0x005020a0;
		Epic12GpuCommand overwrite=c; overwrite.sx=256; overwrite.sy=0;
		overwrite.x=128; overwrite.y=0; overwrite.w=overwrite.h=128;
		overwrite.transparent=overwrite.blend=0; overwrite.tint.r=overwrite.tint.g=overwrite.tint.b=32;
		if(!cache_draw_matches(ref,got,overwrite,true)) return false;
		draws=gpu_test_draws; readback=gpu_test_readback;
		if(!cache_draw_matches(ref,got,c) || gpu_test_draws!=draws || gpu_test_readback!=readback) return false;

		// Keep this snapshot-only draw-count assertion independent of reordering.
		gpu_test_reorder=false;
		// Explicit cross-type reuse: a plain/additive command can pass
		// between feedback sprites, unless its destination modifies the next.
		epic12_gpu_cpu_write(512,512,8,8);
		for(int y=512;y<520;++y) for(int x=512;x<520;++x) ref[y*8192+x]=got[y*8192+x]=0x206080a0;
		Epic12GpuCommand reuse[3]; memset(reuse,0,sizeof(reuse));
		for(int i=0;i<3;++i) {
			reuse[i].sx=reuse[i].sy=512; reuse[i].w=reuse[i].h=8; reuse[i].blend=1;
			reuse[i].sa=reuse[i].da=16; reuse[i].tint.r=reuse[i].tint.g=reuse[i].tint.b=32;
		}
		reuse[1].blend=0; reuse[1].x=32; reuse[2].x=8;
		rectangle reuseBounds(0,39,0,7);
		for(int shader=0;shader<3;++shader) for(int enabled=0;enabled<2;++enabled) {
			gpu_test_attributes=shader!=0; gpu_test_specialized=shader==2; gpu_test_snapshot_reuse=enabled!=0;
			UINT64 resolves=gpu_test_feedback;
			if(!batch_matches(ref,got,reuse,3,reuseBounds) || gpu_test_feedback-resolves!=(enabled?1:2)) return false;
		}
		gpu_test_snapshot_reuse=true; reuse[1].x=8;
		for(int additive=0;additive<2;++additive) {
			reuse[1].blend=additive; reuse[1].da=31; UINT64 resolves=gpu_test_feedback;
			if(!batch_matches(ref,got,reuse,3,reuseBounds) || gpu_test_feedback-resolves!=2) return false;
		}
		printf("PASS alpha/snapshot/reorder integration %d slots: all shader/fallback paths, flips, dirty metadata, no-op and dependencies\n",capacity);
	}
	gpu_test_attributes=gpu_test_specialized=gpu_test_alpha_trim=gpu_test_snapshot_reuse=gpu_test_reorder=true;
	return true;
}

int main()
{
	setbuf(stdout,NULL);
	for(int a=0;a<32;++a) for(int b=0;b<64;++b) {
		epic12_device_colrtable[a][b]=clamp(a*b/31);
		epic12_device_colrtable_rev[a^31][b]=clamp(a*b/31);
	}
	for(int a=0;a<32;++a) for(int b=0;b<32;++b) epic12_device_colrtable_add[a][b]=clamp(a+b);
	epic12_init_blend_tables();
	Epic12GpuCommand c; memset(&c,0,sizeof(c)); c.blend=1;
	for(int t=0;t<64;++t) for(int sa=0;sa<32;++sa) for(int s=0;s<32;++s)
		for(int d=0;d<32;++d) for(int mode=0;mode<3;++mode) {
			c.tint.r=c.tint.g=c.tint.b=t; c.sa=sa; c.da=(s+d+sa)&31; c.dmode=mode==2?4:mode;
			unsigned tinted=clamp(s*t/31), factor=mode==2?31-c.da:(mode==1?tinted:c.da);
			unsigned expected=clamp(tinted*sa/31+d*factor/31);
			UINT32 dst=d==31?255:d*8;
			Epic12GpuCommand normalized=c;
			normalized.dmode=epic12_gpu_dest_mode(c); normalized.da=epic12_gpu_dest_alpha(c);
			bool feedback=epic12_gpu_feedback(normalized);
			UINT32 specialized=gpu_test_pixel_specialized(s*8,feedback?dst:0xcdcdcdcd,dst,normalized,feedback);
			if(((gpu_test_pixel(s*8,dst,c)&248)>>3)!=expected || ((specialized&248)>>3)!=expected) {
				printf("FAIL unified/specialized shader arithmetic %d %d %d %d %d\n",t,sa,s,d,mode);return 1;
			}
		}
	puts("PASS 6291456 component cases on unified and fast/feedback shaders, including additive saturation");
	c.blend=0;
	for(int t=0;t<64;++t) for(int s=0;s<32;++s) for(int d=0;d<2;++d) {
		c.tint.r=c.tint.g=c.tint.b=t; c.sa=0; // plain ignores source alpha
		UINT32 expected=clamp(s*t/31), target=d?255:0;
		UINT32 specialized=gpu_test_pixel_specialized(s*8,0xcdcdcdcd,target,c,false);
		if(((gpu_test_pixel(s*8,target,c)&248)>>3)!=expected || ((specialized&248)>>3)!=expected) {
			puts("FAIL plain fast shader depends on alpha/destination");return 19;
		}
	}
	puts("PASS 4096 plain shader cases independent of alpha and destination");
	const unsigned words=8192*4096;
	UINT32 *initial=(UINT32*)calloc(words,4),*ref=(UINT32*)malloc(words*4),*got=(UINT32*)malloc(words*4);
	if(!initial||!ref||!got)return 2;
	// Distinct opaque/transparent colors in active atlas, viewport and wrap rows.
	for(int y=0;y<1024;++y) for(int x=0;x<1024;++x) initial[y*8192+x]=rnd()&0x20f8f8f8;
	for(int y=4090;y<4096;++y) for(int x=0;x<1024;++x) initial[y*8192+x]=rnd()&0x20f8f8f8;
	UINT16 cmds[120][10]; m_main_rammask=31;
	for(int test=0;test<120;++test) {
		gpu_test_set_capacity(test&1?128:256);
		gpu_test_attributes=test%3!=0;
		gpu_test_specialized=test%3==2;
		gpu_test_alpha_trim=test%4!=0; gpu_test_snapshot_reuse=test%7>=3; gpu_test_reorder=test%11>=4;
		for(int i=0;i<120;++i) {
			UINT16 *v=cmds[i]; int mode=i%7==0?1:(i%13==0?4:0);
			v[0]=0x1000|mode|(rnd()&0xf00); v[1]=(rnd()%32<<11)|(rnd()%32<<3);
			if(i%3) v[1]|=0xf8; // common additive halo
			v[2]=512+rnd()%256; v[3]=256+rnd()%256;
			v[4]=(int)(rnd()%192)-32; v[5]=(int)(rnd()%144)-16;
			v[6]=32+rnd()%96; v[7]=16+rnd()%64; v[8]=rnd()&255; v[9]=rnd();
			if(i%11==0) { v[2]=32+rnd()%128; v[3]=32+rnd()%128; } // framebuffer source and in-place copy
			if(i%29==0) v[3]=4090; // vertical wrap fallback
			if(i%31==0) v[2]=8180; // horizontal rejection
			if(i%37==0) v[0]|=0x20; // unsupported source mode
			if(i%41==0) v[4]=600; // target change / no-op clipping
		}
		UINT64 refdelay=0;
		for(int pass=0;pass<2;++pass) {
			m_bitmaps=pass?got:ref; memcpy(m_bitmaps,initial,words*4); epic12_gpu_reset();
			epic12_gpu_enabled=pass!=0; epic12_device_blit_delay=0;
			gpu_test_fail=(test%5)==0;
			for(int i=0;i<120;++i) {
				m_clip.set(0,i%9?255:767,0,223);
				m_ram16_copy=cmds[i]; UINT32 addr=0; gfx_draw(&addr);
				if(i%17==0) { // uploads must flush pending targets and invalidate texture pages
					epic12_gpu_cpu_write(600,300,64,1);
					for(int x=600;x<664;++x) m_bitmaps[300*8192+x]=(test&31)<<19;
				}
				if(i==60 && test%2) { epic12_gpu_flush(); epic12_gpu_enabled=false; } // live Off
			}
			epic12_gpu_flush();
			if(!pass) refdelay=epic12_device_blit_delay;
			else if(refdelay!=epic12_device_blit_delay) { printf("FAIL delay test %d\n",test);return 3; }
		}
		if(memcmp(ref,got,words*4)) {
			for(unsigned p=0;p<words;++p) if(ref[p]!=got[p]) {printf("FAIL batch test %d xy %u,%u ref=%08x got=%08x\n",test,p%8192,p/8192,ref[p],got[p]);break;}
			return 4;
		}
	}
	printf("PASS 14400 commands, full VRAM and delay comparison; simulated GPU batches=%u commands=%u\n",gpu_test_batches,gpu_test_commands);
	// Repeated uniform states exercise merged draws, shared destination
	// snapshots, overlap barriers, mode-4 normalization and the vertex cap.
	gpu_test_attributes=true; gpu_test_specialized=true;
	gpu_test_alpha_trim=gpu_test_snapshot_reuse=gpu_test_reorder=true;
	Epic12GpuCommand sequence[640];
	for(int i=0;i<640;++i) {
		Epic12GpuCommand &q=sequence[i]; memset(&q,0,sizeof(q));
		int group=i/8;
		q.sx=620+(i%5)*23; q.sy=380+(i%3)*7;
		q.x=(i&7)*32; q.y=((i>>3)&3)*48; q.w=q.h=32;
		q.flipx=i&1; q.flipy=(i>>1)&1; q.transparent=1;
		q.blend=group%5!=0; q.dmode=group%5==1?1:0;
		q.sa=17; q.da=group%5==2?16:(group%5==3?0:31);
		q.tint.r=32; q.tint.g=27; q.tint.b=45;
		if(q.dmode==0 && (i&1)) { q.dmode=4; q.da=31-q.da; }
		if(i>=320) { q.blend=1; q.dmode=0; q.sa=19; q.da=31; } // one large draw group
	}
	for(int pass=0;pass<2;++pass) {
		m_bitmaps=pass?got:ref; memcpy(m_bitmaps,initial,words*4); epic12_gpu_reset(); epic12_gpu_enabled=pass!=0;
		gpu_test_fail=false; epic12_device_blit_delay=0; m_clip.set(0,255,0,223);
		for(int i=0;i<640;++i) {
			const Epic12GpuCommand &q=sequence[i];
			if(!pass) epic12_gpu_replay(q);
			else if(!epic12_gpu_submit(q.flipx,q.transparent,q.blend,0,q.dmode,&m_clip,m_bitmaps,
				q.sx,q.sy,q.x,q.y,q.w,q.h,q.flipy,q.sa,q.da,&q.tint)) return 8;
		}
		epic12_gpu_flush();
	}
	if(memcmp(ref,got,words*4)) {puts("FAIL merged draws/snapshots/atlas geometry");return 9;}
	rectangle target(0,511,0,511),snapshot;
	Epic12GpuCommand pair[2]={sequence[16],sequence[17]};
	pair[1].x=pair[0].x+16; // overlap must break snapshot sharing
	if(epic12_gpu_group_end(pair,0,2,target,snapshot)!=1) return 10;
	pair[1].x=pair[0].x+32;
	if(epic12_gpu_group_end(pair,0,2,target,snapshot)!=2) return 11;
	pair[1].x=480; pair[1].y=480; // sparse rectangles must not resolve the whole screen
	if(epic12_gpu_group_end(pair,0,2,target,snapshot)!=1) return 12;
	puts("PASS 640 grouped commands, snapshot overlap/area barriers, RECTLIST atlas geometry and draw capacity");
	// Change every sprite's style within otherwise mergeable groups. Exercise
	// both the packed-attribute renderer and its uniform compatibility fallback.
	for(int i=0;i<640;++i) {
		Epic12GpuCommand &q=sequence[i];
		q.tint.r=(i*13)&63; q.tint.g=(i*7)&63; q.tint.b=(i*23)&63;
		q.sa=i&31; q.transparent=i&1;
		if(!q.blend && (i&1)) { q.blend=1; q.dmode=0; q.da=0; }
		if(q.dmode==1 && (i&1)) { q.dmode=0; q.da=1+i%30; }
	}
	UINT64 draws[3]={0,0,0};
	for(int pass=0;pass<4;++pass) {
		m_bitmaps=pass?got:ref; memcpy(m_bitmaps,initial,words*4); epic12_gpu_reset();
		epic12_gpu_enabled=pass!=0; gpu_test_attributes=pass>=2; gpu_test_specialized=pass==3; gpu_test_fail=false;
		epic12_device_blit_delay=0;
		UINT64 beforeDraws=gpu_test_draws;
		for(int i=0;i<640;++i) {
			const Epic12GpuCommand &q=sequence[i];
			if(!pass) epic12_gpu_replay(q);
			else if(!epic12_gpu_submit(q.flipx,q.transparent,q.blend,0,q.dmode,&m_clip,m_bitmaps,
				q.sx,q.sy,q.x,q.y,q.w,q.h,q.flipy,q.sa,q.da,&q.tint)) return 13;
		}
		epic12_gpu_flush();
		if(pass) {
			draws[pass-1]=gpu_test_draws-beforeDraws;
			if(memcmp(ref,got,words*4)) {printf("FAIL mixed attributes path %d\n",pass);return 14;}
		}
	}
	if(draws[1]>=draws[0] || draws[1]!=draws[2]) {puts("FAIL attribute draw reduction or shader split changed submissions");return 15;}
	pair[0]=sequence[16]; pair[1]=pair[0]; pair[1].x+=32;
	pair[1].tint.r^=31; pair[1].sa^=31; pair[1].transparent^=1;
	if(epic12_gpu_group_end(pair,0,2,target,snapshot,true)!=2 ||
		epic12_gpu_group_end(pair,0,2,target,snapshot,false)!=1) return 16;
	pair[1].da=31;
	if(epic12_gpu_group_end(pair,0,2,target,snapshot,true)!=1) return 17;
	if(sizeof(Epic12GpuVertex)!=16 || epic12_gpu_pack4(1,2,3,4)!=0x04030201) return 18;
	printf("PASS 640 varying-style commands on all paths; draws uniform=%llu attributes-unified=%llu specialized=%llu\n",
		(unsigned long long)draws[0],(unsigned long long)draws[1],(unsigned long long)draws[2]);
	// Retain a 128-page batch cap with either source cache capacity.
	// The 256-slot cache still improves source residency across batches.
	m_bitmaps=got; epic12_gpu_enabled=true; gpu_test_fail=false;
	m_clip.set(0,255,0,223); clr_t tint; tint.r=tint.g=tint.b=32;
	for(int capacity=128;capacity<=256;capacity+=128) {
		gpu_test_set_capacity(capacity); epic12_gpu_reset();
		unsigned before=gpu_test_batches;
		for(int p=64;p<324;++p) {
			if(!epic12_gpu_submit(0,1,1,0,0,&m_clip,got,(p&63)*128,(p>>6)*128,0,0,64,64,0,16,31,&tint))return 6;
		}
		epic12_gpu_flush();
		unsigned produced=gpu_test_batches-before;
		if(produced!=3u) return 7;
	}
	puts("PASS restored batch cap: 260 source pages -> 3 batches with either 128/256-slot cache");
	if(!gpu_test_fast_groups || !gpu_test_feedback_groups || !gpu_test_unified_groups) return 20;
	printf("PASS shader selection: fast=%llu feedback=%llu unified=%llu groups\n",
		(unsigned long long)gpu_test_fast_groups,(unsigned long long)gpu_test_feedback_groups,
		(unsigned long long)gpu_test_unified_groups);
	if(!cache_coherency(ref,got)) {puts("FAIL persistent cache/dirty source coherency");return 21;}
	if(!reorder_invariants() || !snapshot_boundaries() || !alpha_snapshot_integration(ref,got)) {puts("FAIL alpha/snapshot regression");return 22;}
	if(!gpu_test_reorder_moved) {puts("FAIL reorder path never exercised");return 23;}
	printf("PASS reorder model exercised: batches=%llu moved_commands=%llu\n",
		(unsigned long long)gpu_test_reorder_batches,(unsigned long long)gpu_test_reorder_moved);
	free(initial);free(ref);free(got);return gpu_test_batches?0:5;
}
