#ifndef _COMPOSITOR_INTEL_H
#define _COMPOSITOR_INTEL_H
/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

#include "compositor.h"

#include <exec/lists.h>
#include <exec/semaphores.h>
#include <graphics/gfxbase.h>

struct StackBitMapNode
{
    struct Node         n;
    OOP_Object          *bm;
    struct Region       *screenregion;
    struct Rectangle    screenvisiblerect;
    BOOL                isscreenvisible;
    LONG                displayedwidth;
    LONG                displayedheight;
    LONG                leftedge;
    LONG                topedge;
    IPTR                sbmflags;
};

#define STACKNODEB_VISIBLE      16
#define STACKNODEF_VISIBLE      (1 << STACKNODEB_VISIBLE)

struct HIDDCompositorData
{
    OOP_Object              *compositedbitmap;

    OOP_Object              *screenbitmap;

    OOP_Object              *topbitmap;

    HIDDT_ModeID            screenmodeid;
    struct Rectangle        screenrect;
    BOOL                    modeschanged;

    BOOL                    compositing_active;

    struct Region           *dirtyregion;
    struct Region           *backgroundregion;

    struct List             bitmapstack;

    struct SignalSemaphore  semaphore;

    OOP_Object              *gfx;
    OOP_Object              *gc;
    OOP_Object              *fb;

    OOP_AttrBase    pixFmtAttrBase;
    OOP_AttrBase    syncAttrBase;
    OOP_AttrBase    bitMapAttrBase;
    OOP_AttrBase    gcAttrBase;
    OOP_AttrBase    compositorAttrBase;
};

#define METHOD(base, id, name) \
  base ## __ ## id ## __ ## name (OOP_Class *cl, OOP_Object *o, struct p ## id ## _ ## name *msg)

#define BASE(lib)                   ((LIBBASETYPEPTR)(lib))

#define SD(cl)                      (&BASE(cl->UserData)->sd)

#define LOCK_COMPOSITOR_READ       { ObtainSemaphoreShared(&compdata->semaphore); }
#define LOCK_COMPOSITOR_WRITE      { ObtainSemaphore(&compdata->semaphore); }
#define UNLOCK_COMPOSITOR          { ReleaseSemaphore(&compdata->semaphore); }

#endif /* _COMPOSITOR_INTEL_H */
