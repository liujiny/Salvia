// Original System 32 zoom/rowscroll loops, before contiguous source spans.
static void reference_zoom(clip_struct cliprect, UINT16 *ram, INT32 destbmp, INT32 bgnum)
{
	INT32 tilemaps[4];

	get_tilemaps(bgnum, tilemaps);

	for (INT32 i = 0; i < 4; i++) {
		tilemap_cache = &tmap_cache[tilemaps[i]];

		if (tilemap_cache->dirty) {
			tilemap_cache->dirty = 0;
			GenericTilemapDraw(tilemap_cache->tmap, 32 + tilemap_cache->tmap, 0);
		}
	}

	INT32 opaque = 0;
	INT32 flipx, flipy;

	// todo determine flipping
	compute_tilemap_flips(bgnum, flipx, flipy);

	INT32 clipenable = (BURN_ENDIAN_SWAP_INT16(ram[0x1ff02/2]) >> (11 + bgnum)) & 1;
	INT32 clipout = (BURN_ENDIAN_SWAP_INT16(ram[0x1ff02/2]) >> (6 + bgnum)) & 1;
	INT32 clips = (BURN_ENDIAN_SWAP_INT16(ram[0x1ff06/2]) >> (4 * bgnum)) & 0x0f;
	extents_list clip_extents;
	INT32 clipdraw_start = compute_clipping_extents(clipenable, clipout, clips, cliprect, &clip_extents);

	INT32 dstxstep = BURN_ENDIAN_SWAP_INT16(ram[0x1ff50/2 + 2 * bgnum]) & 0xfff;
	INT32 dstystep;
	if (BURN_ENDIAN_SWAP_INT16(ram[0x1ff00/2]) & 0x4000)
		dstystep = BURN_ENDIAN_SWAP_INT16(ram[0x1ff52/2 + 2 * bgnum]) & 0xfff;
	else
		dstystep = dstxstep;

	/* clamp the zoom factors */
	if (dstxstep < 0x80)
		dstxstep = 0x80;
	if (dstystep < 0x80)
		dstystep = 0x80;

	UINT32 srcxstep = (0x200 << 20) / dstxstep;
	UINT32 srcystep = (0x200 << 20) / dstystep;

	UINT32 srcx_start = (BURN_ENDIAN_SWAP_INT16(ram[0x1ff12/2 + 4 * bgnum]) & 0x3ff) << 20;
	srcx_start += (BURN_ENDIAN_SWAP_INT16(ram[0x1ff10/2 + 4 * bgnum]) & 0xff00) << 4;
	UINT32 srcy = (BURN_ENDIAN_SWAP_INT16(ram[0x1ff16/2 + 4 * bgnum]) & 0x1ff) << 20;
	srcy += (BURN_ENDIAN_SWAP_INT16(ram[0x1ff14/2 + 4 * bgnum]) & 0xfe00) << 4;

	srcx_start -= sign_extend(BURN_ENDIAN_SWAP_INT16(ram[0x1ff30/2 + 2 * bgnum]), (dstxstep != 0x200) ? 10 : 9) * srcxstep;
	srcy -= sign_extend(BURN_ENDIAN_SWAP_INT16(ram[0x1ff32/2 + 2 * bgnum]), (dstystep != 0x200) ? 10 : 9) * srcystep;

	srcx_start += cliprect.nMinx * srcxstep;
	srcy += cliprect.nMiny * srcystep;

	if (flipy)
	{
	//	const rectangle &visarea = screen.visible_area();

		srcy += (cliprect.nMaxy - 2 * cliprect.nMiny) * srcystep;
		srcystep = -srcystep;
	}

	if (flipx)
	{
	//	const rectangle &visarea = screen.visible_area();

		srcx_start += (cliprect.nMaxx - 2 * cliprect.nMinx) * srcxstep;
		srcxstep = -srcxstep;
	}

	for (INT32 y = cliprect.nMiny; y <= cliprect.nMaxy; y++)
	{
		UINT16 const *extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
		UINT16 *const dst = BurnBitmapGetPosition(destbmp+5, 0, y);
		INT32 clipdraw = clipdraw_start;

		if (clipdraw || extents[1] <= cliprect.nMaxx)
		{
			INT32 transparent = 0;

			UINT16 const *tm0 = BurnBitmapGetBitmap(tmap_cache[tilemaps[(((srcy >> 27) & 2) + 0)]].tmap + 32 );
			UINT16 const *tm1 = BurnBitmapGetBitmap(tmap_cache[tilemaps[(((srcy >> 27) & 2) + 1)]].tmap + 32 );
			UINT16 const *src[2] = { &tm0[((srcy >> 20) & 0xff) * 512], &tm1[((srcy >> 20) & 0xff) * 512] };

			UINT32 srcx = srcx_start;
			while (1)
			{
				if (clipdraw)
				{
					for (INT32 x = extents[0]; x < extents[1]; x++)
					{
						UINT16 pix = src[(srcx >> 29) & 1][(srcx >> 20) & 0x1ff];
						srcx += srcxstep;
						if ((pix & 0x0f) == 0 && !opaque)
							pix = 0, transparent++;
						dst[x] = BURN_ENDIAN_SWAP_INT16(pix);
					}
				}
				else
				{
					INT32 pixels = extents[1] - extents[0];
					memset(&dst[extents[0]], 0, pixels * sizeof(dst[0]));
					transparent += pixels;
					srcx += srcxstep * pixels;
				}

				if (extents[1] > cliprect.nMaxx)
					break;

				clipdraw = !clipdraw;
				extents++;
			}
			transparent_check[destbmp+5][y] = (transparent == cliprect.nMaxx - cliprect.nMinx + 1);
		}
		else
			transparent_check[destbmp+5][y] = 1;

		srcy += srcystep;
	}
}

static void reference_rowscroll(clip_struct cliprect, UINT16 *m_videoram, INT32 destbmp, INT32 bgnum)
{
	INT32 tilemaps[4];
	get_tilemaps(bgnum, tilemaps);

	for (INT32 i = 0; i < 4; i++) {
		tilemap_cache = &tmap_cache[tilemaps[i]];

		if (tilemap_cache->dirty) {
			tilemap_cache->dirty = 0;
			GenericTilemapDraw(tilemap_cache->tmap, 32 + tilemap_cache->tmap, 0);
		}
	}

	INT32 opaque = (opaquey_hack) ? ((m_videoram[0x1ff8e/2] >> (8 + bgnum)) & 1) : 0;
	INT32 flipx, flipy;

	compute_tilemap_flips(bgnum, flipx, flipy);

	INT32 clipenable = (BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff02/2]) >> (11 + bgnum)) & 1;
	INT32 clipout = (BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff02/2]) >> (6 + bgnum)) & 1;
	INT32 clips = (BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff06/2]) >> (4 * bgnum)) & 0x0f;
	extents_list clip_extents;
	INT32 clipdraw_start = compute_clipping_extents(clipenable, clipout, clips, cliprect, &clip_extents);

	INT32 rowscroll = (BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff04/2]) >> (bgnum - 2)) & 1;
	INT32 rowselect = (BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff04/2]) >> bgnum) & 1;
	if ((BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff04/2]) >> (bgnum + 2)) & 1)
		rowscroll = rowselect = 0;

	UINT16 const *const table = &m_videoram[(BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff04/2]) >> 10) * 0x400];

	INT32 xscroll = (BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff12/2 + 4 * bgnum]) & 0x3ff) - (BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff30/2 + 2 * bgnum]) & 0x1ff);
	INT32 yscroll = (BURN_ENDIAN_SWAP_INT16(m_videoram[0x1ff16/2 + 4 * bgnum]) & 0x1ff);

	for (INT32 y = cliprect.nMiny; y <= cliprect.nMaxy; y++)
	{
		UINT16 const *extents = &clip_extents.extent[clip_extents.scan_extent[y]][0];
		UINT16 *const dst = BurnBitmapGetPosition(destbmp+5, 0, y);
		INT32 clipdraw = clipdraw_start;

		/* optimize for the case where we are clipped out */
		if (clipdraw || extents[1] <= cliprect.nMaxx)
		{
			INT32 transparent = 0;
			INT32 srcxstep;

			/* if we're not flipped, things are straightforward */
			INT32 srcx;
			if (!flipx)
			{
				srcx = cliprect.nMinx + xscroll;
				srcxstep = 1;
			}
			else
			{
				srcx = cliprect.nMaxx + xscroll;
				srcxstep = -1;
			}

			INT32 srcy;
			INT32 ylookup;
			if (!flipy)
			{
				srcy = yscroll + y;
				ylookup = y;
			}
			else
			{
				srcy = yscroll + cliprect.nMaxy /*visarea.nMaxy*/ - y;
				ylookup = cliprect.nMaxy - y;
			}

			/* apply row scroll/select */
			if (rowscroll)
				srcx += BURN_ENDIAN_SWAP_INT16(table[0x000 + 0x100 * (bgnum - 2) + ylookup]) & 0x3ff;
			if (rowselect)
				srcy = (yscroll + BURN_ENDIAN_SWAP_INT16(table[0x200 + 0x100 * (bgnum - 2) + ylookup])) & 0x1ff;

			/* look up the pages and get their source pixmaps */
			UINT16 const *tm0 = BurnBitmapGetBitmap(tmap_cache[tilemaps[((srcy >> 7) & 2) + 0]].tmap + 32 );
			UINT16 const *tm1 = BurnBitmapGetBitmap(tmap_cache[tilemaps[((srcy >> 7) & 2) + 1]].tmap + 32 );
			UINT16 const *src[2] = { &tm0[(srcy & 0xff) * 512], &tm1[(srcy & 0xff) * 512] };

			/* loop over extents */
			while (1)
			{
				/* if we're drawing on this extent, draw it */
				if (clipdraw)
				{
					for (INT32 x = extents[0]; x < extents[1]; x++, srcx += srcxstep)
					{
						UINT16 pix = src[(srcx >> 9) & 1][srcx & 0x1ff];
						if ((pix & 0x0f) == 0 && !opaque)
							pix = 0, transparent++;
						dst[x] = BURN_ENDIAN_SWAP_INT16(pix);
					}
				}

				/* otherwise, clear to zero */
				else
				{
					INT32 pixels = extents[1] - extents[0];
					memset(&dst[extents[0]], 0, pixels * sizeof(dst[0]));
					transparent += pixels;
					srcx += srcxstep * pixels;
				}

				/* stop at the end */
				if (extents[1] > cliprect.nMaxx)
					break;

				/* swap states and advance to the next extent */
				clipdraw = !clipdraw;
				extents++;
			}

			transparent_check[destbmp+5][y] = (transparent == cliprect.nMaxx - cliprect.nMinx + 1);
		}
		else
			transparent_check[destbmp+5][y] = 1;
	}
}
