/*
    Minimal Intel driver init - just enough to load the module
*/

#include <aros/debug.h>
#include <aros/symbolsets.h>

struct SignalSemaphore globalLock;

static ULONG Intel_Init(void)
{
    return TRUE;
}

APTR HIDDIntelAlloc(ULONG size) { return NULL; }
VOID HIDDIntelFree(APTR memory) { }
IPTR HIDDIntelAllocSize(CONST_APTR memory) { return 0; }

int intel_init(void) { return 0; }

ADD2INITLIB(Intel_Init, 0);
