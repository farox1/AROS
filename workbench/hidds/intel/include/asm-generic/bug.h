#ifndef _ASM_GENERIC_BUG_H
#define _ASM_GENERIC_BUG_H

#define BUG() do { } while(0)
#define BUG_ON(cond) do { if (cond) ; } while(0)
#define WARN_ON(cond) (cond)

#endif /* _ASM_GENERIC_BUG_H */
