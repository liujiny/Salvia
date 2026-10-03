// Mixer loop at baseline 36ff170d; test oracle.
static void reference_mix_rows(INT32 top, INT32 bottom, INT32)
{
 const System32MixerContext &c = system32_mixer;
 const INT32 which=c.which, xoffs=c.xoffs, blendfactor=c.blendfactor;
 const clip_struct &cliprect=c.cliprect;
 UINT16 *m_paletteram=c.m_paletteram;
 const mixer_layer_info (*layerorder)[8]=c.layerorder;
 const INT32 (*rgboffs)[3]=c.rgboffs;
 const INT32 sprgroup_shift=c.sprgroup_shift, sprgroup_mask=c.sprgroup_mask;
 const INT32 sprshadowmask=c.sprshadowmask, sprpixmask=c.sprpixmask, sprshadow=c.sprshadow;
 const INT32 sprx_start=c.sprx_start, sprdx=c.sprdx, sprdy=c.sprdy;
 INT32 spry=c.spry+(top-cliprect.nMiny)*sprdy;
	/* loop over rows */
	for (INT32 y = top; y < bottom; y++, spry += sprdy)
	{
		UINT16 *dest = pTransDraw + (y * nScreenWidth) + xoffs;
		UINT16 *layerbase[8];

		/* get the starting address for each layer */
		layerbase[MIXER_LAYER_TEXT] = get_layer_scanline(MIXER_LAYER_TEXT, y);
		layerbase[MIXER_LAYER_NBG0] = get_layer_scanline(MIXER_LAYER_NBG0, y);
		layerbase[MIXER_LAYER_NBG1] = get_layer_scanline(MIXER_LAYER_NBG1, y);
		layerbase[MIXER_LAYER_NBG2] = get_layer_scanline(MIXER_LAYER_NBG2, y);
		layerbase[MIXER_LAYER_NBG3] = get_layer_scanline(MIXER_LAYER_NBG3, y);
		layerbase[MIXER_LAYER_BITMAP] = get_layer_scanline(MIXER_LAYER_BITMAP, y);
		layerbase[MIXER_LAYER_SPRITES] = get_layer_scanline(which ? MIXER_LAYER_MULTISPR : MIXER_LAYER_SPRITES, spry); // works ok instead of swap?
		layerbase[MIXER_LAYER_BACKGROUND] = get_layer_scanline(MIXER_LAYER_BACKGROUND, y);

		/* loop over columns */
		for (INT32 x = cliprect.nMinx, sprx = sprx_start; x <= cliprect.nMaxx; x++, sprx += sprdx)
		{
			mixer_layer_info const *first;
			INT32 laynum, firstpix;
			INT32 shadow = 0;

			/* first grab the current sprite pixel and determine the group */
			INT32 sprpix = layerbase[MIXER_LAYER_SPRITES][sprx];
			INT32 sprgroup = (sprpix >> sprgroup_shift) & sprgroup_mask;

			/* now scan the layers to find the topmost non-transparent pixel */
			for (first = &layerorder[sprgroup][0]; ; first++)
			{
				laynum = first->index;

				/* non-sprite layers are treated similarly */
				if (laynum != MIXER_LAYER_SPRITES)
				{
					firstpix = BURN_ENDIAN_SWAP_INT16(layerbase[laynum][x]) & 0x1fff;
					if (firstpix != 0 || laynum == MIXER_LAYER_BACKGROUND)
						break;
				}

				/* sprite layers are special */
				else
				{
					firstpix = sprpix;
					shadow = ~firstpix & sprshadowmask;
					if ((firstpix & 0x7fff) != 0x7fff)
					{
						firstpix &= sprpixmask;
						if ((firstpix & 0x7ffe) != sprshadow)
							break;
						shadow = 1;
					}
				}
			}

			/* adjust the first pixel */
			firstpix = BURN_ENDIAN_SWAP_INT16(m_paletteram[(first->palbase + ((firstpix >> first->mixshift) & 0xfff0) + (firstpix & 0x0f)) & 0x3fff]);

			/* compute R, G, B */
			INT32 const *rgbdelta = &rgboffs[first->coloroffs][0];
			if (first->blendmask == 0 && !(rgbdelta[0] | rgbdelta[1] | rgbdelta[2])) {
				if (shadow) firstpix = (firstpix >> 1) & 0x3def;
				dest[x] = ((firstpix & 0x001f) << 10) | (firstpix & 0x03e0) | ((firstpix >> 10) & 0x001f);
				continue;
			}
			INT32 r = ((firstpix >>  0) & 0x1f) + rgbdelta[0];
			INT32 g = ((firstpix >>  5) & 0x1f) + rgbdelta[1];
			INT32 b = ((firstpix >> 10) & 0x1f) + rgbdelta[2];

			/* if there are potential blends, keep looking */
			if (first->blendmask != 0)
			{
				mixer_layer_info const *second;
				INT32 secondpix;

				/* now scan the layers to find the topmost non-transparent pixel */
				for (second = first + 1; ; second++)
				{
					laynum = second->index;

					/* non-sprite layers are treated similarly */
					if (laynum != MIXER_LAYER_SPRITES)
					{
						secondpix = BURN_ENDIAN_SWAP_INT16(layerbase[laynum][x]) & 0x1fff;
						if (secondpix != 0 || laynum == MIXER_LAYER_BACKGROUND)
							break;
					}

					/* sprite layers are special */
					else
					{
						secondpix = sprpix;
						shadow = ~secondpix & sprshadowmask;
						if ((secondpix & 0x7fff) != 0x7fff)
						{
							secondpix &= sprpixmask;
							if ((secondpix & 0x7ffe) != sprshadow)
								break;
							shadow = 1;
						}
					}
				}

				/* are we blending with that layer? */
				if ((first->blendmask & (1 << laynum)) &&
					(laynum != MIXER_LAYER_SPRITES || (first->sprblendmask & (1 << sprgroup))))
				{
					/* adjust the second pixel */
					secondpix = BURN_ENDIAN_SWAP_INT16(m_paletteram[(second->palbase + ((secondpix >> second->mixshift) & 0xfff0) + (secondpix & 0x0f)) & 0x3fff]);

					/* compute first RGB */
					r *= 7 - blendfactor;
					g *= 7 - blendfactor;
					b *= 7 - blendfactor;

					/* add in second RGB */
					rgbdelta = &rgboffs[second->coloroffs][0];
					r += (((secondpix >>  0) & 0x1f) + rgbdelta[0]) * (blendfactor + 1);
					g += (((secondpix >>  5) & 0x1f) + rgbdelta[1]) * (blendfactor + 1);
					b += (((secondpix >> 10) & 0x1f) + rgbdelta[2]) * (blendfactor + 1);

					/* shift off the extra bits */
					r >>= 3;
					g >>= 3;
					b >>= 3;
				}
			}

			/* apply shadow/hilight */
			if (shadow)
			{
				r >>= 1;
				g >>= 1;
				b >>= 1;
			}

			/* clamp and combine */
			if (r > 31)
				firstpix = 31 << 10;
			else if (r > 0)
				firstpix = r << 10;
			else
				firstpix = 0;

			if (g > 31)
				firstpix |= 31 << 5;
			else if (g > 0)
				firstpix |= g << 5;

			if (b > 31)
				firstpix |= 31 << 0;
			else if (b > 0)
				firstpix |= b << 0;
			dest[x] = firstpix;
		}
	}

}
