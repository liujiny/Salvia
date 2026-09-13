/* Bounded per-frame aggregation: no file I/O from CPU memory handlers.
   Always retain 30 preceding frames; capture 120 following a black transition
   or coin/start edge. Periodic snapshots also cover frames skipped by video. */
#if RAIDEN2_DEBUG
#include <stdio.h>
struct r2_debug_access_record {
 unsigned count, first_pc, last_pc;
 UINT16 first, last;
};
struct r2_debug_snapshot {
 unsigned frame, pc, samples[8], sample_count, draws, pixels, lit, palette;
 unsigned bank_mismatch, fetch_mismatch, resets;
 unsigned output_calls, output_nonzero, output_lit, output_indexed, output_dupes;
 unsigned tiles[5];
 unsigned event_count;
 struct { unsigned pc; UINT16 address,value; } events[64];
 UINT16 prg, cop, layers, spr, mode, src, dst, size, status, brightness;
 UINT32 regs[8];
 UINT16 scroll[6];
 UINT8 banks[4], ports[6];
 struct r2_debug_access_record writes[0x200], reads[0x200];
 struct r2_debug_access_record sound_writes[32], sound_reads[32];
};
static struct r2_debug_snapshot r2_trace, r2_history[31];
static unsigned r2_frame_number, r2_post_frames, r2_ever_lit, r2_was_black;
static unsigned r2_reset_count, r2_last_draws;
static UINT8 r2_old_coin = 0xff, r2_old_start = 0xff;
static char r2_log_path[1024];

static void r2_debug_sound_access(unsigned addr, unsigned value, int write)
{
 struct r2_debug_access_record *r;
 unsigned index=addr&31;
 r=write?&r2_trace.sound_writes[index]:&r2_trace.sound_reads[index];
 if(!r->count) { r->first=(UINT16)value;r->first_pc=activecpu_get_pc(); }
 r->count++;r->last=(UINT16)value;r->last_pc=activecpu_get_pc();
}

static FILE *r2_debug_try_path(const char *directory, const char *mode)
{
 FILE *f;
 size_t n;
 if(!directory || !directory[0]) return NULL;
 n=strlen(directory);
 snprintf(r2_log_path,sizeof(r2_log_path),"%s%sraiden2_stage3.log",directory,
   (directory[n-1]=='\\' || directory[n-1]=='/') ? "" : PATH_DEFAULT_SLASH());
 f=fopen(r2_log_path,mode);
 return f;
}

static FILE *r2_debug_open_initial(void)
{
 FILE *f;
 r2_log_path[0]=0;
 f=r2_debug_try_path(options.libretro_save_path,"w");
 if(f) return f;
 f=r2_debug_try_path(options.libretro_content_path,"w");
 if(f) return f;
#if defined(_XBOX360)
 strcpy(r2_log_path,"game:\\raiden2_stage3.log");
#else
 strcpy(r2_log_path,"raiden2_stage3.log");
#endif
 return fopen(r2_log_path,"w");
}

/* Called on the emulation thread immediately before the frontend callback,
   after either CPU conversion or indexed framebuffer upload. Other games
   return before touching diagnostic state. Never read a video worker job. */
void raiden2_debug_output(const void *data, unsigned width, unsigned height,
 unsigned pitch, unsigned stride, int indexed, const UINT32 *palette)
{
 unsigned x,y,p;
 const UINT8 *row;
 if(!Machine || strcmp(Machine->gamedrv->name,"raiden2")) return;
 if(!data) { r2_trace.output_dupes++; return; }
 r2_trace.output_calls++;
 r2_trace.output_nonzero=r2_trace.output_lit=0;
 r2_trace.output_indexed=indexed;
 for(y=0;y<height;y++) {
  row=(const UINT8*)data+y*pitch;
  for(x=0;x<width;x++) {
   p=stride==4 ? ((const UINT32*)row)[x] : ((const UINT16*)row)[x];
   if(p) r2_trace.output_nonzero++;
   if(indexed) {
    if(palette && p<2050 && (palette[p]&0xffffff)) r2_trace.output_lit++;
   } else if(stride==4 ? (p&0xffffff)!=0 : p!=0) r2_trace.output_lit++;
  }
 }
}

static void r2_debug_access(unsigned addr, unsigned value, int write)
{
 struct r2_debug_access_record *r;
 unsigned index;
 if (addr < 0x400 || addr >= 0x800) return;
 index = (addr - 0x400) >> 1;
 r = write ? &r2_trace.writes[index] : &r2_trace.reads[index];
 if (!r->count) { r->first = (UINT16)value; r->first_pc = activecpu_get_pc(); }
 r->count++;
 r->last = (UINT16)value;
 r->last_pc = activecpu_get_pc();
 if(write && (addr&1) && (addr==0x6cb || addr==0x6cd || addr==0x471 ||
    addr==0x6fd || addr==0x6ff || addr==0x61d || (addr>=0x501 && addr<=0x505))) {
  index=r2_trace.event_count++ & 63;
  r2_trace.events[index].pc=activecpu_get_pc();
  r2_trace.events[index].address=(UINT16)(addr&~1U);
  r2_trace.events[index].value=(UINT16)value;
 }
}

static void r2_debug_sample(int unused)
{
 unsigned n = r2_trace.sample_count++;
 (void)unused;
 r2_trace.samples[n & 7] = cpunum_get_pc_byte(0);
}

static void r2_debug_bank(void)
{
 unsigned i, base = (~(r2_prg_bank >> 15) & 1) * 0x20000;
 unsigned pc = activecpu_get_pc() & 0xfffff;
 /* Read every byte of the bank on the rare bank switch. No opcode base
    mutation here: compare the actual current fetch cache where meaningful. */
 for (i = 0; i < 0x20000; i++)
  if (cpu_readmem20(0x20000 + i) != r2_bank_rom[base + i]) r2_trace.bank_mismatch++;
 if (pc >= 0x20000 && pc < 0x40000) {
  i = r2_bank_rom[base + pc - 0x20000];
  if (cpu_readop(pc) != i || cpu_readop_arg(pc) != i) r2_trace.fetch_mismatch++;
 }
}

static void r2_debug_print(FILE *f, const struct r2_debug_snapshot *s, const char *why)
{
 unsigned i, kind;
 fprintf(f, "F=%u %s PC=%05x reset=%u prg_bank=%04x ROMbase=%05x cop_bank=%04x bg_bank=%u mg_bank=%u fg_bank=%u tx_bank=%u layer_enable=%04x sprites_cur_start=%04x cop_dma_mode=%03x cop_dma_src=%04x cop_dma_dst=%04x cop_dma_size=%04x cop_status=%04x brightness=%04x draws=%u bitmap_nonzero=%u rgb_nonblack=%u palette_nonblack=%u bank_read_mismatch=%u fetch_mismatch=%u\n",
 s->frame, why, s->pc, s->resets, s->prg, (~(s->prg >> 15)&1)*0x20000,
 s->cop, s->banks[0],s->banks[1],s->banks[2],s->banks[3],s->layers,s->spr,
 s->mode,s->src,s->dst,s->size,s->status,s->brightness,s->draws,s->pixels,s->lit,s->palette,s->bank_mismatch,s->fetch_mismatch);
 fprintf(f," cop_regs=");
 for(i=0;i<8;i++) fprintf(f,"%08x ",s->regs[i]);
 fprintf(f,"scroll=");
 for(i=0;i<6;i++) fprintf(f,"%04x ",s->scroll[i]);
 fprintf(f,"DrvInputs=%04x,%04x,%04x DrvDips=%02x,%02x seibu_coin_input(raw_active_low)=%02x PC_samples(%u)=",
 s->ports[0]|(s->ports[1]<<8),0xffff,s->ports[4]|0xff00,s->ports[2],s->ports[3],s->ports[5],s->sample_count);
 for(i=0;i<8;i++) fprintf(f,"%05x ",s->samples[i]);
 fprintf(f,"\n");
 fprintf(f," output_calls=%u output_indexed=%u output_nonzero=%u output_rgb_nonblack=%u output_dupes=%u (last completed callback buffer)\n",s->output_calls,s->output_indexed,s->output_nonzero,s->output_lit,s->output_dupes);
 fprintf(f," visible_nontransparent_tiles BG=%u MG=%u FG=%u TX=%u SPR=%u\n",s->tiles[0],s->tiles[1],s->tiles[2],s->tiles[3],s->tiles[4]);
 fprintf(f," ordered_events=%u (last 64 retained)\n",s->event_count);
 for(i=s->event_count>64?s->event_count-64:0;i<s->event_count;i++)
  fprintf(f," E %u %04x=%04x@%05x\n",i,s->events[i&63].address,s->events[i&63].value,s->events[i&63].pc);
 for(kind=0;kind<2;kind++) for(i=0;i<0x200;i++) {
  const struct r2_debug_access_record *r = kind ? &s->reads[i] : &s->writes[i];
  if(r->count) fprintf(f," %c %04x n=%u first=%04x@%05x last=%04x@%05x\n",kind?'R':'W',0x400+i*2,r->count,r->first,r->first_pc,r->last,r->last_pc);
 }
 for(kind=0;kind<2;kind++) for(i=0;i<32;i++) {
  const struct r2_debug_access_record *r=kind?&s->sound_reads[i]:&s->sound_writes[i];
  if(r->count) fprintf(f," Z80_%c %04x n=%u first=%02x@%04x last=%02x@%04x\n",kind?'R':'W',0x4000+i,r->count,r->first,r->first_pc,r->last,r->last_pc);
 }
}

static void r2_debug_reset(void)
{
 FILE *f;
 r2_reset_count++;
 if (!r2_log_path[0]) {
  f=r2_debug_open_initial();
  if(f) {
   fprintf(f,"Raiden2 Stage3b SOUND_STRIDE4 %s %s debug=%d force_video=%d\nlog_path=%s\nsave_path=%s\ncontent_path=%s\nROM bank is a preserved copy; CPU1 low 128KB is RAM. R/W rows aggregate COP writes as assembled words, sound accesses as byte+lane. Seibu main: 0700/0704 commands, 0708/070c replies, 0710 IRQ, 0714 pending read, 0718 handshake write. PC samples do not prove an instruction-level loop. bitmap is MAME output, not FBNeo bitmap32.\n",__DATE__,__TIME__,RAIDEN2_DEBUG,RAIDEN2_FORCE_VIDEO_DIAG,r2_log_path,options.libretro_save_path?options.libretro_save_path:"(null)",options.libretro_content_path?options.libretro_content_path:"(null)");
   fflush(f);
   fclose(f);
  }
 }
 f=fopen(r2_log_path,"a");
 if(f) { fprintf(f,"MACHINE_RESET %u at frame %u\n",r2_reset_count,r2_frame_number); fclose(f); }
 memset(&r2_trace,0,sizeof(r2_trace));
 r2_post_frames=120;
 /* cpu_post_run frees reset-scoped timers. Allocate on EVERY machine init,
    never retain a stale timer pointer across a soft reset. */
 timer_pulse(TIME_IN_HZ(55.47*8),0,r2_debug_sample);
}

static void r2_debug_video(struct mame_bitmap *bitmap, const struct rectangle *clip)
{
 static UINT8 litpen[65536];
 unsigned i, p;
 int x,y;
 UINT8 r,g,b;
 unsigned layer,offs,code,bank,cols,rows,ts;
 const UINT8 *ram;
 const struct GfxElement *gfx;
 int sx,sy,scrollx,scrolly;
 for(layer=0;layer<4;layer++) {
  r2_trace.tiles[layer]=0;
  if(r2_video_enable()&(1<<layer)) continue;
  ram=layer==0?back_data:layer==1?mid_data:layer==2?fore_data:videoram;
  bank=layer==0?bg_bank:layer==1?mid_bank:layer==2?fg_bank:tx_bank;
  ts=layer==3?8:16;cols=layer==3?64:32;rows=32;
  gfx=Machine->gfx[layer==3?0:1];
  scrollx=layer==3?0:(r2_scroll[layer*4]|(r2_scroll[layer*4+1]<<8))&511;
  scrolly=layer==3?0:(r2_scroll[layer*4+2]|(r2_scroll[layer*4+3]<<8))&511;
  for(offs=0;offs<cols*rows;offs++) {
   sx=(int)(offs%cols*ts)-scrollx;sy=(int)(offs/cols*ts)-scrolly;
   if(sx<-(int)ts+1) sx+=cols*ts;
   if(sy<-(int)ts+1) sy+=rows*ts;
   if(sx>clip->max_x || sy>clip->max_y || sx+(int)ts<=clip->min_x || sy+(int)ts<=clip->min_y) continue;
   code=((ram[offs*2]|(ram[offs*2+1]<<8))&0xfff)|(bank<<12);
   if(code<gfx->total_elements && gfx->pen_usage && (gfx->pen_usage[code]&~(1U<<15))) r2_trace.tiles[layer]++;
  }
 }
 memset(litpen,0,sizeof(litpen));
 r2_trace.palette=0;
 for(i=0;i<2048;i++) {
  palette_get_color(i,&r,&g,&b);
  if(r|g|b) { r2_trace.palette++; if(Machine->pens[i]<65536) litpen[Machine->pens[i]]=1; }
 }
 r2_trace.pixels=r2_trace.lit=0;
 for(y=clip->min_y;y<=clip->max_y;y++) for(x=clip->min_x;x<=clip->max_x;x++) {
  p=bitmap->depth==32 ? ((UINT32*)bitmap->line[y])[x] : ((UINT16*)bitmap->line[y])[x];
  if(p) r2_trace.pixels++;
  if(bitmap->depth==32 ? (p&0xffffff)!=0 : litpen[p]!=0) r2_trace.lit++;
 }
 r2_trace.draws++;
}

static void r2_debug_frame(void)
{
 FILE *f;
 unsigned i, edge, black, trigger, start;
 struct r2_debug_snapshot *s=&r2_trace;
 s->frame=r2_frame_number++;
 s->pc=activecpu_get_pc(); s->resets=r2_reset_count;
 s->prg=r2_prg_bank; s->cop=r2_cop_bank; s->layers=r2_layer_enable;
 s->banks[0]=bg_bank;s->banks[1]=mid_bank;s->banks[2]=fg_bank;s->banks[3]=tx_bank;
 s->spr=r2_sprites_cur_start;s->mode=r2_cop_dma_mode;
 s->src=r2_cop_dma_src[s->mode];s->dst=r2_cop_dma_dst[s->mode];s->size=r2_cop_dma_size[s->mode];
 s->status=r2_cop_status;s->brightness=r2_pal_brightness_val;
 memcpy(s->regs,r2_cop_regs,sizeof(s->regs));
 for(i=0;i<6;i++) { s->scroll[i]=r2_scroll[i*2]|(r2_scroll[i*2+1]<<8);s->ports[i]=(UINT8)readinputport(i); }
 edge=((s->ports[5]^r2_old_coin)&3)||((s->ports[4]^r2_old_start)&3);
 r2_old_coin=s->ports[5];r2_old_start=s->ports[4];
 black=s->draws!=r2_last_draws ? (s->lit==0) : r2_was_black;
 trigger=edge || (r2_ever_lit && black && !r2_was_black);
 if(s->draws!=r2_last_draws && s->lit) r2_ever_lit=1;
 r2_last_draws=s->draws;r2_was_black=black;
 r2_history[s->frame%31]=*s;
 if(trigger || r2_post_frames || !(s->frame%60)) {
  f=fopen(r2_log_path,"a");
  if(f) {
   if(trigger) {
    start=s->frame>30?s->frame-30:0;
    for(i=start;i<s->frame;i++) r2_debug_print(f,&r2_history[i%31],"PRE");
    r2_post_frames=121;
   }
   r2_debug_print(f,s,edge?"INPUT_EDGE":trigger?"BLACK_EDGE":"FRAME");
   fclose(f);
  }
 }
 if(r2_post_frames) r2_post_frames--;
 memset(s->writes,0,sizeof(s->writes));memset(s->reads,0,sizeof(s->reads));
 memset(s->sound_writes,0,sizeof(s->sound_writes));memset(s->sound_reads,0,sizeof(s->sound_reads));
 s->sample_count=0;memset(s->samples,0,sizeof(s->samples));
 s->event_count=0;
}
#endif
