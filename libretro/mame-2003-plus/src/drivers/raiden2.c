/*
   Raiden II driver - Salvia/MAME2003+ playability backport

   This is a targeted backport of the later working Raiden II emulation to
   the MAME 0.78-style API used by Salvia's mame2003-plus core.

   Key additions over the old preliminary driver:
   - V30 program banking
   - Raiden II COP/protection command simulation
   - sprite protection/DMA/sort helpers
   - correct tile banks, CRTC scroll/layer state
   - correct 4-word sprite format + priority passes
   - host-independent Raiden II sprite ROM interleave/decryption
   - Seibu YM2151 + dual OKIM6295 sound path
   - correct input/system/coin layout and sane DIP defaults

   Source basis: later MAME Raiden II driver/r2crypt and current FBNeo's
   Raiden II COP implementation, adapted to this old core API.
*/

#include "driver.h"
#include "vidhrdw/generic.h"
#include "cpu/z80/z80.h"
#include "sndhrdw/seibu.h"
#include "sound/2151intf.h"
#include "sound/adpcm.h"
#include "state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* seibu_pending_w exists in sndhrdw/seibu.c but is not declared by the old header */
WRITE_HANDLER( seibu_pending_w );

static struct tilemap *background_layer,*midground_layer,*foreground_layer,*text_layer;
static unsigned char *back_data,*fore_data,*mid_data;
static int bg_bank, mid_bank, fg_bank, tx_bank;
static UINT8 r2_scroll[12];
static UINT16 r2_layer_enable;
static UINT16 r2_prg_bank;
/* The old memory system uses REGION_CPU1[0..1ffff] as writable RAM.
   Preserve both switchable ROM pages before the first CPU/reset runs. */
static UINT8 *r2_bank_rom;

#include "raiden2_debug.h"

#include "raiden2_r2crypt.inc"
#include "raiden2_cop.inc"
#include "raiden2_debug.inc"
#include "raiden2_sound.inc"

/* -----------------------------------------------------------------------
 * Video
 * ----------------------------------------------------------------------- */

static UINT16 r2_spr_word(const UINT8 *p)
{
	return (UINT16)(p[0] | ((UINT16)p[1] << 8));
}

static void r2_draw_one_sprite(struct mame_bitmap *bitmap, const struct rectangle *cliprect,
                               int tile, int color, int flipx, int flipy, int sx, int sy)
{
	/* Avoid general drawgfx setup for invisible wraparound copies. */
	if (sx > cliprect->max_x || sy > cliprect->max_y ||
	    sx + 15 < cliprect->min_x || sy + 15 < cliprect->min_y) return;
#if RAIDEN2_DEBUG
	if(sx<=cliprect->max_x && sy<=cliprect->max_y && sx+16>cliprect->min_x && sy+16>cliprect->min_y &&
	   Machine->gfx[2]->pen_usage && (Machine->gfx[2]->pen_usage[tile&0xffff]&~(1U<<15))) r2_trace.tiles[4]++;
#endif
	drawgfx(bitmap, Machine->gfx[2], tile & 0xffff, color & 0x3f,
	        flipx, flipy, sx, sy, cliprect, TRANSPARENCY_PEN, 15);
}

static void draw_sprites(struct mame_bitmap *bitmap, const struct rectangle *cliprect, int priority)
{
	const UINT8 *base = spriteram;
	const UINT8 *source;
	int start;

	if (!spriteram || !Machine->gfx[2]) return;
	if (r2_video_enable() & 0x10) return;

	start = r2_sprites_cur_start;
	if (start > 0xff8) start = 0xff8;
	start &= ~7;
	source = base + start;

	while (source >= base)
	{
		UINT16 a = r2_spr_word(source + 0);
		int tile_number = r2_spr_word(source + 2);
		int sx = r2_spr_word(source + 4);
		int sy = r2_spr_word(source + 6);
		int ytlim = ((a >> 12) & 7) + 1;
		int xtlim = ((a >> 8) & 7) + 1;
		int xflip = (a >> 15) & 1;
		int yflip = (a >> 11) & 1;
		int colr = a & 0x3f;
		int pri = (a >> 6) & 3;
		int xstep = 16, ystep = 16;
		int xt, yt;

		if (pri == priority)
		{
			if (xflip) { ystep = -16; sy += ytlim * 16 - 16; }
			if (yflip) { xstep = -16; sx += xtlim * 16 - 16; }

			for (xt = 0; xt < xtlim; ++xt)
			{
				for (yt = 0; yt < ytlim; ++yt)
				{
					int x = (sx + xstep * xt) & 0x1ff;
					int y = (sy + ystep * yt) & 0x1ff;
					r2_draw_one_sprite(bitmap, cliprect, tile_number, colr, yflip, xflip, x, y);
					r2_draw_one_sprite(bitmap, cliprect, tile_number, colr, yflip, xflip, x - 0x200, y);
					r2_draw_one_sprite(bitmap, cliprect, tile_number, colr, yflip, xflip, x, y - 0x200);
					r2_draw_one_sprite(bitmap, cliprect, tile_number, colr, yflip, xflip, x - 0x200, y - 0x200);
					tile_number++;
				}
			}
		}

		if (source == base) break;
		source -= 8;
	}
}

WRITE_HANDLER( raiden2_background_w )
{
	if (back_data[offset] == (UINT8)data) return;
	back_data[offset] = data;
	tilemap_mark_tile_dirty(background_layer, offset / 2);
}

WRITE_HANDLER( raiden2_midground_w )
{
	if (mid_data[offset] == (UINT8)data) return;
	mid_data[offset] = data;
	tilemap_mark_tile_dirty(midground_layer, offset / 2);
}

WRITE_HANDLER( raiden2_foreground_w )
{
	if (fore_data[offset] == (UINT8)data) return;
	fore_data[offset] = data;
	tilemap_mark_tile_dirty(foreground_layer, offset / 2);
}

WRITE_HANDLER( raiden2_text_w )
{
	if (videoram[offset] == (UINT8)data) return;
	videoram[offset] = data;
	tilemap_mark_tile_dirty(text_layer, offset / 2);
}

static void get_back_tile_info(int tile_index)
{
	int offs = tile_index * 2;
	int v = back_data[offs] | (back_data[offs + 1] << 8);
	int color = (v >> 12) & 0x0f;
	int tile = (v & 0x0fff) | (bg_bank << 12);
	SET_TILE_INFO(1, tile, color, 0)
}

static void get_mid_tile_info(int tile_index)
{
	int offs = tile_index * 2;
	int v = mid_data[offs] | (mid_data[offs + 1] << 8);
	int color = ((v >> 12) & 0x0f) + 0x20;
	int tile = (v & 0x0fff) | (mid_bank << 12);
	SET_TILE_INFO(1, tile, color, 0)
}

static void get_fore_tile_info(int tile_index)
{
	int offs = tile_index * 2;
	int v = fore_data[offs] | (fore_data[offs + 1] << 8);
	int color = ((v >> 12) & 0x0f) + 0x10;
	int tile = (v & 0x0fff) | (fg_bank << 12);
	SET_TILE_INFO(1, tile, color, 0)
}

static void get_text_tile_info(int tile_index)
{
	int offs = tile_index * 2;
	int v = videoram[offs] | (videoram[offs + 1] << 8);
	int color = (v >> 12) & 0x0f;
	int tile = (v & 0x0fff) | (tx_bank << 12);
	SET_TILE_INFO(0, tile, color, 0)
}

VIDEO_START(raiden2)
{
	text_layer       = tilemap_create(get_text_tile_info, tilemap_scan_rows, TILEMAP_TRANSPARENT, 8, 8, 64, 32);
	background_layer = tilemap_create(get_back_tile_info, tilemap_scan_rows, TILEMAP_TRANSPARENT, 16, 16, 32, 32);
	midground_layer  = tilemap_create(get_mid_tile_info, tilemap_scan_rows, TILEMAP_TRANSPARENT, 16, 16, 32, 32);
	foreground_layer = tilemap_create(get_fore_tile_info, tilemap_scan_rows, TILEMAP_TRANSPARENT, 16, 16, 32, 32);
	if (!text_layer || !background_layer || !midground_layer || !foreground_layer) return 1;
	tilemap_set_transparent_pen(background_layer, 15);
	tilemap_set_transparent_pen(midground_layer, 15);
	tilemap_set_transparent_pen(foreground_layer, 15);
	tilemap_set_transparent_pen(text_layer, 15);
	return 0;
}

VIDEO_UPDATE(raiden2)
{
	int sx, sy;
#if RAIDEN2_DEBUG
	r2_trace.tiles[4]=0;
#endif
	fillbitmap(bitmap, get_black_pen(), cliprect);

	sx = r2_scroll[0] | (r2_scroll[1] << 8); sy = r2_scroll[2] | (r2_scroll[3] << 8);
	tilemap_set_scrollx(background_layer, 0, sx & 0x1ff); tilemap_set_scrolly(background_layer, 0, sy & 0x1ff);
	sx = r2_scroll[4] | (r2_scroll[5] << 8); sy = r2_scroll[6] | (r2_scroll[7] << 8);
	tilemap_set_scrollx(midground_layer, 0, sx & 0x1ff); tilemap_set_scrolly(midground_layer, 0, sy & 0x1ff);
	sx = r2_scroll[8] | (r2_scroll[9] << 8); sy = r2_scroll[10] | (r2_scroll[11] << 8);
	tilemap_set_scrollx(foreground_layer, 0, sx & 0x1ff); tilemap_set_scrolly(foreground_layer, 0, sy & 0x1ff);

	/* Priority order follows the later working implementation. */
	draw_sprites(bitmap, cliprect, 0);
	if (!(r2_video_enable() & 0x01)) tilemap_draw(bitmap, cliprect, background_layer, 0, 0);
	draw_sprites(bitmap, cliprect, 1);
	if (!(r2_video_enable() & 0x02)) tilemap_draw(bitmap, cliprect, midground_layer, 0, 0);
	draw_sprites(bitmap, cliprect, 2);
	if (!(r2_video_enable() & 0x04)) tilemap_draw(bitmap, cliprect, foreground_layer, 0, 0);
	draw_sprites(bitmap, cliprect, 3);
	if (!(r2_video_enable() & 0x08)) tilemap_draw(bitmap, cliprect, text_layer, 0, 0);
	r2_debug_video(bitmap, cliprect);
}

/* -----------------------------------------------------------------------
 * Main CPU memory
 * ----------------------------------------------------------------------- */

static READ_HANDLER(r2_openbus_r) { return 0xff; }

static READ_HANDLER(r2_inputs_r)
{
	int value = 0xff;
	switch (offset) {
	case 0: value=readinputport(2); break;
	case 1: value=readinputport(3); break;
	case 4: value=readinputport(0); break;
	case 5: value=readinputport(1); break;
	case 12: value=readinputport(4); break;
	}
	r2_debug_access(0x740+offset, (value&0xff)<<((offset&1)*8), 0);
	return value;
}

static MEMORY_READ_START( raiden2_readmem )
	{ 0x00000, 0x003ff, MRA_RAM },
	{ 0x00400, 0x006ff, r2_cop_r },
	{ 0x00700, 0x0071f, r2_sound_main_r },
	{ 0x00740, 0x0074d, r2_inputs_r },
	{ 0x00762, 0x00763, r2_dst1_r },
	{ 0x00000, 0x007ff, MRA_RAM }, /* unassigned low I/O shadow, as on the Vez map */
	{ 0x00800, 0x0bfff, MRA_RAM },
	{ 0x0c000, 0x0cfff, MRA_RAM },
	{ 0x0d000, 0x0d7ff, MRA_RAM },
	{ 0x0d800, 0x0dfff, MRA_RAM },
	{ 0x0e000, 0x0e7ff, MRA_RAM },
	{ 0x0e800, 0x0f7ff, MRA_RAM },
	{ 0x0f800, 0x0ffff, MRA_RAM },
	{ 0x10000, 0x1efff, MRA_RAM },
	{ 0x1f000, 0x1ffff, MRA_RAM },
	{ 0x20000, 0x2ffff, MRA_BANK3 },
	{ 0x30000, 0x3ffff, MRA_BANK4 },
	{ 0x40000, 0xfffff, MRA_ROM },
MEMORY_END

static MEMORY_WRITE_START( raiden2_writemem )
	{ 0x00000, 0x003ff, MWA_RAM },
	{ 0x00400, 0x006ff, r2_cop_w },
	{ 0x0068e, 0x0068f, MWA_NOP },
	{ 0x00700, 0x0071f, r2_sound_main_w },
	{ 0x00000, 0x007ff, MWA_RAM },
	{ 0x00800, 0x0bfff, MWA_RAM },
	{ 0x0c000, 0x0cfff, MWA_RAM, &spriteram, &spriteram_size },
	{ 0x0d000, 0x0d7ff, raiden2_background_w, &back_data },
	{ 0x0d800, 0x0dfff, raiden2_foreground_w, &fore_data },
	{ 0x0e000, 0x0e7ff, raiden2_midground_w, &mid_data },
	{ 0x0e800, 0x0f7ff, raiden2_text_w, &videoram },
	{ 0x0f800, 0x0ffff, MWA_RAM },
	{ 0x10000, 0x1efff, MWA_RAM },
	{ 0x1f000, 0x1ffff, paletteram_xBBBBBGGGGGRRRRR_w, &paletteram },
	{ 0x20000, 0xfffff, MWA_ROM },
MEMORY_END

/* -----------------------------------------------------------------------
 * Sound CPU: YM2151 + dual OKIM6295
 * ----------------------------------------------------------------------- */

static READ_HANDLER(r2_sound_z80_r)
{
	int value;
	if(offset<2) value=seibu_soundlatch_r(offset);
	else if(offset==2) value=seibu_main_data_pending_r(0);
	else value=readinputport(5);
	r2_debug_sound_access(0x4010+offset,value,0);
	return value;
}

static WRITE_HANDLER(r2_sound_z80_reply_w)
{
	seibu_main_data_w(offset,data);
	r2_debug_sound_access(0x4018+offset,data,1);
}

static WRITE_HANDLER(r2_sound_z80_pending_w)
{
	seibu_pending_w(offset,data);
	r2_debug_sound_access(0x4000,data,1);
}

static MEMORY_READ_START( raiden2_sound_readmem )
	{ 0x0000, 0x1fff, MRA_ROM },
	{ 0x2000, 0x27ff, MRA_RAM },
	{ 0x4009, 0x4009, YM2151_status_port_0_r },
	{ 0x4010, 0x4013, r2_sound_z80_r },
	{ 0x6000, 0x6000, OKIM6295_status_0_r },
	{ 0x6002, 0x6002, OKIM6295_status_1_r },
	{ 0x8000, 0xffff, MRA_BANK1 },
MEMORY_END

static MEMORY_WRITE_START( raiden2_sound_writemem )
	{ 0x0000, 0x1fff, MWA_ROM },
	{ 0x2000, 0x27ff, MWA_RAM },
	{ 0x4000, 0x4000, r2_sound_z80_pending_w },
	{ 0x4001, 0x4001, seibu_irq_clear_w },
	{ 0x4002, 0x4002, seibu_rst10_ack_w },
	{ 0x4003, 0x4003, seibu_rst18_ack_w },
	{ 0x4007, 0x4007, seibu_bank_w },
	{ 0x4008, 0x4008, YM2151_register_port_0_w },
	{ 0x4009, 0x4009, YM2151_data_port_0_w },
	{ 0x4018, 0x4019, r2_sound_z80_reply_w },
	{ 0x401a, 0x401a, seibu_bank_w },
	{ 0x401b, 0x401b, seibu_coin_w },
	{ 0x6000, 0x6000, OKIM6295_data_0_w },
	{ 0x6002, 0x6002, OKIM6295_data_1_w },
	{ 0x8000, 0xffff, MWA_ROM },
MEMORY_END

static struct YM2151interface raiden2_ym2151_interface =
{
	1,
	28636360/8,
	{ YM3012_VOL(50,MIXER_PAN_LEFT,50,MIXER_PAN_RIGHT) },
	{ seibu_ym2151_irqhandler }
};

static struct OKIM6295interface raiden2_oki_interface =
{
	2,
	{ (28636360/28)/132, (28636360/28)/132 },
	{ REGION_SOUND1, REGION_SOUND2 },
	{ 40, 40 }
};

/* -----------------------------------------------------------------------
 * Inputs
 * ----------------------------------------------------------------------- */

INPUT_PORTS_START( raiden2 )
	PORT_START /* P1 low byte */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY | IPF_PLAYER1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY | IPF_PLAYER1)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY | IPF_PLAYER1)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER1)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER1)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER1)
	PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START /* P2 high byte of modern P1_P2 */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_JOYSTICK_UP    | IPF_8WAY | IPF_PLAYER2)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN  | IPF_8WAY | IPF_PLAYER2)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT  | IPF_8WAY | IPF_PLAYER2)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT | IPF_8WAY | IPF_PLAYER2)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_BUTTON1 | IPF_PLAYER2)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_BUTTON2 | IPF_PLAYER2)
	PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START /* DSW A */
	PORT_DIPNAME(0x07, 0x07, DEF_STR(Coin_A))
	PORT_DIPSETTING(0x01, DEF_STR(4C_1C))
	PORT_DIPSETTING(0x02, DEF_STR(3C_1C))
	PORT_DIPSETTING(0x04, DEF_STR(2C_1C))
	PORT_DIPSETTING(0x07, DEF_STR(1C_1C))
	PORT_DIPSETTING(0x06, DEF_STR(1C_2C))
	PORT_DIPSETTING(0x05, DEF_STR(1C_3C))
	PORT_DIPSETTING(0x03, DEF_STR(1C_4C))
	PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
	PORT_DIPNAME(0x38, 0x38, DEF_STR(Coin_B))
	PORT_DIPSETTING(0x08, DEF_STR(4C_1C))
	PORT_DIPSETTING(0x10, DEF_STR(3C_1C))
	PORT_DIPSETTING(0x20, DEF_STR(2C_1C))
	PORT_DIPSETTING(0x38, DEF_STR(1C_1C))
	PORT_DIPSETTING(0x30, DEF_STR(1C_2C))
	PORT_DIPSETTING(0x28, DEF_STR(1C_3C))
	PORT_DIPSETTING(0x18, DEF_STR(1C_4C))
	PORT_DIPSETTING(0x00, DEF_STR(Free_Play))
	PORT_DIPNAME(0x40, 0x40, "Starting Coin")
	PORT_DIPSETTING(0x40, "Normal")
	PORT_DIPSETTING(0x00, "X 2")
	PORT_DIPNAME(0x80, 0x80, DEF_STR(Flip_Screen))
	PORT_DIPSETTING(0x80, DEF_STR(Off))
	PORT_DIPSETTING(0x00, DEF_STR(On))

	PORT_START /* DSW B */
	PORT_DIPNAME(0x03, 0x03, DEF_STR(Difficulty))
	PORT_DIPSETTING(0x03, "Normal")
	PORT_DIPSETTING(0x02, "Easy")
	PORT_DIPSETTING(0x01, "Hard")
	PORT_DIPSETTING(0x00, "Very Hard")
	PORT_DIPNAME(0x0c, 0x0c, DEF_STR(Lives))
	PORT_DIPSETTING(0x00, "1")
	PORT_DIPSETTING(0x04, "4")
	PORT_DIPSETTING(0x08, "2")
	PORT_DIPSETTING(0x0c, "3")
	PORT_DIPNAME(0x30, 0x30, DEF_STR(Bonus_Life))
	PORT_DIPSETTING(0x30, "200000 500000")
	PORT_DIPSETTING(0x20, "400000 1000000")
	PORT_DIPSETTING(0x10, "1000000 3000000")
	PORT_DIPSETTING(0x00, "None")
	PORT_DIPNAME(0x40, 0x40, "Demo Sound")
	PORT_DIPSETTING(0x00, DEF_STR(Off))
	PORT_DIPSETTING(0x40, DEF_STR(On))
	PORT_DIPNAME(0x80, 0x80, "Test Mode")
	PORT_DIPSETTING(0x80, DEF_STR(Off))
	PORT_DIPSETTING(0x00, DEF_STR(On))

	PORT_START /* SYSTEM at 0x74c */
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_START1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_START2)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_SERVICE1)
	PORT_BIT(0xf0, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START /* coins read by sound CPU */
	PORT_BIT_IMPULSE(0x01, IP_ACTIVE_LOW, IPT_COIN1, 4)
	PORT_BIT_IMPULSE(0x02, IP_ACTIVE_LOW, IPT_COIN2, 4)
	PORT_BIT(0xfc, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/* -----------------------------------------------------------------------
 * Graphics decode
 * ----------------------------------------------------------------------- */

static struct GfxLayout raiden2_charlayout =
{
	8,8,4096,4,
	{8,12,0,4},
	{3,2,1,0,19,18,17,16},
	{0*32,1*32,2*32,3*32,4*32,5*32,6*32,7*32},
	32*8
};

static struct GfxLayout raiden2_tilelayout =
{
	16,16,0x8000,4,
	{8,12,0,4},
	{3,2,1,0,19,18,17,16,3+64*8,2+64*8,1+64*8,0+64*8,19+64*8,18+64*8,17+64*8,16+64*8},
	{0*32,1*32,2*32,3*32,4*32,5*32,6*32,7*32,8*32,9*32,10*32,11*32,12*32,13*32,14*32,15*32},
	128*8
};

static struct GfxLayout raiden2_spritelayout =
{
	16,16,0x10000,4,
	{0,1,2,3},
	{4,0,12,8,20,16,28,24,36,32,44,40,52,48,60,56},
	{0*64,1*64,2*64,3*64,4*64,5*64,6*64,7*64,8*64,9*64,10*64,11*64,12*64,13*64,14*64,15*64},
	16*16*4
};

static struct GfxDecodeInfo raiden2_gfxdecodeinfo[] =
{
	{ REGION_GFX1, 0x00000, &raiden2_charlayout,   0x700, 0x10 },
	{ REGION_GFX2, 0x00000, &raiden2_tilelayout,   0x400, 0x40 },
	{ REGION_GFX3, 0x00000, &raiden2_spritelayout, 0x000, 0x40 },
	{ -1 }
};

/* -----------------------------------------------------------------------
 * Machine
 * ----------------------------------------------------------------------- */

static INTERRUPT_GEN( raiden2_interrupt )
{
	r2_debug_frame();
	cpu_set_irq_line_and_vector(cpu_getactivecpu(), 0, HOLD_LINE, 0xc0/4);
}

static void r2_state_postload(void)
{
	r2_main_bankswitch(r2_prg_bank);
	if (background_layer) tilemap_mark_all_tiles_dirty(background_layer);
	if (midground_layer) tilemap_mark_all_tiles_dirty(midground_layer);
	if (foreground_layer) tilemap_mark_all_tiles_dirty(foreground_layer);
	if (text_layer) tilemap_mark_all_tiles_dirty(text_layer);
}

static void r2_state_save_register(void)
{
	int i;
	state_save_register_int("raiden2", 0, "bg_bank", &bg_bank);
	state_save_register_int("raiden2", 0, "mid_bank", &mid_bank);
	state_save_register_int("raiden2", 0, "fg_bank", &fg_bank);
	state_save_register_int("raiden2", 0, "tx_bank", &tx_bank);
	state_save_register_UINT8("raiden2", 0, "scroll", r2_scroll, 12);
	state_save_register_UINT16("raiden2", 0, "layer_enable", &r2_layer_enable, 1);
	state_save_register_UINT16("raiden2", 0, "prg_bank", &r2_prg_bank, 1);

	state_save_register_UINT8("raiden2_cop", 0, "ram", r2_copram, sizeof(r2_copram));
	state_save_register_UINT32("raiden2_cop", 0, "regs", r2_cop_regs, 8);
	state_save_register_UINT32("raiden2_cop", 0, "itoa", &r2_cop_itoa, 1);
	state_save_register_UINT16("raiden2_cop", 0, "status", &r2_cop_status, 1);
	state_save_register_UINT16("raiden2_cop", 0, "scale", &r2_cop_scale, 1);
	state_save_register_UINT16("raiden2_cop", 0, "itoa_digit_count", &r2_cop_itoa_digit_count, 1);
	state_save_register_UINT8("raiden2_cop", 0, "itoa_digits", r2_cop_itoa_digits, 10);
	state_save_register_UINT16("raiden2_cop", 0, "angle", &r2_cop_angle, 1);
	state_save_register_UINT16("raiden2_cop", 0, "distance", &r2_cop_dist, 1);
	state_save_register_UINT16("raiden2_cop", 0, "angle_target", &r2_cop_angle_target, 1);
	state_save_register_UINT16("raiden2_cop", 0, "angle_step", &r2_cop_angle_step, 1);
	state_save_register_UINT16("raiden2_cop", 0, "dma_mode", &r2_cop_dma_mode, 1);
	state_save_register_UINT16("raiden2_cop", 0, "dma_src", r2_cop_dma_src, 0x200);
	state_save_register_UINT16("raiden2_cop", 0, "dma_dst", r2_cop_dma_dst, 0x200);
	state_save_register_UINT16("raiden2_cop", 0, "dma_size", r2_cop_dma_size, 0x200);
	state_save_register_UINT16("raiden2_cop", 0, "dma_v1", &r2_cop_dma_v1, 1);
	state_save_register_UINT16("raiden2_cop", 0, "dma_v2", &r2_cop_dma_v2, 1);
	state_save_register_UINT16("raiden2_cop", 0, "dma_adr_rel", &r2_cop_dma_adr_rel, 1);
	state_save_register_UINT16("raiden2_cop", 0, "sprites_start", &r2_sprites_cur_start, 1);
	state_save_register_UINT16("raiden2_cop", 0, "brightness", &r2_pal_brightness_val, 1);
	state_save_register_UINT16("raiden2_cop", 0, "bank", &r2_cop_bank, 1);
	state_save_register_UINT16("raiden2_cop", 0, "sprite_x", &r2_sprite_prot_x, 1);
	state_save_register_UINT16("raiden2_cop", 0, "sprite_y", &r2_sprite_prot_y, 1);
	state_save_register_UINT16("raiden2_cop", 0, "dst1", &r2_dst1, 1);
	state_save_register_UINT16("raiden2_cop", 0, "sprite_maxx", &r2_cop_spr_maxx, 1);
	state_save_register_UINT16("raiden2_cop", 0, "sprite_off", &r2_cop_spr_off, 1);
	state_save_register_UINT16("raiden2_cop", 0, "sprite_src", r2_sprite_prot_src_addr, 2);
	state_save_register_UINT16("raiden2_cop", 0, "hit_status", &r2_cop_hit_status, 1);
	state_save_register_UINT16("raiden2_cop", 0, "hit_base", &r2_cop_hit_baseadr, 1);
	state_save_register_UINT16("raiden2_cop", 0, "hit_value_status", &r2_cop_hit_val_stat, 1);
	state_save_register_INT16("raiden2_cop", 0, "hit_values", r2_cop_hit_val, 3);
	state_save_register_UINT32("raiden2_cop", 0, "sort_ram", &r2_cop_sort_ram_addr, 1);
	state_save_register_UINT32("raiden2_cop", 0, "sort_lookup", &r2_cop_sort_lookup, 1);
	state_save_register_UINT16("raiden2_cop", 0, "sort_param", &r2_cop_sort_param, 1);
	for (i = 0; i < 2; ++i) {
		state_save_register_INT16("raiden2_collision", i, "pos", r2_collision[i].pos, 3);
		state_save_register_INT8("raiden2_collision", i, "delta", r2_collision[i].dx, 3);
		state_save_register_UINT8("raiden2_collision", i, "size", r2_collision[i].size, 3);
		state_save_register_int("raiden2_collision", i, "allow_swap", &r2_collision[i].allow_swap);
		state_save_register_UINT16("raiden2_collision", i, "flags_swap", &r2_collision[i].flags_swap, 1);
		state_save_register_UINT32("raiden2_collision", i, "sprite_address", &r2_collision[i].spradr, 1);
		state_save_register_INT16("raiden2_collision", i, "minimum", r2_collision[i].min, 3);
		state_save_register_INT16("raiden2_collision", i, "maximum", r2_collision[i].max, 3);
	}
	state_save_register_func_postload(r2_state_postload);
}

static MACHINE_INIT(raiden2)
{
	machine_init_seibu_sound_1();
	seibu_bank_w(0, 0);

	bg_bank = 0;
	mid_bank = 1;
	fg_bank = 6;
	tx_bank = 0;
	memset(r2_scroll, 0, sizeof(r2_scroll));
	r2_layer_enable = 0;
	r2_prg_bank = 0;
	r2_reset_cop();

	/* Working later driver starts on entries 2/3.  Use BANK3/BANK4 so the
	   sound CPU can keep BANK1 for the Seibu sound ROM. */
	r2_main_bankswitch(0);
	r2_debug_reset();
}

static MACHINE_DRIVER_START( raiden2 )
	MDRV_CPU_ADD(V30, 32000000/2)
	MDRV_CPU_MEMORY(raiden2_readmem, raiden2_writemem)
	MDRV_CPU_VBLANK_INT(raiden2_interrupt, 1)

	MDRV_CPU_ADD(Z80, 28636360/8)
	MDRV_CPU_FLAGS(CPU_AUDIO_CPU)
	MDRV_CPU_MEMORY(raiden2_sound_readmem, raiden2_sound_writemem)

	MDRV_FRAMES_PER_SECOND(55.47)
	MDRV_VBLANK_DURATION(DEFAULT_60HZ_VBLANK_DURATION)
	MDRV_MACHINE_INIT(raiden2)

	MDRV_VIDEO_ATTRIBUTES(VIDEO_TYPE_RASTER)
	MDRV_SCREEN_SIZE(64*8, 64*8)
	MDRV_VISIBLE_AREA(0*8, 40*8-1, 0*8, 30*8-1)
	MDRV_GFXDECODE(raiden2_gfxdecodeinfo)
	MDRV_PALETTE_LENGTH(2048)
	MDRV_VIDEO_START(raiden2)
	MDRV_VIDEO_UPDATE(raiden2)

	MDRV_SOUND_ADD(YM2151, raiden2_ym2151_interface)
	MDRV_SOUND_ADD(OKIM6295, raiden2_oki_interface)
MACHINE_DRIVER_END

/* -----------------------------------------------------------------------
 * ROMs
 * ----------------------------------------------------------------------- */

ROM_START( raiden2 )
	ROM_REGION(0x200000, REGION_CPU1, 0)
	ROM_LOAD16_BYTE("prg0", 0x000000, 0x80000, CRC(09475ec4) SHA1(05027f2d8f9e11fcbd485659eda68ada286dae32))
	ROM_RELOAD(                 0x100000, 0x80000)
	ROM_LOAD16_BYTE("prg1", 0x000001, 0x80000, CRC(4609b5f2) SHA1(272d2aa75b8ea4d133daddf42c4fc9089093df2e))
	ROM_RELOAD(                 0x100001, 0x80000)

	ROM_REGION(0x20000, REGION_CPU2, 0)
	ROM_LOAD("snd", 0x00000, 0x08000, CRC(f51a28f9) SHA1(7ae2e2ba0c8159a544a8fd2bb0c2c694ba849302))
	ROM_CONTINUE(   0x10000, 0x08000)
	ROM_COPY(REGION_CPU2, 0x00000, 0x18000, 0x08000)

	ROM_REGION(0x020000, REGION_GFX1, ROMREGION_DISPOSE)
	ROM_LOAD("px0", 0x000000, 0x020000, CRC(c9ec9469) SHA1(a29f480a1bee073be7a177096ef58e1887a5af24))

	ROM_REGION(0x400000, REGION_GFX2, ROMREGION_DISPOSE)
	ROM_LOAD("bg1", 0x000000, 0x200000, CRC(e61ad38e) SHA1(63b06cd38db946ad3fc5c1482dc863ef80b58fec))
	ROM_LOAD("bg2", 0x200000, 0x200000, CRC(a694a4bb) SHA1(39c2614d0effc899fe58f735604283097769df77))

	/* Keep old-set contiguous loading. DRIVER_INIT explicitly repacks and
	   decrypts this region before MAME decodes the graphics. */
	ROM_REGION(0x800000, REGION_GFX3, ROMREGION_DISPOSE)
	ROM_LOAD("obj1", 0x000000, 0x200000, CRC(ff08ef0b) SHA1(a1858430e8171ca8bab785457ef60e151b5e5cf1))
	ROM_LOAD("obj2", 0x200000, 0x200000, CRC(638eb771) SHA1(9774cc070e71668d7d1d20795502dccd21ca557b))
	ROM_LOAD("obj3", 0x400000, 0x200000, CRC(897a0322) SHA1(abb2737a2446da5b364fc2d96524b43d808f4126))
	ROM_LOAD("obj4", 0x600000, 0x200000, CRC(b676e188) SHA1(19cc838f1ccf9c4203cd0e5365e5d99ff3a4ff0f))

	ROM_REGION(0x100000, REGION_SOUND1, 0)
	ROM_LOAD("voi1", 0x00000, 0x80000, CRC(f340457b) SHA1(8169acb24c82f68d223a31af38ee36eb6cb3adf4))

	ROM_REGION(0x100000, REGION_SOUND2, 0)
	ROM_LOAD("voi2", 0x00000, 0x80000, CRC(d321ff54) SHA1(b61e602525f36eb28a1408ffb124abfbb6a08706))
ROM_END

static DRIVER_INIT(raiden2)
{
	UINT8 *gfx = memory_region(REGION_GFX3);
	r2_bank_rom = auto_malloc(0x40000);
	if (r2_bank_rom) memcpy(r2_bank_rom, memory_region(REGION_CPU1), 0x40000);
	else r2_bank_rom = memory_region(REGION_CPU1) + 0x100000; /* untouched ROM_RELOAD mirror */
#if RAIDEN2_DEBUG
	r2_log_path[0] = 0;
	r2_frame_number = r2_reset_count = r2_last_draws = 0;
	r2_ever_lit = r2_was_black = 0;
	r2_old_coin = r2_old_start = 0xff;
#endif
	if (raiden2_prepare_and_decrypt_sprites(gfx) != 0)
		logerror("Raiden2: sprite decryption allocation failed\n");
	r2_state_save_register();
	seibu_sound_state_save_register();
}

/* This backport is intended to be playable.  Keep imperfect flags until
   Xbox 360 hardware testing confirms every priority/blending/sound detail. */
GAMEX(1993, raiden2, 0, raiden2, raiden2, raiden2, ROT270,
      "Seibu Kaihatsu", "Raiden 2 (playability backport)",
      GAME_IMPERFECT_GRAPHICS | GAME_IMPERFECT_SOUND)
