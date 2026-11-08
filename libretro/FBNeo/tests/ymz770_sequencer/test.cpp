#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef int INT32;
typedef short INT16;

struct ymz_channel
{
	UINT16 phrase;
	UINT8 pan;
	UINT8 pan_delay;
	UINT8 pan1;
	UINT8 pan1_delay;
	INT32 volume;
	UINT8 volume_target;
	UINT8 volume_delay;
	UINT8 volume2;
	UINT8 loop;

	bool is_playing, last_block, is_paused;

	INT16 output_data[0x1000];
	int output_remaining;
	int output_ptr;
	int atbl;
	int pptr;

	int end_channel_data;
};

struct ymz_sequence
{
	INT32 delay;
	UINT16 sequence;
	UINT16 timer;
	UINT16 stopchan;
	UINT8 loop;
	UINT32 offset;
	UINT8 bank;
	bool is_playing;
	bool is_paused;
};

struct ymz_sqc
{
	UINT8 sqc;
	UINT8 loop;
	UINT32 offset;
	bool is_playing;
	bool is_waiting;
};

static ymz_channel m_channels[16];
static ymz_sequence m_sequences[8];

static UINT8 m_rom[0x10000];
static const INT32 m_rom_size = 0x10000;
static const INT32 m_rom_mask = 0xffff;

static UINT8 get_rom_byte(UINT32 offset) { return m_rom[offset & m_rom_mask]; }
static UINT32 ymz770_get_seq_offs(int sqn) { return (UINT32)(0x1000 + sqn * 0x100); }

static void ymz770_internal_reg_write(UINT8 reg, UINT8 data)
{
	if (reg >= 0x40 && reg < 0x60)
	{
		int ch = (reg >> 2) & 7;
		switch (reg & 3)
		{
			case 0: m_channels[ch].phrase = data; break;
			case 1: m_channels[ch].volume2 = data; break;
			case 2: m_channels[ch].pan = data << 3; break;
			case 3:
				if ((data & 6) == 2 || ((data & 6) == 6 && !m_channels[ch].is_playing))
					m_channels[ch].is_playing = true;
				else if ((data & 6) == 0)
					m_channels[ch].is_playing = false;
				m_channels[ch].loop = (data & 1) ? 255 : 0;
				break;
		}
	}
}

// Production YMZ770 sequencer implementation with the loop-point fix
static void ymz770_sequencer()
{
	for (int i = 0; i < 8; i++)
	{
		ymz_sequence &sequence = m_sequences[i];

		if (sequence.is_playing)
		{
			if (sequence.delay > 0)
				sequence.delay--;
			else
			{
				int reg = get_rom_byte(sequence.offset++);
				UINT8 data = get_rom_byte(sequence.offset++);
				switch (reg)
				{
				case 0x0f:
					if (sequence.loop)
					{
						sequence.offset = ymz770_get_seq_offs(sequence.sequence); // loop sequence
					}
					else
					{
						for (int ch = 0; ch < 8; ch++)
							if (sequence.stopchan & (1 << ch))
								m_channels[ch].is_playing = false;
						sequence.is_playing = false;
					}
					break;
				case 0x0e:
					sequence.delay += sequence.timer * 32 + 32 - 1;
					break;
				default:
					sequence.delay = -1;
					ymz770_internal_reg_write(reg, data);
					break;
				}
			}
		}
	}
}

int main()
{
	printf("Running ymz770_sequencer unit test...\n");

	// Test 1: Looping sequence (e.g. DDPDFK Stage 1 BGM SQN 22)
	// Bytecode at 0x1000 + 0*0x100:
	// REG 0x40 (phrase 0x0D), REG 0x43 (KEYON 0x03), WAIT(1), 0x0F (EOS)
	memset(m_rom, 0, sizeof(m_rom));
	memset(m_channels, 0, sizeof(m_channels));
	memset(m_sequences, 0, sizeof(m_sequences));

	UINT32 seq_start = ymz770_get_seq_offs(0);
	UINT8 *p = &m_rom[seq_start];
	*p++ = 0x40; *p++ = 0x0d; // Phrase 0x0D
	*p++ = 0x43; *p++ = 0x03; // KEYON = 3 (Key-on + loop)
	*p++ = 0x0e; *p++ = 0x00; // WAIT 1 cycle
	*p++ = 0x0f; *p++ = 0x00; // 0x0F (EOS)

	ymz_sequence &seq = m_sequences[0];
	seq.sequence = 0;
	seq.stopchan = 0x01; // Controls Channel 0
	seq.loop = 1;        // Looping sequence
	seq.offset = seq_start;
	seq.delay = 0;
	seq.is_playing = true;

	// Step 1: REG 0x40
	ymz770_sequencer();
	assert(m_channels[0].phrase == 0x0d);

	// Step 2: REG 0x43 (KEYON)
	ymz770_sequencer();
	assert(m_channels[0].is_playing == true);

	// Step 3: WAIT
	ymz770_sequencer();
	assert(seq.delay == 30); // 0*32 + 31 - 1 = 30

	// Burn through wait
	while (seq.delay > 0) ymz770_sequencer();

	// Next step executes 0x0F (EOS)
	ymz770_sequencer();

	// Under the fix: Channel 0 MUST NOT BE KILLED on loop!
	assert(m_channels[0].is_playing == true);
	// Sequence must still be playing
	assert(seq.is_playing == true);
	// Offset must have rewound to sequence start
	assert(seq.offset == seq_start);

	printf("  PASS: Test 1 - Looping sequence preserves channel playback and rewinds to start\n");

	// Test 2: Non-looping sequence (loop == 0, e.g. Stage 1 Intro SQN 21)
	memset(m_channels, 0, sizeof(m_channels));
	memset(m_sequences, 0, sizeof(m_sequences));

	seq_start = ymz770_get_seq_offs(1);
	p = &m_rom[seq_start];
	*p++ = 0x40; *p++ = 0x0c; // Phrase 0x0C
	*p++ = 0x43; *p++ = 0x03; // KEYON = 3
	*p++ = 0x0f; *p++ = 0x00; // 0x0F (EOS)

	seq.sequence = 1;
	seq.stopchan = 0x01; // Controls Channel 0
	seq.loop = 0;        // Non-looping sequence!
	seq.offset = seq_start;
	seq.delay = 0;
	seq.is_playing = true;

	// Step 1: REG 0x40
	ymz770_sequencer();
	// Step 2: REG 0x43
	ymz770_sequencer();
	assert(m_channels[0].is_playing == true);

	// Step 3: 0x0F (EOS) on non-looping sequence
	ymz770_sequencer();

	// Channel 0 MUST BE SILENCED on non-looping EOS
	assert(m_channels[0].is_playing == false);
	// Sequence MUST TERMINATE
	assert(seq.is_playing == false);

	printf("  PASS: Test 2 - Non-looping sequence silences stopchan and terminates cleanly\n");

	// Test 3: Multiple channels stopchan mask
	memset(m_channels, 0, sizeof(m_channels));
	memset(m_sequences, 0, sizeof(m_sequences));

	seq.sequence = 2;
	seq.stopchan = 0x07; // Channels 0, 1, 2
	seq.loop = 0;
	seq.offset = ymz770_get_seq_offs(2);
	p = &m_rom[seq.offset];
	*p++ = 0x0f; *p++ = 0x00;

	m_channels[0].is_playing = true;
	m_channels[1].is_playing = true;
	m_channels[2].is_playing = true;
	m_channels[3].is_playing = true; // Channel 3 not in stopchan

	seq.is_playing = true;
	ymz770_sequencer();

	assert(m_channels[0].is_playing == false);
	assert(m_channels[1].is_playing == false);
	assert(m_channels[2].is_playing == false);
	assert(m_channels[3].is_playing == true); // Should still be playing!
	assert(seq.is_playing == false);

	printf("  PASS: Test 3 - Multi-channel stopchan bitmask correctly evaluated\n");

	printf("PASS ymz770_sequencer: all loop, EOS, and stopchan tests passed\n");
	return 0;
}
