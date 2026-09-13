/*
 * Raiden II specific Seibu COP simulation backported from the later working
 * MAME/FBNeo implementations.  This is intentionally V30 little-endian and
 * does not reuse the old 68k-oriented generic seicop memory helpers.
 */

#define R2_COP_BASE 0x00400
#define R2_COP_END  0x006ff

static UINT8 r2_copram[0x300];
static UINT32 r2_cop_regs[8], r2_cop_itoa;
static UINT16 r2_cop_status, r2_cop_scale, r2_cop_itoa_digit_count;
static UINT16 r2_cop_angle, r2_cop_dist, r2_cop_angle_target, r2_cop_angle_step;
static UINT8 r2_cop_itoa_digits[10];
static UINT16 r2_cop_dma_mode;
static UINT16 r2_cop_dma_src[0x200], r2_cop_dma_dst[0x200], r2_cop_dma_size[0x200];
static UINT16 r2_cop_dma_v1, r2_cop_dma_v2, r2_cop_dma_adr_rel;
static UINT16 r2_sprites_cur_start;
static UINT16 r2_pal_brightness_val;
static UINT16 r2_cop_bank;
static UINT16 r2_sprite_prot_x, r2_sprite_prot_y, r2_dst1, r2_cop_spr_maxx, r2_cop_spr_off;
static UINT16 r2_sprite_prot_src_addr[2];
static UINT16 r2_cop_hit_status, r2_cop_hit_baseadr, r2_cop_hit_val_stat;
static INT16 r2_cop_hit_val[3];
static UINT32 r2_cop_sort_ram_addr, r2_cop_sort_lookup;
static UINT16 r2_cop_sort_param;

struct r2_collision_info
{
	INT16 pos[3];
	INT8 dx[3];
	UINT8 size[3];
	int allow_swap;
	UINT16 flags_swap;
	UINT32 spradr;
	INT16 min[3], max[3];
};
static struct r2_collision_info r2_collision[2];

static UINT8 r2_mem_r8(UINT32 a)
{
	return (UINT8)cpu_readmem20(a & 0xfffff);
}

static UINT16 r2_mem_r16(UINT32 a)
{
	return (UINT16)(r2_mem_r8(a) | ((UINT16)r2_mem_r8(a + 1) << 8));
}

static UINT32 r2_mem_r32(UINT32 a)
{
	return (UINT32)r2_mem_r16(a) | ((UINT32)r2_mem_r16(a + 2) << 16);
}

static void r2_mem_w8(UINT32 a, UINT8 v)
{
	cpu_writemem20(a & 0xfffff, v);
}

static void r2_mem_w16(UINT32 a, UINT16 v)
{
	r2_mem_w8(a, (UINT8)v);
	r2_mem_w8(a + 1, (UINT8)(v >> 8));
}

static void r2_mem_w32(UINT32 a, UINT32 v)
{
	r2_mem_w16(a, (UINT16)v);
	r2_mem_w16(a + 2, (UINT16)(v >> 16));
}

static UINT16 r2_copram_word(UINT32 addr)
{
	UINT32 o = (addr & 0x7fe) - R2_COP_BASE;
	if (o + 1 >= sizeof(r2_copram)) return 0xffff;
	return (UINT16)(r2_copram[o] | ((UINT16)r2_copram[o + 1] << 8));
}

static void r2_reset_cop(void)
{
	memset(r2_copram, 0, sizeof(r2_copram));
	memset(r2_cop_regs, 0, sizeof(r2_cop_regs));
	r2_cop_itoa = 0;
	r2_cop_status = 0;
	r2_cop_scale = 0;
	r2_cop_itoa_digit_count = 4;
	r2_cop_angle = r2_cop_dist = 0;
	r2_cop_angle_target = r2_cop_angle_step = 0;
	memset(r2_cop_itoa_digits, 0, sizeof(r2_cop_itoa_digits));
	r2_cop_dma_mode = 0;
	memset(r2_cop_dma_src, 0, sizeof(r2_cop_dma_src));
	memset(r2_cop_dma_dst, 0, sizeof(r2_cop_dma_dst));
	memset(r2_cop_dma_size, 0, sizeof(r2_cop_dma_size));
	r2_cop_dma_v1 = r2_cop_dma_v2 = r2_cop_dma_adr_rel = 0;
	r2_sprites_cur_start = 0;
	r2_pal_brightness_val = 0;
	r2_cop_bank = 0;
	r2_sprite_prot_x = r2_sprite_prot_y = r2_dst1 = r2_cop_spr_maxx = r2_cop_spr_off = 0;
	memset(r2_sprite_prot_src_addr, 0, sizeof(r2_sprite_prot_src_addr));
	memset(r2_collision, 0, sizeof(r2_collision));
	r2_cop_hit_status = r2_cop_hit_baseadr = r2_cop_hit_val_stat = 0;
	memset(r2_cop_hit_val, 0, sizeof(r2_cop_hit_val));
	r2_cop_sort_ram_addr = r2_cop_sort_lookup = 0;
	r2_cop_sort_param = 0;
}

static void r2_itoa_compute(void)
{
	int digits = r2_cop_itoa_digit_count >= 2 ? 9 : 1 << (r2_cop_itoa_digit_count * 2);
	UINT32 val = r2_cop_itoa;
	int i;
	if (digits > 9) digits = 9;
	for (i = 0; i < digits; ++i)
	{
		if (!val && i) r2_cop_itoa_digits[i] = 0x20;
		else
		{
			r2_cop_itoa_digits[i] = (UINT8)(0x30 | (val % 10));
			val /= 10;
		}
	}
	r2_cop_itoa_digits[9] = 0;
}

static void r2_sprite_prot_src_write(UINT16 data)
{
	UINT32 src;
	INT32 x, y, w, h;
	UINT16 head1, head2, flag;
	r2_sprite_prot_src_addr[1] = data;
	src = ((UINT32)r2_sprite_prot_src_addr[0] << 4) + r2_sprite_prot_src_addr[1];
	x = (INT16)((INT32)(r2_mem_r32(src + 0x08) >> 16) - (INT32)r2_sprite_prot_x);
	y = (INT16)((INT32)(r2_mem_r32(src + 0x04) >> 16) - (INT32)r2_sprite_prot_y);
	head1 = r2_mem_r16(src + r2_cop_spr_off);
	head2 = r2_mem_r16(src + r2_cop_spr_off + 2);
	w = ((((head1 >> 8) & 7) + 1) << 4);
	h = ((((head1 >> 12) & 7) + 1) << 4);
	flag = (x - w / 2 > -w && x - w / 2 < (INT32)r2_cop_spr_maxx + w &&
	        y - h / 2 > -h && y - h / 2 < 256 + h) ? 1 : 0;
	flag = (r2_mem_r16(src) & 0xfffe) | flag;
	r2_mem_w16(src, flag);
	if (flag & 1)
	{
		r2_mem_w16(r2_dst1, head1);
		r2_mem_w16(r2_dst1 + 2, head2);
		r2_mem_w16(r2_dst1 + 4, (UINT16)(x - w / 2));
		r2_mem_w16(r2_dst1 + 6, (UINT16)(y - h / 2));
		r2_dst1 += 8;
	}
}

static void r2_collision_read_pos(int slot, UINT32 spradr, int allow_swap)
{
	int i;
	r2_collision[slot].allow_swap = allow_swap;
	r2_collision[slot].flags_swap = r2_mem_r16(spradr + 2);
	r2_collision[slot].spradr = spradr;
	for (i = 0; i < 3; ++i) r2_collision[slot].pos[i] = (INT16)r2_mem_r16(spradr + 6 + 4 * i);
}

static void r2_collision_update_hitbox(int slot, UINT32 hitadr, UINT16 data)
{
	UINT32 hitadr2 = r2_mem_r16(hitadr) | ((UINT32)r2_cop_hit_baseadr << 16);
	int num_axis = (data & 0x0100) ? 3 : 2;
	INT16 dx[3], size[3];
	UINT8 res = (num_axis == 3) ? 7 : 3;
	int i;

	for (i = 0; i < 3; ++i)
	{
		r2_collision[slot].dx[i] = 0;
		r2_collision[slot].size[i] = 0;
		dx[i] = size[i] = 0;
	}
	for (i = 0; i < num_axis; ++i)
	{
		r2_collision[slot].dx[i] = (INT8)r2_mem_r8(hitadr2++);
		r2_collision[slot].size[i] = r2_mem_r8(hitadr2++);
		dx[i] = r2_collision[slot].dx[i];
		size[i] = r2_collision[slot].size[i];
	}

	for (i = 0; i < num_axis; ++i)
	{
		if (r2_collision[slot].allow_swap && (r2_collision[slot].flags_swap & (1 << i)))
		{
			r2_collision[slot].max[i] = r2_collision[slot].pos[i] - dx[i];
			r2_collision[slot].min[i] = r2_collision[slot].max[i] - size[i];
		}
		else
		{
			r2_collision[slot].min[i] = r2_collision[slot].pos[i] + dx[i];
			r2_collision[slot].max[i] = r2_collision[slot].min[i] + size[i];
		}
		if (r2_collision[0].max[i] > r2_collision[1].min[i] && r2_collision[0].min[i] < r2_collision[1].max[i]) res &= ~(1 << i);
		if (r2_collision[1].max[i] > r2_collision[0].min[i] && r2_collision[1].min[i] < r2_collision[0].max[i]) res &= ~(1 << i);
		r2_cop_hit_val[i] = r2_collision[0].pos[i] - r2_collision[1].pos[i];
	}
	r2_cop_hit_val_stat = res;
	r2_cop_hit_status = res;
}

static void r2_cop_command(int offset, UINT16 data)
{
	INT32 dx, dy, delta, div, raw_angle, amp, res;
	UINT32 ppos, npos, v1, v2, v3;
	double angle;
	offset /= 2;
	r2_cop_status &= 0x7fff;

	switch (data)
	{
		case 0x0205:
			ppos = r2_mem_r32(r2_cop_regs[0] + 4 + offset * 4);
			npos = ppos + r2_mem_r32(r2_cop_regs[0] + 0x10 + offset * 4);
			delta = (INT32)(INT16)(npos >> 16) - (INT32)(INT16)(ppos >> 16);
			r2_mem_w32(r2_cop_regs[0] + 4 + offset * 4, npos);
			r2_mem_w16(r2_cop_regs[0] + 0x1e + offset * 4,
			             (UINT16)(r2_mem_r16(r2_cop_regs[0] + 0x1e + offset * 4) + delta));
			break;
		case 0x0904:
			r2_mem_w32(r2_cop_regs[0] + 16 + offset * 4,
			             r2_mem_r32(r2_cop_regs[0] + 16 + offset * 4) - r2_mem_r32(r2_cop_regs[0] + 0x28 + offset * 4));
			break;
		case 0x0905:
			r2_mem_w32(r2_cop_regs[0] + 16 + offset * 4,
			             r2_mem_r32(r2_cop_regs[0] + 16 + offset * 4) + r2_mem_r32(r2_cop_regs[0] + 0x28 + offset * 4));
			break;
		case 0x130e: case 0x138e: case 0x330e: case 0x338e:
			dx = (INT32)r2_mem_r32(r2_cop_regs[1] + 4) - (INT32)r2_mem_r32(r2_cop_regs[0] + 4);
			dy = (INT32)r2_mem_r32(r2_cop_regs[1] + 8) - (INT32)r2_mem_r32(r2_cop_regs[0] + 8);
			if (!dy) { r2_cop_status |= 0x8000; r2_cop_angle = 0; }
			else { r2_cop_angle = (UINT16)((INT32)(atan((double)dx / (double)dy) * 128.0 / M_PI)); if (dy < 0) r2_cop_angle += 0x80; }
			if (data & 0x0080) r2_mem_w8(r2_cop_regs[0] + 0x34, (UINT8)r2_cop_angle);
			break;
		case 0x2208: case 0x2288:
			dx = (INT16)r2_mem_r16(r2_cop_regs[0] + 0x12);
			dy = (INT16)r2_mem_r16(r2_cop_regs[0] + 0x16);
			if (!dy) { r2_cop_status |= 0x8000; r2_cop_angle = 0; }
			else { r2_cop_angle = (UINT16)((INT32)(atan((double)dx / (double)dy) * 128.0 / M_PI)); if (dy < 0) r2_cop_angle += 0x80; }
			if (data & 0x0080) r2_mem_w8(r2_cop_regs[0] + 0x34, (UINT8)r2_cop_angle);
			break;
		case 0x2a05:
			delta = (INT16)r2_mem_r16(r2_cop_regs[1] + 0x1e + offset * 4);
			r2_mem_w16(r2_cop_regs[0] + 6 + offset * 4, (UINT16)(r2_mem_r16(r2_cop_regs[0] + 6 + offset * 4) + delta));
			r2_mem_w16(r2_cop_regs[0] + 0x1e + offset * 4, (UINT16)(r2_mem_r16(r2_cop_regs[0] + 0x1e + offset * 4) + delta));
			break;
		case 0x39b0: case 0x3b30: case 0x3bb0:
			dx = ((INT32)r2_mem_r32(r2_cop_regs[1] + 4) - (INT32)r2_mem_r32(r2_cop_regs[0] + 4)) >> 16;
			dy = ((INT32)r2_mem_r32(r2_cop_regs[1] + 8) - (INT32)r2_mem_r32(r2_cop_regs[0] + 8)) >> 16;
			r2_cop_dist = (UINT16)sqrt((double)dx * dx + (double)dy * dy);
			if (data & 0x0080) r2_mem_w16(r2_cop_regs[0] + ((data & 0x0200) ? 0x3a : 0x38), r2_cop_dist);
			break;
		case 0x42c2:
			div = r2_mem_r16(r2_cop_regs[0] + 0x36); if (!div) div = 1;
			r2_cop_status = 7;
			r2_mem_w16(r2_cop_regs[0] + 0x38, (UINT16)((r2_cop_dist << (5 - r2_cop_scale)) / div));
			break;
		case 0x4aa0:
			div = r2_mem_r16(r2_cop_regs[0] + 0x38); if (!div) div = 1;
			r2_cop_status = 7;
			r2_mem_w16(r2_cop_regs[0] + 0x36, (UINT16)((r2_cop_dist << (5 - r2_cop_scale)) / div));
			break;
		case 0x6200:
			raw_angle = r2_mem_r8(r2_cop_regs[0] + 0x34);
			r2_cop_angle_target &= 0xff; r2_cop_angle_step &= 0xff;
			v1 = r2_mem_r16(r2_cop_regs[0]) & ~0x0004U;
			delta = raw_angle - r2_cop_angle_target;
			if (delta >= 128) delta -= 256; else if (delta < -128) delta += 256;
			if (delta < 0) { if (delta >= -(INT32)r2_cop_angle_step) { raw_angle = r2_cop_angle_target; v1 |= 4; } else raw_angle += r2_cop_angle_step; }
			else { if (delta <= (INT32)r2_cop_angle_step) { raw_angle = r2_cop_angle_target; v1 |= 4; } else raw_angle -= r2_cop_angle_step; }
			r2_mem_w16(r2_cop_regs[0], (UINT16)v1); r2_mem_w8(r2_cop_regs[0] + 0x34, (UINT8)raw_angle);
			break;
		case 0x8100:
			raw_angle = r2_mem_r16(r2_cop_regs[0] + 0x34) & 0xff;
			angle = raw_angle * M_PI / 128.0;
			amp = (65536 >> 5) * (r2_mem_r16(r2_cop_regs[0] + 0x36) & 0xff);
			if (raw_angle == 0xc0) amp *= 2;
			res = ((INT32)(amp * sin(angle))) << r2_cop_scale;
			r2_mem_w32(r2_cop_regs[0] + 16, (UINT32)res);
			break;
		case 0x8900:
			raw_angle = r2_mem_r16(r2_cop_regs[0] + 0x34) & 0xff;
			angle = raw_angle * M_PI / 128.0;
			amp = (65536 >> 5) * (r2_mem_r16(r2_cop_regs[0] + 0x36) & 0xff);
			if (raw_angle == 0x80) amp *= 2;
			res = ((INT32)(amp * cos(angle))) << r2_cop_scale;
			r2_mem_w32(r2_cop_regs[0] + 20, (UINT32)res);
			break;
		case 0x5205: case 0x5a05:
			r2_mem_w32(r2_cop_regs[1], r2_mem_r32(r2_cop_regs[0]));
			break;
		case 0xf205:
			r2_mem_w32(r2_cop_regs[2], r2_mem_r32(r2_cop_regs[0] + 4));
			break;
		case 0xa100: case 0xa180:
			r2_collision_read_pos(0, r2_cop_regs[0], data & 0x0080); break;
		case 0xa900: case 0xa980:
			r2_collision_read_pos(1, r2_cop_regs[1], data & 0x0080); break;
		case 0xb100:
			r2_collision_update_hitbox(0, r2_cop_regs[2], data); break;
		case 0xb900:
			r2_collision_update_hitbox(1, r2_cop_regs[3], data); break;
		case 0xede5:
			v1 = r2_mem_r32(r2_cop_regs[4] + 8); v2 = r2_mem_r32(r2_cop_regs[4] + 12); v3 = r2_mem_r32(r2_cop_regs[4] + 16);
			if (v3) r2_mem_w16(r2_cop_regs[4] + 22, (UINT16)((v1 + v2) / v3));
			break;
		case 0xf790:
			r2_cop_dist = (UINT16)sqrt((double)r2_mem_r32(r2_cop_regs[4] + 8)); break;
		case 0xfc84:
			r2_mem_w32(r2_cop_regs[4] + 8, r2_mem_r32(r2_cop_regs[4]) - r2_mem_r32(r2_cop_regs[4] + 4)); break;
		default:
			break;
	}
}

static UINT8 r2_fade_table(UINT16 v)
{
	int low = v & 0x1f;
	int high = v & 0x3e0;
	return (UINT8)((low * (high | (high >> 5)) + 0x210) >> 10);
}

static void r2_cop_dma_trigger(void)
{
	UINT32 src, dst, size, i, address, length;
	UINT16 pal_val, w;
	int r, g, b, rt, gt, bt;
	int rsize;
	UINT16 mode = r2_cop_dma_mode & 0x1ff;

	switch (mode)
	{
		case 0x14:
			rsize = ((0x80 - r2_cop_dma_size[mode]) & 0x7f) + 1;
			r2_sprites_cur_start = (UINT16)(0x1000 - (rsize << 5));
			break;
		case 0x82:
			src = (UINT32)r2_cop_dma_src[mode] << 6;
			dst = (UINT32)r2_cop_dma_dst[mode] << 6;
			size = (((UINT32)r2_cop_dma_size[mode] << 5) - ((UINT32)r2_cop_dma_dst[mode] << 6) + 0x20) / 2;
			for (i = 0; i < size; ++i)
			{
				w = r2_mem_r16(src + (r2_cop_dma_adr_rel * 0x400));
				bt = r2_fade_table(((w & 0x7c00) >> 5) | (r2_pal_brightness_val ^ 0));
				w = r2_mem_r16(src);
				b = r2_fade_table(((w & 0x7c00) >> 5) | (r2_pal_brightness_val ^ 0x1f));
				pal_val = (UINT16)(((b + bt) & 0x1f) << 10);
				w = r2_mem_r16(src + (r2_cop_dma_adr_rel * 0x400));
				gt = r2_fade_table((w & 0x03e0) | (r2_pal_brightness_val ^ 0));
				w = r2_mem_r16(src);
				g = r2_fade_table((w & 0x03e0) | (r2_pal_brightness_val ^ 0x1f));
				pal_val |= (UINT16)(((g + gt) & 0x1f) << 5);
				w = r2_mem_r16(src + (r2_cop_dma_adr_rel * 0x400));
				rt = r2_fade_table(((w & 0x001f) << 5) | (r2_pal_brightness_val ^ 0));
				w = r2_mem_r16(src);
				r = r2_fade_table(((w & 0x001f) << 5) | (r2_pal_brightness_val ^ 0x1f));
				pal_val |= (UINT16)((r + rt) & 0x1f);
				r2_mem_w16(dst, pal_val);
				src += 2; dst += 2;
			}
			break;
		case 0x09:
			src = (UINT32)r2_cop_dma_src[mode] << 6;
			dst = (UINT32)r2_cop_dma_dst[mode] << 6;
			size = (((UINT32)r2_cop_dma_size[mode] << 5) - ((UINT32)r2_cop_dma_dst[mode] << 6) + 0x20) / 2;
			for (i = 0; i < size; ++i) { r2_mem_w16(dst, r2_mem_r16(src)); src += 2; dst += 2; }
			break;
		case 0x118: case 0x11f:
			if (r2_cop_dma_dst[mode] != 0) break;
			address = (UINT32)r2_cop_dma_src[mode] << 6;
			length = ((UINT32)r2_cop_dma_size[mode] + 1) << 5;
			for (i = address; i < address + length; i += 4)
				r2_mem_w32(i, (UINT32)r2_cop_dma_v1 | ((UINT32)r2_cop_dma_v2 << 16));
			break;
		default:
			break;
	}
}

static void r2_cop_sort_trigger(UINT16 count)
{
	UINT32 i, j;
	UINT16 a, b;
	UINT32 ka, kb;
	if (r2_cop_sort_param != 1 && r2_cop_sort_param != 2) return;
	/* Stable insertion: equal depth entries must retain upload order,
	   as in FBNeo's stable merge. No STL, allocation or host-endian casts. */
	for (i = 1; i < count; ++i)
	{
		a = r2_mem_r16(r2_cop_sort_lookup + i * 2);
		ka = r2_mem_r32(r2_cop_sort_ram_addr + a);
		for (j = i; j > 0; --j)
		{
			b = r2_mem_r16(r2_cop_sort_lookup + (j - 1) * 2);
			kb = r2_mem_r32(r2_cop_sort_ram_addr + b);
			if (r2_cop_sort_param == 1 ? kb <= ka : kb >= ka) break;
			r2_mem_w16(r2_cop_sort_lookup + j * 2, b);
		}
		r2_mem_w16(r2_cop_sort_lookup + j * 2, a);
	}
}

static void r2_crtc_write(UINT32 addr, UINT8 data)
{
	UINT32 o = addr & 0x7f;
	if ((o & 0x7e) == 0x1c)
	{
		if (o & 1) r2_layer_enable = (r2_layer_enable & 0x00ff) | ((UINT16)data << 8);
		else r2_layer_enable = (r2_layer_enable & 0xff00) | data;
	}
	if (o >= 0x20 && o <= 0x2b) r2_scroll[o & 0x0f] = data;
}

static void r2_main_bankswitch(UINT16 data)
{
	UINT8 *rom = r2_bank_rom;
	int bb = (~(data >> 15)) & 1;
	r2_prg_bank = data;
	cpu_setbank(3, rom + bb * 0x20000);
	cpu_setbank(4, rom + bb * 0x20000 + 0x10000);
	if (cpu_getactivecpu() == 0) r2_debug_bank();
}

static READ_HANDLER(r2_cop_r)
{
	UINT32 addr = R2_COP_BASE + offset;
	UINT16 ret;
	UINT32 pair = addr & 0x7fe;
	ret = r2_copram_word(pair);
	switch (pair)
	{
		case 0x580: ret = r2_cop_hit_status; break;
		case 0x582: case 0x584: case 0x586: ret = (UINT16)r2_cop_hit_val[(pair - 0x582) / 2]; break;
		case 0x588: ret = r2_cop_hit_val_stat; break;
		case 0x590: case 0x592: case 0x594: case 0x596: case 0x598:
		{
			int d = pair & 0x0e;
			ret = r2_cop_itoa_digits[d] | ((UINT16)r2_cop_itoa_digits[d + 1] << 8);
			break;
		}
		case 0x5b0: ret = r2_cop_status; break;
		case 0x5b2: ret = r2_cop_dist; break;
		case 0x5b4: ret = r2_cop_angle; break;
		case 0x6c0: ret = r2_cop_spr_off; break;
		case 0x6c2: ret = r2_sprite_prot_src_addr[0]; break;
		case 0x6dc: ret = r2_cop_spr_maxx; break;
		default: break;
	}
	r2_debug_access(addr, ret & (0xff << ((addr & 1)*8)), 0);
	return (ret >> ((addr & 1) * 8)) & 0xff;
}

static WRITE_HANDLER(r2_cop_w)
{
	UINT32 addr = R2_COP_BASE + offset;
	UINT32 o = addr - R2_COP_BASE;
	UINT32 pair = addr & 0x7fe;
	UINT16 w;
	int idx;
	if (o < sizeof(r2_copram)) r2_copram[o] = data;
	r2_debug_access(addr, r2_copram_word(pair), 1);
	if (addr >= 0x600 && addr <= 0x64f) r2_crtc_write(addr, (UINT8)data);
	if (!(addr & 1)) return;
	w = r2_copram_word(pair);

	switch (pair)
	{
		case 0x41c: r2_cop_angle_target = w; break;
		case 0x41e: r2_cop_angle_step = w; break;
		case 0x420: r2_cop_itoa = (r2_cop_itoa & 0xffff0000U) | w; r2_itoa_compute(); break;
		case 0x422: r2_cop_itoa = (r2_cop_itoa & 0x0000ffffU) | ((UINT32)w << 16); break;
		case 0x424: r2_cop_itoa_digit_count = w; break;
		case 0x428: r2_cop_dma_v1 = w; break;
		case 0x42a: r2_cop_dma_v2 = w; break;
		case 0x436: r2_cop_hit_baseadr = w; break;
		case 0x444: r2_cop_scale = w & 3; break;
		case 0x450: r2_cop_sort_ram_addr = (r2_cop_sort_ram_addr & 0x0000ffffU) | ((UINT32)w << 16); break;
		case 0x452: r2_cop_sort_ram_addr = (r2_cop_sort_ram_addr & 0xffff0000U) | w; break;
		case 0x454: r2_cop_sort_lookup = (r2_cop_sort_lookup & 0x0000ffffU) | ((UINT32)w << 16); break;
		case 0x456: r2_cop_sort_lookup = (r2_cop_sort_lookup & 0xffff0000U) | w; break;
		case 0x458: r2_cop_sort_param = w; break;
		case 0x45a: r2_pal_brightness_val = w; break;
		case 0x45c: break;
		case 0x470:
			r2_cop_bank = w;
			if (fg_bank != ((r2_cop_bank >> 14) | 4)) { fg_bank = (r2_cop_bank >> 14) | 4; if (foreground_layer) tilemap_mark_all_tiles_dirty(foreground_layer); }
			break;
		case 0x476: r2_cop_dma_adr_rel = w; break;
		case 0x478: if (r2_cop_dma_mode < 0x200) r2_cop_dma_src[r2_cop_dma_mode] = w; break;
		case 0x47a: if (r2_cop_dma_mode < 0x200) r2_cop_dma_size[r2_cop_dma_mode] = w; break;
		case 0x47c: if (r2_cop_dma_mode < 0x200) r2_cop_dma_dst[r2_cop_dma_mode] = w; break;
		case 0x47e: r2_cop_dma_mode = w & 0x1ff; break;
		case 0x4a0: case 0x4a2: case 0x4a4: case 0x4a6: case 0x4a8: case 0x4aa: case 0x4ac:
			idx = (pair & 0x0f) / 2; r2_cop_regs[idx] = (r2_cop_regs[idx] & 0x0000ffffU) | ((UINT32)w << 16); break;
		case 0x4c0: case 0x4c2: case 0x4c4: case 0x4c6: case 0x4c8: case 0x4ca: case 0x4cc:
			idx = (pair & 0x0f) / 2; r2_cop_regs[idx] = (r2_cop_regs[idx] & 0xffff0000U) | w; break;
		case 0x500: case 0x502: case 0x504: r2_cop_command(pair & 0x7, w); break;
		case 0x6c0: r2_cop_spr_off = w; break;
		case 0x6c2: r2_sprite_prot_src_addr[0] = w; break;
		case 0x6c6: r2_dst1 = w; break;
		case 0x6ca: r2_main_bankswitch(w); break;
		case 0x6cc:
		{
			int nb = (w & 1) << 1;
			int nm = (w & 2) | 1;
			if (bg_bank != nb) { bg_bank = nb; if (background_layer) tilemap_mark_all_tiles_dirty(background_layer); }
			if (mid_bank != nm) { mid_bank = nm; if (midground_layer) tilemap_mark_all_tiles_dirty(midground_layer); }
			break;
		}
		case 0x6d8: r2_sprite_prot_x = w; break;
		case 0x6da: r2_sprite_prot_y = w; break;
		case 0x6dc: r2_cop_spr_maxx = w; break;
		case 0x6de: r2_sprite_prot_src_write(w); break;
		case 0x6fc: r2_cop_dma_trigger(); break;
		case 0x6fe: r2_cop_sort_trigger(w); break;
		default: break;
	}
}

static READ_HANDLER(r2_dst1_r)
{
	return (r2_dst1 >> (offset * 8)) & 0xff;
}
