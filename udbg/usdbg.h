#ifndef _USDBG_H
# define _USDBG_H

# include "../usys/us.h"
# include <stdio.h>

typedef struct us_dbg_breakpoint_s us_dbg_breakpoint_t ;
typedef struct us_dbg_s            us_dbg_t            ;

struct us_dbg_breakpoint_s {
  u16_t segx   ;
  u64_t addr   ;
  u8_t  byte   ; // this byte is replaced by `int 3`
  u8_t  exists ;
} ;

struct us_dbg_s {
  int                   breakpointc ;
  us_dbg_breakpoint_t * breakpointv ;
  FILE *                fp          ;
} ;

u32_t us_debug (
  us_t *     us  ,
  us_dbg_t * dbg
) ;

#endif
