#ifndef _US_H
# define _US_H

# include "usver.h"
# include "usdef.h"

typedef struct us_ker_s us_ker_t ;
typedef struct us_mem_s us_mem_t ;
typedef struct us_opt_s us_opt_t ;
typedef struct us_s     us_t     ;

enum {
  US_SEG_PERM_P = 1 << 0 , 
  US_SEG_PERM_X = 1 << 1 ,
  US_SEG_PERM_R = 1 << 2 ,
  US_SEG_PERM_W = 1 << 3 ,
  US_SEG_IOPL   = 3 << 4
} ;

enum {
  US_FLAG_C    = 1 <<  0 ,
  US_FLAG_1    = 1 <<  1 ,
  US_FLAG_P    = 1 <<  2 ,
  US_FLAG_A    = 1 <<  4 ,
  US_FLAG_Z    = 1 <<  6 ,
  US_FLAG_S    = 1 <<  7 ,
  US_FLAG_I    = 1 <<  9 ,
  US_FLAG_D    = 1 << 10 ,
  US_FLAG_O    = 1 << 11 ,
  US_FLAG_IOPL = 3 << 12 ,
  US_FLAG_V    = 1 << 14 , // virtual/physical address space (not yet implemented)
  US_FLAG_IB   = 1 << 15   // ignore bounds resizing the data
} ;

enum {
  US_REG_0  , US_REG_AX    = US_REG_0  ,
  US_REG_1  , US_REG_CX    = US_REG_1  ,
  US_REG_2  , US_REG_DX    = US_REG_2  ,
  US_REG_3  , US_REG_BX    = US_REG_3  ,
  US_REG_4  , US_REG_SP    = US_REG_4  ,
  US_REG_5  , US_REG_BP    = US_REG_5  ,
  US_REG_6  , US_REG_SI    = US_REG_6  ,
  US_REG_7  , US_REG_DI    = US_REG_7  ,
  US_REG_8  , US_REG_FLAGS = US_REG_8  ,
  US_REG_9  , US_REG_IP    = US_REG_9  ,
  US_REG_10 , US_REG_IDT   = US_REG_10 ,
  US_REG_11 , US_REG_SDT   = US_REG_11 ,
  US_REG_12 , US_REG_CLOCK = US_REG_12 ,
  US_REG_13 ,
  US_REG_14 ,
  US_REG_15 ,

  US_N_REGS
} ;

enum {
  US_SEG_0 , US_SEG_DATA  = US_SEG_0 ,
  US_SEG_1 , US_SEG_EXTRA = US_SEG_1 ,
  US_SEG_2 , US_SEG_STACK = US_SEG_2 ,
  US_SEG_3 , US_SEG_CODE  = US_SEG_3 ,

  US_N_SEGS
} ;

enum {
  US_IRQ_DIV_BY_ZERO     ,
  US_IRQ_SINGLE_STEP     ,
  US_IRQ_NON_MASKABLE    ,
  US_IRQ_BREAKPOINT      ,
  US_IRQ_OUT_OF_BOUNDS   ,
  US_IRQ_SEGMENT_PROTECT ,
  US_IRQ_SEGMENT_FAULT   ,
  US_IRQ_STACK_OVERFLOW  ,
  US_IRQ_STACK_UNDERFLOW ,
  US_IRQ_UNDEFINED_INST  ,
  US_IRQ_INTERRUPT_FAULT ,
  US_IRQ_OUT_OF_CLOCKS   ,
  
  US_N_IRQS = 0x100
} ;

struct us_ker_s {
  u64_t reg [US_N_REGS] ;
  u16_t seg [US_N_SEGS] ;
} ;

struct us_mem_s {
  u64_t  size ;
  u8_t * data ;
} ;

struct us_opt_s {
  u8_t  verbose : 1 ;
  u64_t max_clocks  ;
} ;

struct us_s {
  us_ker_t ker ;
  us_mem_t mem ;
  us_opt_t opt ;
  u32_t    IRQ ;
  
  struct {
    u64_t  IP        ; // instruction pointer
    u32_t  cp        ; // `code` pointer
    u8_t   code [16] ; // read from code segment
    u8_t   op [2]    ; // opcodes
    u64_t  oprd_size ; // operands size
    u64_t  addr_size ; // address size
    
    struct { // prefixes
      u8_t has_SOV : 1 ; // segment override
      u8_t has_ZOV : 1 ; // size override
      u8_t has_AOV : 1 ; // address override
      u8_t has_REP : 1 ; // repeat the instruction
      u8_t REP_cc  : 1 ; // repeat condition
      u8_t has_IP  : 1 ; // address of IP
    } ;
    
    struct { // Mod RM and SIB
      u8_t mod : 2 ; // mode
      u8_t reg : 3 ; // register or opcode extencion
      u8_t rm  : 3 ; // register or memory
      u8_t sc  : 2 ; // index scale
      u8_t idx : 3 ; // index register
      u8_t bs  : 3 ; // base register
    } ;
    
    struct { // values
      u16_t segx ; // segment index
      u64_t addr ; // computed address
      i64_t imm  ; // immediate value
    } ;
  } inst ;
} ;

u32_t us_load_img (
        us_t * us ,
  const char * fn
) ;

u32_t us_write (
        us_t * us   ,
        u16_t  segx ,
        u64_t  addr ,
        u64_t  size ,
  const any_t  data
) ;

u32_t us_read (
  us_t * us   ,
  u16_t  segx ,
  u64_t  addr ,
  u64_t  size ,
  any_t  data
) ;

u32_t us_int (
  us_t * us  ,
  u32_t  IRQ
) ;

u32_t us_iret (
  us_t * us
) ;

u32_t us_push (
        us_t * us   ,
        u64_t  size ,
  const any_t  data
) ;

u32_t us_pop (
  us_t * us   ,
  u64_t  size ,
  any_t  data
) ;

u32_t us_clock (
  us_t * us
) ;

#endif
