#ifndef _COMPOSITOR_INTEL_H
#define _COMPOSITOR_INTEL_H
/*
    Copyright (C) 2010-2026, The AROS Development Team. All rights reserved.
*/

#include "compositor.h"

#include <exec/lists.h>

struct _Rectangle
{
    WORD MinX;
    WORD MinY;
    WORD MaxX;
    WORD MaxY;
};

struct StackBitMapNode
{
    struct Node         n;
    OOP_Object *        bm;
    struct _Rectangle   screenvisiblerect;
    BOOL                isscreenvisible;
    LONG                displayedwidth;
    LONG                displayedheight;
};

struct HIDDCompositorData
{
    /* Bitmap to which all screen bitmaps are composited */
    OOP_Object              *compositedbitmap;
    
    /* Pointer to actual screen bitmap */
    OOP_Object              *screenbitmap;

    /* Pointer to top bitmap on stack */
    OOP_Object              *topbitmap;

    HIDDT_ModeID            screenmodeid;
    struct _Rectangle       screenrect;
    BOOL                    modeschanged;

    struct List             bitmapstack;
    
    struct SignalSemaphore  semaphore;
    
    OOP_Object              *gfx;
    OOP_Object              *gc;

    /* Attr bases */
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
