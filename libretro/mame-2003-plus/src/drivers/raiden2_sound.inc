/* Raiden II spreads eight Seibu registers across 32 V30 byte addresses.
   Modern MAME divides its WORD offset by two; our BYTE offset must divide
   by four. Keep the generic Seibu handler unchanged for other games. */
static READ_HANDLER(r2_sound_main_r)
{
 UINT16 word = seibu_main_word_r((offset >> 2) & 7, 0);
 int value = (word >> ((offset & 1) * 8)) & 0xff;
 r2_debug_access(0x700 + offset, value << ((offset & 1) * 8), 0);
 return value;
}

static WRITE_HANDLER(r2_sound_main_w)
{
 r2_debug_access(0x700 + offset, (data & 0xff) << ((offset & 1) * 8), 1);
 /* Only the low byte is wired. Bit 1 of the address is a mirror. */
 if (!(offset & 1)) seibu_main_word_w((offset >> 2) & 7, data & 0xff, 0xff00);
}
