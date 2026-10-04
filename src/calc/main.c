#ifndef COMPILE_PC

#include <debug.h>
#include <graphx.h>
#include <setjmp.h>
#include <ti/getcsc.h>
#include <ti/screen.h>

#include "gui.h"
#include "heap.h"

static jmp_buf out_of_memory;

static void fail_out_of_memory(void) {
	longjmp(out_of_memory, 1);
}

int main(void) {
	dbg_printf("PinaCAS main() at %p\n", (void *)main);

	heap_Init();
	dbg_printf("Heap: %u bytes\n", (unsigned)heap_Available());

	if (setjmp(out_of_memory)) {
		gfx_End();
		os_ClrHome();
		os_PutStrFull("PinaCAS ran out of memory.");
		while (!os_GetCSC())
			;
		return 1;
	}

	heap_SetFailure(fail_out_of_memory);

	gui_Run();

	return 0;
}

#else
typedef int make_iso_compilers_happy;
#endif
