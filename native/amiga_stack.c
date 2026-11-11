/*
 * Stack size for the libnix startup code.
 *
 * libnix reads this symbol when the program starts and allocates its stack
 * accordingly. Without it a program gets the shell's default - 4 KB - which a
 * C++ program with a state stack, recursive YAML parsing and 300-byte stack
 * frames blows through immediately. What that looks like from outside is not a
 * Guru: the exception handler itself faults on the dead stack, the emulated
 * 68040 double-faults, and WinUAE stops the CPU and shows HALT1. Black screen,
 * no log, nothing to read.
 *
 * The AmigaDOS "Stack" command in the startup script does NOT cover this - it
 * sets the shell's idea of the stack for the process it spawns, which libnix
 * has already overridden by the time main() runs. The openttd_amiga_68k port
 * carries the same 1 MB symbol for the same reason.
 */
/* 2026-09-17, ReMoM: EKSPERYMENT 8 MB. Na 1 MB HeMoM stawal w rysowaniu ekranu
   glownego, a zmiana optymalizacji jednego pliku (-O2 -> -O0) PRZESUWALA
   miejsce awarii - typowy objaw przepelnienia stosu (wieksze ramki na -O0).
   Kod ReMoM pochodzi z PC, gdzie stos ma 1-8 MB i strone ochronna; na Amidze
   przepelniony stos po cichu niszczy pamiec pod soba. */
unsigned long __stack = 8UL * 1024UL * 1024UL;

/* 2026-11-11, AmiEU2: __stack above is NOT honoured by this libnix (a run without "Stack 2000000" stopped in
 * world loading, log ends after "display:" - proven in eu2-rtgtest). eu2setup's Play, a shell with the default
 * stack, any start path: the game makes sure of its own 2 MB here. When the process already has that much
 * (icon stack, Stack command) nothing happens. Everything used across StackSwap is static. */
#include <exec/tasks.h>
#include <exec/memory.h>
#include <proto/exec.h>

#define EU2_STACK 2000000UL
int eu2_main(void);
static struct StackSwapStruct g_sss;
static APTR g_stk;
static int g_rc;

int main(void)
{
	struct Task *t = FindTask(NULL);
	if ((ULONG)t->tc_SPUpper - (ULONG)t->tc_SPLower >= EU2_STACK - 4096UL) return eu2_main();
	g_stk = AllocVec(EU2_STACK, MEMF_ANY);
	if (g_stk == NULL) return eu2_main();
	g_sss.stk_Lower = g_stk;
	g_sss.stk_Upper = (ULONG)g_stk + EU2_STACK;
	g_sss.stk_Pointer = (APTR)g_sss.stk_Upper;
	StackSwap(&g_sss);
	g_rc = eu2_main();
	StackSwap(&g_sss);
	FreeVec(g_stk);
	return g_rc;
}
