// Pre-optimization draw_one_sprite from df3d4ba3fca2a1d3ddc0fbc5a4a9f742b4293c9e.
static INT32 draw_one_sprite_reference(UINT16 const *data, INT32 xoffs, INT32 yoffs, const clip_struct clipin, const clip_struct clipout)
{
	static const INT32 transparency_masks[4][4] =
	{
		{ 0x7fff, 0x3fff, 0x1fff, 0x0fff },
		{ 0x3fff, 0x1fff, 0x0fff, 0x07ff },
		{ 0x3fff, 0x1fff, 0x0fff, 0x07ff },
		{ 0x1fff, 0x0fff, 0x07ff, 0x03ff }
	};

	UINT16 *m_spriteram = (UINT16*)DrvSprRAM;
	INT32 select = (!is_multi32 || !(BURN_ENDIAN_SWAP_INT16(data[3]) & 0x0800)) ? MIXER_LAYER_SPRITES_2 : MIXER_LAYER_MULTISPR_2;

	UINT16 *bitmap = BurnBitmapGetPosition(5 + select, 0, 0);
	UINT8 numbanks = (graphics_length[1] / 4) >> 20; // size in 32bit dwords, not bytes! -dink

	INT32 indirect = BURN_ENDIAN_SWAP_INT16(data[0]) & 0x2000;
	INT32 indlocal = BURN_ENDIAN_SWAP_INT16(data[0]) & 0x1000;
	INT32 shadow   = (BURN_ENDIAN_SWAP_INT16(data[0]) & 0x0800) && (sprite_control_latched[0x0a/2] & 1);
	INT32 fromram  = BURN_ENDIAN_SWAP_INT16(data[0]) & 0x0400;
	INT32 bpp8     = BURN_ENDIAN_SWAP_INT16(data[0]) & 0x0200;
	INT32 transp   = (BURN_ENDIAN_SWAP_INT16(data[0]) & 0x0100) ? 0 : (bpp8 ? 0xff : 0x0f);
	INT32 flipy    = BURN_ENDIAN_SWAP_INT16(data[0]) & 0x0080;
	INT32 flipx    = BURN_ENDIAN_SWAP_INT16(data[0]) & 0x0040;
	INT32 offsety  = BURN_ENDIAN_SWAP_INT16(data[0]) & 0x0020;
	INT32 offsetx  = BURN_ENDIAN_SWAP_INT16(data[0]) & 0x0010;
	INT32 adjusty  = (BURN_ENDIAN_SWAP_INT16(data[0]) >> 2) & 3;
	INT32 adjustx  = (BURN_ENDIAN_SWAP_INT16(data[0]) >> 0) & 3;
	INT32 srch     = (BURN_ENDIAN_SWAP_INT16(data[1]) >> 8);
	INT32 srcw     = bpp8 ? (BURN_ENDIAN_SWAP_INT16(data[1]) & 0x3f) : ((BURN_ENDIAN_SWAP_INT16(data[1]) >> 1) & 0x3f);
	INT32 bank     = is_multi32 ?
					((BURN_ENDIAN_SWAP_INT16(data[3]) & 0x2000) >> 13) | ((BURN_ENDIAN_SWAP_INT16(data[3]) & 0x8000) >> 14) :
					((BURN_ENDIAN_SWAP_INT16(data[3]) & 0x0800) >> 11) | ((BURN_ENDIAN_SWAP_INT16(data[3]) & 0x4000) >> 13);
	INT32 dsth     = BURN_ENDIAN_SWAP_INT16(data[2]) & 0x3ff;
	INT32 dstw     = BURN_ENDIAN_SWAP_INT16(data[3]) & 0x3ff;
	INT32 ypos     = (INT16)(BURN_ENDIAN_SWAP_INT16(data[4]) << 4) >> 4;
	INT32 xpos     = (INT16)(BURN_ENDIAN_SWAP_INT16(data[5]) << 4) >> 4;
	UINT32 addr  = BURN_ENDIAN_SWAP_INT16(data[6]) | ((BURN_ENDIAN_SWAP_INT16(data[2]) & 0xf000) << 4);
	INT32 color    = 0x8000 | (BURN_ENDIAN_SWAP_INT16(data[7]) & (bpp8 ? 0x7f00 : 0x7ff0));
	INT32 hzoom, vzoom;
	INT32 xdelta = 1, ydelta = 1;
	INT32 xtarget, ytarget, yacc = 0, pix, transmask;
	const UINT32 *spritedata;
	UINT32 addrmask;
	UINT16 indtable[16];

	/* if hidden, or top greater than/equal to bottom, or invalid bank, punt */
	if (srcw == 0 || srch == 0 || dstw == 0 || dsth == 0)
		goto bail;

	/* determine the transparency mask for pixels */
	transmask = transparency_masks[sprite_control_latched[0x08/2] & 3][sprite_control_latched[0x0a/2] & 3];
	if (bpp8)
		transmask &= 0xfff0;

	/* create the local palette for the indirect case */
	if (indirect)
	{
		UINT16 const *src = indlocal ? &data[8] : &m_spriteram[8 * (BURN_ENDIAN_SWAP_INT16(data[7]) & 0x1fff)];
		for (INT32 x = 0; x < 16; x++)
			indtable[x] = (BURN_ENDIAN_SWAP_INT16(src[x]) & (bpp8 ? 0xfff0 : 0xffff)) | ((sprite_control_latched[0x0a/2] & 1) ? 0x8000 : 0x0000);
	}

	/* clamp to within the memory region size */
	if (fromram)
	{
		spritedata = DrvSprRAM32;
		addrmask = (0x20000 / 4) - 1;
	}
	else
	{
		UINT32 *sprite_region = (UINT32*)DrvGfxROM[1];
		if (numbanks)
			bank %= numbanks;
		spritedata = &sprite_region[bank << 20];
		addrmask = 0xfffff;
	}

	/* compute X/Y deltas */
	hzoom = (((bpp8 ? 4 : 8) * srcw) << 16) / dstw;
	vzoom = (srch << 16) / dsth;

	/* adjust the starting X position */
	if (offsetx)
		xpos += xoffs;
	switch (adjustx)
	{
		case 0:
		case 3: xpos -= (dstw - 1) / 2;     break;
		case 1: xpos -= dstw - 1;           break;
		case 2:                             break;
	}

	/* adjust the starting Y position */
	if (offsety)
		ypos += yoffs;
	switch (adjusty)
	{
		case 0:
		case 3: ypos -= (dsth - 1) / 2;     break;
		case 1: ypos -= dsth - 1;           break;
		case 2:                             break;
	}

	/* adjust for flipping */
	if (flipx)
	{
		xpos += dstw - 1;
		xdelta = -1;
	}
	if (flipy)
	{
		ypos += dsth - 1;
		ydelta = -1;
	}

	/* compute target X,Y positions for loops */
	xtarget = xpos + xdelta * dstw;
	ytarget = ypos + ydelta * dsth;

	/* adjust target x for clipping */
	if (xdelta > 0 && xtarget > clipin.nMaxx)
	{
		xtarget = clipin.nMaxx + 1;
		if (xpos >= xtarget)
			goto bail;
	}
	if (xdelta < 0 && xtarget < clipin.nMinx)
	{
		xtarget = clipin.nMinx - 1;
		if (xpos <= xtarget)
			goto bail;
	}

	/* loop from top to bottom */
	for (INT32 y = ypos; y != ytarget; y += ydelta)
	{
		/* skip drawing if not within the inclusive cliprect */
		if (y >= clipin.nMiny && y <= clipin.nMaxy)
		{
			INT32 do_clipout = (y >= clipout.nMiny && y < clipout.nMaxy);
			UINT16 *const dest = bitmap + y * 512;
			INT32 xacc = 0;

			/* 4bpp case */
			if (!bpp8)
			{
				/* start at the word before because we preincrement below */
				UINT32 curaddr = addr - 1;
				for (INT32 x = xpos; x != xtarget; )
				{
					UINT32 pixels = BURN_ENDIAN_SWAP_INT32(spritedata[++curaddr & addrmask]);

					/* draw four pixels */
					pix = (pixels >> 28) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(transp)  x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >> 24) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0);      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >> 20) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0);      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >> 16) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0);      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >> 12) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0);      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >>  8) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0);      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >>  4) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(0);      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >>  0) & 0xf; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_16(transp); x += xdelta; xacc += hzoom; } xacc -= 0x10000;

					/* check for end code */
					if (transp != 0 && pix == 0x0f)
						break;
				}
			}

			/* 8bpp case */
			else
			{
				/* start at the word before because we preincrement below */
				UINT32 curaddr = addr - 1;
				for (INT32 x = xpos; x != xtarget; )
				{
					UINT32 pixels = BURN_ENDIAN_SWAP_INT32(spritedata[++curaddr & addrmask]);

					/* draw four pixels */
					pix = (pixels >> 24) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(transp); x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >> 16) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(0);      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >>  8) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(0);      x += xdelta; xacc += hzoom; } xacc -= 0x10000;
					pix = (pixels >>  0) & 0xff; while (xacc < 0x10000 && x != xtarget) { sprite_draw_pixel_256(transp); x += xdelta; xacc += hzoom; } xacc -= 0x10000;

					/* check for end code */
					if (transp != 0 && pix == 0xff)
						break;
				}
			}
		}

		/* accumulate zoom factors; if we carry into the high bit, skip an extra row */
		yacc += vzoom;
		addr += srcw * (yacc >> 16);
		yacc &= 0xffff;
	}

bail:
	return (indirect && indlocal) ? 2 : 0;
}
