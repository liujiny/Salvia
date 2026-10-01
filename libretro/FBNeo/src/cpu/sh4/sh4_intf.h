#include <stdint.h>

#ifndef FASTCALL
 #undef __fastcall
 #define __fastcall
#endif

typedef UINT8 (__fastcall *pSh3ReadByteHandler)(UINT32 a);
typedef void (__fastcall *pSh3WriteByteHandler)(UINT32 a, UINT8 d);
typedef UINT16 (__fastcall *pSh3ReadWordHandler)(UINT32 a);
typedef void (__fastcall *pSh3WriteWordHandler)(UINT32 a, UINT16 d);
typedef UINT32 (__fastcall *pSh3ReadLongHandler)(UINT32 a);
typedef void (__fastcall *pSh3WriteLongHandler)(UINT32 a, UINT32 d);

void __fastcall Sh3WriteByte(UINT32 a, UINT8 d);
UINT8 __fastcall Sh3ReadByte(UINT32 a);

void Sh3Init(INT32 num, INT32 hz, char md0, char md1, char md2, char md3, char md4, char md5, char md6, char md7, char md8 );
void Sh3Exit();

void sh4_set_cave_blitter_delay_func(void (*pfunc)(int));
void sh4_set_cave_blitter_delay_timer(int cycles);
INT32 sh4_get_cpu_speed();
void Sh3SetClockCV1k(INT32 clock);

// Xbox 360 SH3 -> PPC dynamic recompiler; other hosts keep the interpreter.
void Sh3SetDrc(INT32 enabled);
// One 64 KiB RAM page whose read handler is side-effect-free except for a
// single watched longword. Re-register after changing the read handlers.
void Sh3SetDrcReadMirror(UINT8* ram, UINT32 page, UINT32 watched, INT32 handler);
// Sampled MOV.L diagnostics only; caller retains pointer ownership until exit.
// Register after SetDrcReadMirror; replacing the mirror revokes this metadata.
void Sh3SetDrcIdleWatch(const UINT32* idle_ram, const UINT32* idle_pc);
// Register a fully mapped RAM window (power-of-two span and backing size).
// Mapping/handler changes revoke the window and invalidate compiled blocks.
INT32 Sh3SetDrcRam(UINT8* ram, UINT32 start, UINT32 span, UINT32 backing_size);

void Sh3SetTimerGranularity(INT32 timergransh); // speedhack

void Sh3Open(const INT32 i);
void Sh3Close();
INT32 Sh3GetActive();

void Sh3Reset();
INT32 Sh3Run(INT32 cycles);
#ifdef _XBOX
// Explicit low-frequency diagnostic runner; ordinary Sh3Run remains uninstrumented.
void Sh3WorkBeginFrame();
INT32 Sh3WorkRun(INT32 cycles);
void Sh3WorkReset();
void Sh3WorkReport(void (*emit)(const char*));
#endif

void Sh3SetIRQLine(INT32 line, INT32 state);

INT32 Sh3MapMemory(UINT8* pMemory, UINT32 nStart, UINT32 nEnd, INT32 nType);
INT32 Sh3MapHandler(uintptr_t nHandler, UINT32 nStart, UINT32 nEnd, INT32 nType);

INT32 Sh3SetReadPortHandler(pSh3ReadLongHandler pHandler);
INT32 Sh3SetWritePortHandler(pSh3WriteLongHandler pHandler);

INT32 Sh3SetReadByteHandler(INT32 i, pSh3ReadByteHandler pHandler);
INT32 Sh3SetWriteByteHandler(INT32 i, pSh3WriteByteHandler pHandler);
INT32 Sh3SetReadWordHandler(INT32 i, pSh3ReadWordHandler pHandler);
INT32 Sh3SetWriteWordHandler(INT32 i, pSh3WriteWordHandler pHandler);
INT32 Sh3SetReadLongHandler(INT32 i, pSh3ReadLongHandler pHandler);
INT32 Sh3SetWriteLongHandler(INT32 i, pSh3WriteLongHandler pHandler);

UINT32 Sh3GetPC(INT32 n);
void Sh3RunEnd();

void Sh3BurnUntilInt();

INT32 Sh3TotalCycles();
void Sh3NewFrame();
void Sh3BurnCycles(INT32 cycles);
INT32 Sh3Idle(INT32 cycles);
void Sh3SetEatCycles(INT32 i);

INT32 Sh3Scan(INT32 nAction);


void Sh3CheatWriteByte(UINT32 a, UINT8 d); // cheat core
UINT8 Sh3CheatReadByte(UINT32 a);

extern struct cpu_core_config Sh3Config;

// depreciate this and use BurnTimerAttach directly!
#define BurnTimerAttachSh3(clock)	\
	BurnTimerAttach(&Sh3Config, clock)
