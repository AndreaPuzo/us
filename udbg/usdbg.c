#include "usdbg.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

u32_t __dump_hex (
        FILE * fp        ,
        int    addr_size , // digit to print
        u64_t  addr      ,
        u64_t  size      ,
  const any_t  data
)
{
  char fmt [32] ;
  
  if (0 != addr_size)
    sprintf(fmt, "%%0%dllX |", addr_size) ;
  
  for (u64_t i = 0 ; i < size ; ++i) {
    if (0 != addr_size)
      fprintf(fp, fmt, addr + i) ;
  
    u64_t lim = size - i ;
    
    if (16 < lim)
      lim = 16 ;
    
    for (u64_t j = 0 ; j < lim ; ++j)
      fprintf(fp, " %02X", *((u8_t *)data + i + j)) ;
    
    for (u64_t j = 0 ; j < (4 + 16 - lim) ; ++j)
      fprintf(fp, " ") ;
    
    for (u64_t j = 0 ; j < lim ; ++j) {
      char byte = *((u8_t *)data + i + j) ;
      
      if (0 != isprint(byte))
        fprintf(fp, "%c", byte) ;
      else
        fprintf(fp, ".") ;
    }
    
    fprintf(fp, "\n") ;
  }
  
  return 0 ;
}

u32_t __dump_mem (
  us_t * us   ,
  FILE * fp   ,
  u64_t  addr ,
  u64_t  size
)
{
  if (us->mem.size < addr + size) {
    fprintf(stderr, "debug: memory section is out of memory\n") ;
    return 1 ;
  }
  
  u64_t last_addr = addr + size - 1 ;
  int addr_size = 2 ; // 8-bit
  
  if (0 != (last_addr >> 32))
    addr_size = 12 ; // 48-bit
  else if (0 != (last_addr >> 16))
    addr_size = 8 ; // 32-bit
  else if (0 != (last_addr >> 8))
    addr_size = 4 ; // 16-bit
  
  return __dump_hex(fp, addr_size, addr, size, us->mem.data + addr) ;
}

u32_t __dump_seg (
  us_t * us    ,
  FILE * fp    ,
  u16_t  _segx ,
  u64_t  _addr ,
  u64_t  _size
)
{
  if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_V)) {
    // Segment Descriptor Entry (SDE):
    // [  0:1  ] size    scale       -> 1, 2, 4, 8
    // [  2:3  ] size    granularity -> -, KiB, MiB, GiB
    // [  4:5  ] address scale       -> 1, 2, 4, 8
    // [  6:7  ] address granularity -> B, KiB, MiB, GiB
    // [  8:23 ] address offset      -> 1 << 8-bit value
    // [ 24:31 ] permissions         -> P|X|R|W|IOPL
    
    // read the SDE from the Segment Descriptor Table (SDT)
      
    u16_t segx = us->ker.reg[US_REG_SDT] >> 48 ;
    u64_t addr = (us->ker.reg[US_REG_SDT] << 16) >> 16 ;
    
    u32_t SDE ;
    
    if (
      US_N_IRQS != us_read(
        us, segx, addr + _segx * sizeof(SDE), sizeof(SDE), &SDE
      )
    ) {
      fprintf(stderr, "debug: `us_read` failed loading the segment descriptor entry\n") ;
      return 1 ;
    }
    
    u64_t size ;
    
    // compute the segment physical size
    
    size =
      (1 << ((SDE >> 0) & 3))               * // scale
      (1 << (10 * (1 << ((SDE >> 2) & 3)))) ; // granularity
    
    // compute the segment physical address
    
    addr =
      (1 << ((SDE >> 4) & 3))               * // scale
      (1 << (10 * (1 << ((SDE >> 6) & 3)))) + // granularity
      (((SDE >> 8) & 0xFF) << 2)            ; // offset
    
    // generate the permissions string
    
    u8_t perm = (SDE >> 24) & 15 ;
    char pstr [] = "---- -" ;
    
    // presence
    if (0 != (perm & US_SEG_PERM_P))
      pstr[0] = 'p' ;
    // executable
    if (0 != (perm & US_SEG_PERM_X))
      pstr[1] = 'x' ;
    // readable
    if (0 != (perm & US_SEG_PERM_R))
      pstr[2] = 'r' ;
    // writable
    if (0 != (perm & US_SEG_PERM_W))
      pstr[3] = 'w' ;
    
    // I/O privilege level
    pstr[5] = '0' + ((perm >> 4) & 3) ;
    
    if (size < _addr + _size)
      _size = size - _addr ;
    
    fprintf(
      fp                               ,
      "debug: Segment 0x%04X\n"
      "... address      : 0x%012llX\n"
      "... size         : 0x%012llX\n"
      "... permissions  : %s\n"        ,
      addr, size, pstr
    ) ;
    
    u64_t last_addr = _addr + _size - 1 ;
    int addr_size = 2 ; // 8-bit
    
    if (0 != (last_addr >> 32))
      addr_size = 12 ; // 48-bit
    else if (0 != (last_addr >> 16))
      addr_size = 8 ; // 32-bit
    else if (0 != (last_addr >> 8))
      addr_size = 4 ; // 16-bit
    
    return __dump_hex(fp, addr_size, _addr, _size, us->mem.data + addr + _addr) ;
  }
  
  return 0 ;
}

u32_t __dump_reg (
  us_t * us   ,
  FILE * fp   ,
  u8_t   regx ,
  u64_t  size
)
{
  switch (regx) {
  case US_REG_AX    : fprintf(fp, "AX    | ") ; break ;
  case US_REG_CX    : fprintf(fp, "CX    | ") ; break ;
  case US_REG_DX    : fprintf(fp, "DX    | ") ; break ;
  case US_REG_BX    : fprintf(fp, "BX    | ") ; break ;
  case US_REG_SP    : fprintf(fp, "SP    | ") ; break ;
  case US_REG_BP    : fprintf(fp, "BP    | ") ; break ;
  case US_REG_SI    : fprintf(fp, "SI    | ") ; break ;
  case US_REG_DI    : fprintf(fp, "DI    | ") ; break ;
  case US_REG_FLAGS : fprintf(fp, "FLAGS | ") ; break ;
  case US_REG_IP    : fprintf(fp, "IP    | ") ; break ;
  case US_REG_IDT   : fprintf(fp, "IDT   | ") ; break ;
  case US_REG_SDT   : fprintf(fp, "SDT   | ") ; break ;
  case US_REG_CLOCK : fprintf(fp, "CLOCK | ") ; break ;
  
  default :
    fprintf(stderr, "debug: invalid register index\n") ;
    return 1 ;
  }

  if (US_REG_FLAGS == regx) {
    char fstr [] = "-------0--0-0---" ;
    
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_IB))
      fstr[0] = 'B' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_V))
      fstr[1] = 'V' ;
    fstr[2] = '0' + ((us->ker.reg[US_REG_FLAGS] >> 13) & 1) ;
    fstr[3] = '0' + ((us->ker.reg[US_REG_FLAGS] >> 12) & 1) ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_O))
      fstr[4] = 'O' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_D))
      fstr[5] = 'D' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_I))
      fstr[6] = 'I' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_S))
      fstr[8] = 'S' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_Z))
      fstr[9] = 'Z' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_A))
      fstr[11] = 'A' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_P))
      fstr[13] = 'P' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_1))
      fstr[14] = '1' ;
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_C))
      fstr[15] = 'C' ;
    
    fprintf(fp, "%s", fstr) ;
    return 0 ;
  }

  switch (size) {
  case 1 :
    if (US_REG_SP <= regx && regx <= US_REG_DI)
      return __dump_hex(fp, 0, 0, size, us->ker.reg + regx - US_REG_SP) ;
  case 2 :
  case 4 :
  case 8 :
    return __dump_hex(fp, 0, 0, size, us->ker.reg + regx) ;
  
  default :
    fprintf(stderr, "debug: invalid register size\n") ;
    return 1 ;
  }

  return 0 ;
}

int __search_breakpoint (
  us_dbg_t * dbg  ,
  u16_t      segx ,
  u64_t      addr
)
{
  for (int i = 0 ; i < dbg->breakpointc ; ++i) {
    if (
      dbg->breakpointv[i].segx == segx &&
      dbg->breakpointv[i].addr == addr &&
      0 != dbg->breakpointv[i].exists
    )
      return i ;
  }
  
  return -1 ;
}

u32_t __set_breakpoint (
  us_t *     us   ,
  us_dbg_t * dbg  ,
  u16_t      segx ,
  u64_t      addr
)
{
  int breakpointx = __search_breakpoint(dbg, segx, addr) ;
  
  if (breakpointx < 0) {
    us_dbg_breakpoint_t * breakpoints = realloc(
      dbg->breakpointv, (dbg->breakpointc + 1) * sizeof(us_dbg_breakpoint_t)
    ) ;
    
    if (NULL == breakpoints) {
      fprintf(stderr, "debug: cannot reallocate the vector of breakpoints: not enough memory\n") ;
      return 1 ;
    }
    
    breakpointx = dbg->breakpointc ;
    dbg->breakpointv = breakpoints ;
    ++dbg->breakpointc ;
  }
  
  us_dbg_breakpoint_t * breakpoint = dbg->breakpointv + breakpointx ;
  
  if (
    0 != us_read(
      us, segx, addr, sizeof(u8_t), &breakpoint->byte
    )
  ) {
    fprintf(stderr, "debug: cannot save the byte to replace with the breakpoint\n") ;
    return 1 ;
  }
  
  u8_t int3 = 0x0E ; // int 3
  
  if (
    0 != us_write(
      us, segx, addr, sizeof(u8_t), &int3
    )
  ) {
    fprintf(stderr, "debug: cannot set the breakpoint\n") ;
    return 1 ;
  }
  
  breakpoint->segx   = segx ;
  breakpoint->addr   = addr ;
  breakpoint->exists = 1    ;
  
  return 0 ;
}

u32_t __clear_breakpoint (
  us_t *     us   ,
  us_dbg_t * dbg  ,
  u16_t      segx ,
  u64_t      addr
)
{
  int breakpointx = __search_breakpoint(dbg, segx, addr) ;
  
  if (0 <= breakpointx) {
    us_dbg_breakpoint_t * breakpoint = dbg->breakpointv + breakpointx ;
    
    if (
      0 != us_write(
        us, segx, addr, sizeof(u8_t), &breakpoint->byte
      )
    ) {
      fprintf(stderr, "debug: cannot restore the content of the breakpoint\n") ;
      return 1 ;
    }
    
    breakpoint->segx   = 0 ;
    breakpoint->addr   = 0 ;
    breakpoint->byte   = 0 ;
    breakpoint->exists = 0 ;
  } else {
    fprintf(stderr, "debug: breakpoint does not exist\n") ;
    return 1 ;
  }
  
  return 0 ;
}

u32_t __scan_breakpoint_address (
  us_t *  us     ,
  char *  input  ,
  int     ip     ,
  char ** endptr ,
  u16_t * segx   ,
  u64_t * addr
)
{
  while (0 != isspace(input[ip]))
    ++ip ;
  
  if (0 == strcmp(input + ip, "CS:IP")) {
    *segx = us->ker.seg[US_SEG_CODE] ;
    *addr = us->ker.reg[US_REG_IP] - 1 ; // IP was increasede by the clock
  } else if (0 == strncmp(input + ip, "CS:", 3)) {
    ip += 3 ;
    *segx = us->ker.seg[US_SEG_CODE] ;
    *addr = strtoull(input + ip, endptr, 0) ;
  } else {    
    *segx = strtoull(input + ip, endptr, 0) ;
    
    if (':' != **endptr) {
      fprintf(stderr, "debug: breakpoint requires far pointers\n") ;
      return 1 ;
    }
    
    *addr = strtoull(*endptr, endptr, 0) ;
  }
  
  return 0 ;
}

u32_t us_debug (
  us_t *     us  ,
  us_dbg_t * dbg
)
{
  static char input [1024 + 1] ;
  int quit = 0 ;
  int ip = -1 ;
  
  if (NULL == dbg->fp)
    dbg->fp = stderr ;
  
  do {
    do {
      int chr = fgetc(stdin) ;
      
      ++ip ;
      if (EOF == chr || '\n' == chr)
        input[ip] = 0 ;
      else
        input[ip] = (char)chr ;
    } while (0 != input[ip]) ;
    
    if (0 == strcmp(input, "quit"))
      quit = 1 ;
    else if (0 == strncmp(input, "file", 4)) {
      ip += 4 ;
      
      while (0 != isspace(input[ip]))
        ++ip ;
      
      FILE * fp = fopen(input + ip, "w") ;
      
      if (NULL != fp)
        dbg->fp = fp ;
      else
        fprintf(stderr, "debug: cannot open `%s` to log the debug\n", input + ip) ;
    } else if (0 == strncmp(input, "breakpoint", 10)) {
      ip += 10 ;
      
      u16_t segx ;
      u64_t addr ;
      char * endptr ;
      
      if ( 
        0 != __scan_breakpoint_address (
            us      ,
            input   ,
            ip      ,
            &endptr ,
            &segx   ,
            &addr
          )
      ) {
        ip = (int)endptr - (int)input ;
        
        while (0 != isspace(input[ip]))
          ++ip ;
        
        if (0 == strncmp(input + ip, "--clear", 7)) {          
          if (0 != __clear_breakpoint(us, dbg, segx, addr))
            fprintf(
              stderr, "debug: cannot add the breakpoint at 0x%04X:%012llX\n",
              segx, addr
            ) ;
        } else if (0 == strncmp(input + ip, "--set", 5)) {
          if (0 != __set_breakpoint(us, dbg, segx, addr))
            fprintf(
              stderr, "debug: cannot add the breakpoint at 0x%04X:%012llX\n",
              segx, addr
            ) ;
        } else if (0 == strncmp(input + ip,"--list", 6)) {
          if (0 != dbg->breakpointc) {
            for (int i = 0 ; i < dbg->breakpointc ; ++i) {
              fprintf(
                stderr ,
                ">>> Breakpoint (%i) at 0x%04X:%012llX\n"
                "... replaced byte : 0x%02X\n"
                "... exists        : %s\n" ,
                i, dbg->breakpointv[i].segx, dbg->breakpointv[i].addr,
                dbg->breakpointv[i].byte,
                (0 != dbg->breakpointv[i].exists) ? "yes" : "no"
              ) ;
            }
          } else
            fprintf(stderr, "debug: any breakpoint has been set\n") ;          
        } else
          fprintf(stderr, "debug: unknown command `%s`\n", input) ;
      }
      else
        fprintf(stderr, "debug: cannot read the breakpoint address\n") ;   
    } else if (0 == strcmp(input, "regs")) {
      __dump_reg(us, dbg->fp, US_REG_AX    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_CX    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_DX    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_BX    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_SP    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_BP    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_SI    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_DI    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_FLAGS , 8) ;
      __dump_reg(us, dbg->fp, US_REG_IP    , 8) ;
      __dump_reg(us, dbg->fp, US_REG_IDT   , 8) ;
      __dump_reg(us, dbg->fp, US_REG_SDT   , 8) ;
      __dump_reg(us, dbg->fp, US_REG_CLOCK , 8) ;
    } else if (0 == strcmp(input, "segs")) {
      __dump_seg(us, dbg->fp, us->ker.seg[US_SEG_DATA]  , 0, -1) ;
      __dump_seg(us, dbg->fp, us->ker.seg[US_SEG_EXTRA] , 0, -1) ;
      __dump_seg(us, dbg->fp, us->ker.seg[US_SEG_STACK] , 0, -1) ;
      __dump_seg(us, dbg->fp, us->ker.seg[US_SEG_CODE]  , 0, -1) ;
    } else
      fprintf(stderr, "debug: unknown command `%s`\n", input) ;
  } while (0 == quit) ;
  
  return 0 ;
}
