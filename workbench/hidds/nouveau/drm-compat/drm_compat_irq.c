/*
    Copyright 2026, The AROS Development Team. All rights reserved.
*/

#include <aros/debug.h>
#include <proto/exec.h>
#include <exec/interrupts.h>

#include <drm-compat/drm_compat_funcs.h>


struct irq_handler_entry
{
    unsigned int        irq;        /* IRQ number (INT line) */
    irq_handler_t       handler;    /* Linux-style handler */
    void                *dev_id;    /* Device cookie for matching */
    struct Interrupt    is;         /* AROS Interrupt structure */
};

struct irq_handler_entry entry;

static AROS_INTH1(irq_dispatcher, struct irq_handler_entry *, entry)
{
    AROS_INTFUNC_INIT

    irqreturn_t ret = entry->handler(entry->irq, entry->dev_id);
    return (ret == IRQ_HANDLED) ? TRUE : FALSE;

    AROS_INTFUNC_EXIT
}

int request_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, const char *name, void *dev)
{
    if (!handler)
        return -EINVAL;

    /* Some cards (e.g. laptop discrete GPUs) report INT line 0 or 255
       because their IRQ is routed via the IOAPIC/MSI path, which the AROS
       PCI subsystem does not expose. Registering an interrupt server on
       INTB_KERNEL + 0 would conflict with the kernel vector, so skip
       registration but report success to let the driver run without
       interrupts. */
    if ((irq == 0) || (irq >= 255))
    {
        bug("request_irq: skipping invalid IRQ %u for %s (driver runs WITHOUT interrupts)\n", irq, name);
        return 0;
    }

    entry.irq       = irq;
    entry.handler   = handler;
    entry.dev_id    = dev;

    /* Set up the Interrupt structure */
    entry.is.is_Node.ln_Type    = NT_INTERRUPT;
    entry.is.is_Node.ln_Pri     = 10;
    entry.is.is_Node.ln_Name    = (STRPTR)"Nouveau IRQ handler";
    entry.is.is_Code            = (VOID_FUNC)irq_dispatcher;
    entry.is.is_Data            = (APTR)&entry;

    /* Register with the interrupt system */
    AddIntServer(INTB_KERNEL + irq, &entry.is);

    return 0;
}

