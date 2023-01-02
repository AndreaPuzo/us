#include "us.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// Clock Cycle
// -----------------------------------------------------------------------------
// Next clock:
//   1. read (with no bounds) 16 bytes from code segment (in memory, at CS:IP)
//   2. fetch the opcode and prefixes of the next instruction to execute
//   3. decode and execute the instruction
// =============================================================================

u32_t __get_reg (
  us_t * us   ,
  u8_t   regx ,
  u64_t  size ,
  any_t  data
)
{
  switch (size) {
  case 1 : {
    u8_t * reg = (u8_t *)(us->ker.reg + (regx & 3)) ;
    
    if (3 <= regx)
      *(u8_t *)data = reg[0] ; // low
    else
      *(u8_t *)data = reg[1] ; // high
  } break ;
  
  case 2 : *(u16_t *)data = *(u16_t *)(us->ker.reg + regx) ; break ;
  case 4 : *(u32_t *)data = *(u32_t *)(us->ker.reg + regx) ; break ;
  case 8 : *(u64_t *)data = *(u64_t *)(us->ker.reg + regx) ; break ;
  
  default :
    return us_int(us, US_IRQ_NON_MASKABLE) ;
  }
  
  return US_N_IRQS ;
}

u32_t __sext (
  us_t *  us   ,
  u64_t   data ,
  u64_t   size ,
  i64_t * sext
)
{
  switch (size) {
  case 1 : *sext = *(i8_t  *)data ; break ;
  case 2 : *sext = *(i16_t *)data ; break ;
  case 4 : *sext = *(i32_t *)data ; break ;
  case 8 : *sext = *(i64_t *)data ; break ;
  
  default :
    return us_int(us, US_IRQ_NON_MASKABLE) ;
  }
  
  return US_N_IRQS ;
}

u32_t __set_reg (
        us_t * us   ,
        u8_t   regx ,
        u64_t  size ,
  const any_t  data
)
{
  switch (size) {
  case 1 : {
    u8_t * reg = (u8_t *)(us->ker.reg + (regx & 3)) ;
    
    if (3 <= regx)
      reg[0] = *(u8_t *)data ; // low
    else
      reg[1] =  *(u8_t *)data ; // high
  } break ;
  
  case 2 : *(u16_t *)(us->ker.reg + regx) = *(u16_t *)data ; break ;
  // clear the high 32-bit
  case 4 : *(u64_t *)(us->ker.reg + regx) = *(u32_t *)data ; break ;
  case 8 : *(u64_t *)(us->ker.reg + regx) = *(u64_t *)data ; break ;
  
  default :
    return us_int(us, US_IRQ_NON_MASKABLE) ;
  }
  
  return US_N_IRQS ;
}

u32_t __fetch_inst (
  us_t * us
)
{
  // read the instruction from memory

  us->ker.reg[US_REG_FLAGS] |= US_FLAG_IB ;
  
  if (
    US_N_IRQS != us_read(
      us, us->ker.seg[US_SEG_CODE], us->ker.reg[US_REG_IP],
      sizeof(us->inst.code), us->inst.code
    )
  )
    return us->IRQ ;
  
  us->ker.reg[US_REG_FLAGS] &= ~US_FLAG_IB ;
  
  // scan the prefixes
  
  us->inst.IP = us->ker.reg[US_REG_IP] ; // save the instruction pointer
  
_scan :
    
  if ( // segment override
    0x60 <= us->inst.code[us->inst.cp] &&
    us->inst.code[us->inst.cp] <= 0x63
  ) {
    us->inst.has_SOV = 1 ; 
    us->inst.segx = us->ker.seg[us->inst.code[us->inst.cp] & 3] ;
    
    // next byte of code
    us->inst.cp += sizeof(u8_t) ;
    goto _scan ;
  }
  
  if ( // repeat
    0x64 == us->inst.code[us->inst.cp] ||
    0x65 == us->inst.code[us->inst.cp]
  ) {
    us->inst.has_REP = 1 ;
    us->inst.REP_cc = us->inst.code[us->inst.cp] & 1 ;
    
    // next byte of code
    us->inst.cp += sizeof(u8_t) ;
    goto _scan ;
  }
  
  if ( // operand size override
    0x66 == us->inst.code[us->inst.cp]
  ) {
    us->inst.has_ZOV = 1 ;
    
    // next byte of code
    us->inst.cp += sizeof(u8_t) ;
    goto _scan ;
  }
  
  if ( // address size override
    0x67 == us->inst.code[us->inst.cp]
  ) {
    us->inst.has_AOV = 1 ;
    
    // next byte of code
    us->inst.cp += sizeof(u8_t) ;
    goto _scan ;
  }
  
  // operation code
  
  us->inst.op[0] = us->inst.code[us->inst.cp] ;
  
  if (0xF0 == us->inst.code[us->inst.cp]) {
    // 2-byte operation code
    us->inst.cp += sizeof(u8_t) ;
    us->inst.op[1] = us->inst.code[us->inst.cp] ;
  }
  
  us->inst.cp += sizeof(u8_t) ;
  
  return US_N_IRQS ;
}

u32_t __fetch_SIB (
  us_t * us
)
{
  // fetch the SIB byte
  u8_t byte = us->inst.code[us->inst.cp] ;
  us->inst.cp += sizeof(u8_t) ;
  
  us->inst.sc  = (byte >> 6) & 3 ;
  us->inst.idx = (byte >> 3) & 7 ;
  us->inst.bs  = (byte >> 0) & 7 ;
  
  // decode the SIB byte
  
  i64_t disp = 0 ;
  u64_t addr = 0 ;
  
  switch (us->inst.mod) {
  case 0 :
    if (US_REG_BP != us->inst.bs) {      
      if (US_N_IRQS != __get_reg(us, us->inst.bs, us->inst.addr_size, &addr))
        return us->IRQ ;
      
      // address: base
      us->inst.addr += addr ;
    } else {
      disp = *(i32_t *)(us->inst.code + us->inst.cp) ;
      us->inst.cp += sizeof(i32_t) ;
      
      // address: displacement
      us->inst.addr += disp ;
    }
    
    if (US_REG_SP != us->inst.idx) {      
      if (US_N_IRQS != __get_reg(us, us->inst.idx, us->inst.addr_size, &addr))
        return us->IRQ ;
      
      // address: scale * index
      us->inst.addr += (1 << us->inst.sc) * addr ;
    }
    break ;
  
  case 1 :
  case 2 :
    if (US_REG_SP != us->inst.idx) {      
      if (US_N_IRQS != __get_reg(us, us->inst.idx, us->inst.addr_size, &addr))
        return us->IRQ ;
      
      // address: scale * index
      us->inst.addr = (1 << us->inst.sc) * addr ;
    }
    
    if (1 == us->inst.mod) {
      disp = *(i8_t *)(us->inst.code + us->inst.cp) ;
      us->inst.cp += sizeof(i8_t) ;
    } else {
      disp = *(i32_t *)(us->inst.code + us->inst.cp) ;
      us->inst.cp += sizeof(i32_t) ;
    }
    
    // address: displacement
    us->inst.addr += disp ;
    break ;
  
  default :
    return us_int(us, US_IRQ_NON_MASKABLE) ;
  }
  
  return US_N_IRQS ;
}

u32_t __fetch_ModRM (
  us_t * us
)
{
  // fetch the ModRM byte
  u8_t byte = us->inst.code[us->inst.cp] ;
  us->inst.cp += sizeof(u8_t) ;
  
  us->inst.mod = (byte >> 6) & 3 ;
  us->inst.reg = (byte >> 3) & 7 ;
  us->inst.rm  = (byte >> 0) & 7 ;
  
  // decode the ModRM byte
  
  i64_t disp = 0 ;
  u64_t addr = 0 ;
  
  switch (us->inst.mod) {
  case 0 :
    if (US_REG_BP == us->inst.rm) { // [IP + disp32]
      // address: base
      us->inst.has_IP = 1 ;
      
      // address: displacement
      us->inst.addr = *(i32_t *)(us->inst.code + us->inst.cp) ;
      us->inst.cp += sizeof(i32_t) ;
    } else if (US_REG_SP == us->inst.rm) { // [SIB]
      if (US_N_IRQS != __fetch_SIB(us))
        return us->IRQ ;
    } else { // [rm]
      if (US_N_IRQS != __get_reg(us, us->inst.rm, us->inst.addr_size, &addr))
        return us->IRQ ;
      
      // address: base
      us->inst.addr = addr ;
    }
    break ;
    
  case 1 : // disp = disp8
  case 2 : // disp = disp32
    if (US_REG_SP == us->inst.rm) { // [SIB + disp]
      if (US_N_IRQS != __fetch_SIB(us))
        return us->IRQ ;
    } else { // [rm + disp]
      if (US_N_IRQS != __get_reg(us, us->inst.rm, us->inst.addr_size, &addr))
        return us->IRQ ;
      
      // address: base
      us->inst.addr = addr ;
      
      if (1 == us->inst.mod) {
        disp = *(i8_t *)(us->inst.code + us->inst.cp) ;
        us->inst.cp += sizeof(i8_t) ;
      } else {
        disp = *(i32_t *)(us->inst.code + us->inst.cp) ;
        us->inst.cp += sizeof(i32_t) ;
      }
      
      // address: displacement
      us->inst.addr += disp ;
    }
    break ;
  
  case 3 : // rm
    break ;
  
  default :
    return us_int(us, US_IRQ_NON_MASKABLE) ;
  }
  
  return US_N_IRQS ;
}

#define __raise_0(__us)                             \
  {                                                 \
    (__us)->ker.reg[US_REG_IP] += (__us)->inst.cp ; \
    return us->IRQ ;                                \
  }

#define __raise(__us, __IRQ)                        \
  {                                                 \
    (__us)->ker.reg[US_REG_IP] += (__us)->inst.cp ; \
    return us_int((__us), (__IRQ)) ;                \
  }

u32_t __fetch_uimm (us_t * us, u64_t size, any_t data)
{
  switch (size) {
  case 1 : *(u8_t  *)data = *(u8_t  *)(us->inst.code + us->inst.cp) ; break ;
  case 2 : *(u16_t *)data = *(u16_t *)(us->inst.code + us->inst.cp) ; break ;
  case 4 : *(u32_t *)data = *(u32_t *)(us->inst.code + us->inst.cp) ; break ;
  case 8 : *(u64_t *)data = *(u64_t *)(us->inst.code + us->inst.cp) ; break ;
  
  default :
    return us_int(us, US_IRQ_NON_MASKABLE) ;
  }
  
  us->inst.cp += size ;
  
  return US_N_IRQS ;
}

u32_t __fetch_imm (us_t * us, u64_t size, i64_t * data)
{
  switch (size) {
  case 1 : *data = *(i8_t  *)(us->inst.code + us->inst.cp) ; break ;
  case 2 : *data = *(i16_t *)(us->inst.code + us->inst.cp) ; break ;
  case 4 : *data = *(i32_t *)(us->inst.code + us->inst.cp) ; break ;
  case 8 : *data = *(i64_t *)(us->inst.code + us->inst.cp) ; break ;
  
  default :
    return us_int(us, US_IRQ_NON_MASKABLE) ;
  }
  
  us->inst.cp += size ;
  
  return US_N_IRQS ;
}

#define __get_modrm_reg(__us, __size, __data)                                 \
  {                                                                           \
    if (US_N_IRQS != __get_reg((__us), (__us)->inst.reg, (__size), (__data))) \
      __raise_0(__us)                                                         \
  }

#define __set_modrm_reg(__us, __size, __data)                                 \
  {                                                                           \
    if (US_N_IRQS != __set_reg((__us), (__us)->inst.reg, (__size), (__data))) \
      __raise_0(__us)                                                         \
  }

#define __get_modrm_rm(__us, __size, __data)                                    \
  {                                                                             \
    if (3 != (__us)->inst.mod) {                                                \
      if (                                                                      \
        US_N_IRQS != us_read(                                                   \
          (__us), (__us)->inst.segx, (__us)->inst.addr, (__size), (__data)      \
        )                                                                       \
      )                                                                         \
        __raise_0(__us)                                                         \
    } else {                                                                    \
      if (US_N_IRQS != __get_reg((__us), (__us)->inst.reg, (__size), (__data))) \
        __raise_0(__us)                                                         \
    }                                                                           \
  }

#define __set_modrm_rm(__us, __size, __data)                                    \
  {                                                                             \
    if (3 != (__us)->inst.mod) {                                                \
      if (                                                                      \
        US_N_IRQS != us_write(                                                  \
          (__us), (__us)->inst.segx, (__us)->inst.addr, (__size), (__data)      \
        )                                                                       \
      )                                                                         \
        __raise_0(__us)                                                         \
    } else {                                                                    \
      if (US_N_IRQS != __set_reg((__us), (__us)->inst.reg, (__size), (__data))) \
        __raise_0(__us)                                                         \
    }                                                                           \
  }

#define __modrm(__us)                     \
  {                                       \
    if (US_N_IRQS != __fetch_ModRM(__us)) \
      __raise_0(__us)                     \
  }

#define __uimm(__us, __size, __data)                           \
  {                                                            \
    if (US_N_IRQS != __fetch_uimm((__us), (__size), (__data))) \
      __raise_0(__us)                                          \
  }

#define __imm(__us, __size, __data)                           \
  {                                                           \
    if (US_N_IRQS != __fetch_imm((__us), (__size), (__data))) \
      __raise_0(__us)                                         \
  }

#define _SOV(__us, __defseg)           \
  {                                    \
    if (0 != (__us)->inst.has_SOV)     \
      (__us)->inst.segx = (__defseg) ; \
  }

#define _ZOV(__us, __op, __0_0, __0_1, __1_0, __1_1) \
  {                                                  \
    if (0 != ((__op) & 1)) {                         \
      if (0 != (__us)->inst.has_ZOV)                 \
        (__us)->inst.oprd_size = (__1_1) ;           \
      else                                           \
        (__us)->inst.oprd_size = (__1_0) ;           \
    } else {                                         \
      if (0 != (__us)->inst.has_ZOV)                 \
        (__us)->inst.oprd_size = (__0_1) ;           \
      else                                           \
        (__us)->inst.oprd_size = (__0_0) ;           \
    }                                                \
  }

#define _AOV(__us, __0_0, __0_1)         \
  {                                      \
    if (0 != (__us)->inst.has_AOV)       \
      (__us)->inst.addr_size = (__0_1) ; \
    else                                 \
      (__us)->inst.addr_size = (__0_0) ; \
  }

u32_t __exec_inst (
  us_t * us
)
{
  union { u64_t u ; i64_t i ; } a ;
  union { u64_t u ; i64_t i ; } b ;
  union { u64_t u ; i64_t i ; } c ;

#define _SOV_ZOV_AOV_0                 \
  _SOV(us, us->ker.seg[US_SEG_DATA])   \
  _ZOV(us, us->inst.op[0], 0, 1, 2, 3) \
  _AOV(us, 3, 2)

#define __init_modrm_0 \
  _SOV_ZOV_AOV_0       \
  __modrm(us)

  switch (us->inst.op[0]) {  
  case 0x00 :   // add r8  r/m8
  case 0x01 : { // add r32 r/m32
    __init_modrm_0
    __get_modrm_reg(us, us->inst.oprd_size, &a.u)
    __get_modrm_rm(us, us->inst.oprd_size, &b.u)
    c.u = a.u + b.u ;
    __set_modrm_reg(us, us->inst.oprd_size, &c.u)
    // TODO: update flags
  } break ;
  
  case 0x02 :   // add r/m8  r8
  case 0x03 : { // add r/m32 r32
    __init_modrm_0
    __get_modrm_reg(us, us->inst.oprd_size, &b.u)
    __get_modrm_rm(us, us->inst.oprd_size, &a.u)
    c.u = a.u + b.u ;
    __set_modrm_rm(us, us->inst.oprd_size, &c.u)
    // TODO: update flags
  } break ;
  
  case 0x04 :   // sub r8  r/m8
  case 0x05 : { // sub r32 r/m32
    __init_modrm_0
    __get_modrm_reg(us, us->inst.oprd_size, &a.u)
    __get_modrm_rm(us, us->inst.oprd_size, &b.u)
    c.u = a.u - b.u ;
    __set_modrm_reg(us, us->inst.oprd_size, &c.u)
    // TODO: update flags
  } break ;
  
  case 0x06 :   // sub r/m8  r8
  case 0x07 : { // sub r/m32 r32
    __init_modrm_0
    __get_modrm_reg(us, us->inst.oprd_size, &b.u)
    __get_modrm_rm(us, us->inst.oprd_size, &a.u)
    c.u = a.u - b.u ;
    __set_modrm_rm(us, us->inst.oprd_size, &c.u)
    // TODO: update flags
  } break ;
  
  case 0x08 : { // int imm8
    u8_t imm = *(u8_t *)(us->inst.code + us->inst.cp) ;
    us->inst.cp += sizeof(u8_t) ;
    
    // interrupt
    us->ker.reg[US_REG_IP] += us->inst.cp ;
    return us_int(us, imm) ;
  }
  
  case 0x09 :   // iret
    return us_iret(us) ;
  
  case 0x0A :   // cmp r8  r/m8
  case 0x0B : { // cmp r32 r/m32
    __init_modrm_0
    __get_modrm_reg(us, us->inst.oprd_size, &a.u)
    __get_modrm_rm(us, us->inst.oprd_size, &b.u)
    c.u = a.u - b.u ;
  } break ;
  
  case 0x0C :   // cmp r/m8  r8
  case 0x0D : { // cmp r/m32 r32
    __init_modrm_0
    __get_modrm_reg(us, us->inst.oprd_size, &b.u)
    __get_modrm_rm(us, us->inst.oprd_size, &a.u)
    c.u = a.u - b.u ;
  } break ;
  
  case 0x0E : { // int 3 (breakpoint)
    us->ker.reg[US_REG_IP] += us->inst.cp ;
    return us_int(us, US_IRQ_BREAKPOINT) ;
  }
  
  case 0x0F : {
    switch (us->inst.op[1]) {
    case 0x00 : case 0x01 : case 0x02 : case 0x03 :
    case 0x04 : case 0x05 : case 0x06 : case 0x07 :
    case 0x08 : case 0x09 : case 0x0A : case 0x0B :
    case 0x0C : case 0x0D : case 0x0E : case 0x0F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x10 : case 0x11 : case 0x12 : case 0x13 :
    case 0x14 : case 0x15 : case 0x16 : case 0x17 :
    case 0x18 : case 0x19 : case 0x1A : case 0x1B :
    case 0x1C : case 0x1D : case 0x1E : case 0x1F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x20 : case 0x21 : case 0x22 : case 0x23 :
    case 0x24 : case 0x25 : case 0x26 : case 0x27 :
    case 0x28 : case 0x29 : case 0x2A : case 0x2B :
    case 0x2C : case 0x2D : case 0x2E : case 0x2F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x30 : case 0x31 : case 0x32 : case 0x33 :
    case 0x34 : case 0x35 : case 0x36 : case 0x37 :
    case 0x38 : case 0x39 : case 0x3A : case 0x3B :
    case 0x3C : case 0x3D : case 0x3E : case 0x3F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x40 : case 0x41 : case 0x42 : case 0x43 :
    case 0x44 : case 0x45 : case 0x46 : case 0x47 :
    case 0x48 : case 0x49 : case 0x4A : case 0x4B :
    case 0x4C : case 0x4D : case 0x4E : case 0x4F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x50 : case 0x51 : case 0x52 : case 0x53 :
    case 0x54 : case 0x55 : case 0x56 : case 0x57 :
    case 0x58 : case 0x59 : case 0x5A : case 0x5B :
    case 0x5C : case 0x5D : case 0x5E : case 0x5F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x60 : case 0x61 : case 0x62 : case 0x63 :
    case 0x64 : case 0x65 : case 0x66 : case 0x67 :
    case 0x68 : case 0x69 : case 0x6A : case 0x6B :
    case 0x6C : case 0x6D : case 0x6E : case 0x6F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x70 : case 0x71 : case 0x72 : case 0x73 :
    case 0x74 : case 0x75 : case 0x76 : case 0x77 :
    case 0x78 : case 0x79 : case 0x7A : case 0x7B :
    case 0x7C : case 0x7D : case 0x7E : case 0x7F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x80 : case 0x81 : case 0x82 : case 0x83 :
    case 0x84 : case 0x85 : case 0x86 : case 0x87 :
    case 0x88 : case 0x89 : case 0x8A : case 0x8B :
    case 0x8C : case 0x8D : case 0x8E : case 0x8F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0x90 : case 0x91 : case 0x92 : case 0x93 :
    case 0x94 : case 0x95 : case 0x96 : case 0x97 :
    case 0x98 : case 0x99 : case 0x9A : case 0x9B :
    case 0x9C : case 0x9D : case 0x9E : case 0x9F :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0xA0 : case 0xA1 : case 0xA2 : case 0xA3 :
    case 0xA4 : case 0xA5 : case 0xA6 : case 0xA7 :
    case 0xA8 : case 0xA9 : case 0xAA : case 0xAB :
    case 0xAC : case 0xAD : case 0xAE : case 0xAF :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0xB0 : case 0xB1 : case 0xB2 : case 0xB3 :
    case 0xB4 : case 0xB5 : case 0xB6 : case 0xB7 :
    case 0xB8 : case 0xB9 : case 0xBA : case 0xBB :
    case 0xBC : case 0xBD : case 0xBE : case 0xBF :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0xC0 : case 0xC1 : case 0xC2 : case 0xC3 :
    case 0xC4 : case 0xC5 : case 0xC6 : case 0xC7 :
    case 0xC8 : case 0xC9 : case 0xCA : case 0xCB :
    case 0xCC : case 0xCD : case 0xCE : case 0xCF :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0xD0 : case 0xD1 : case 0xD2 : case 0xD3 :
    case 0xD4 : case 0xD5 : case 0xD6 : case 0xD7 :
    case 0xD8 : case 0xD9 : case 0xDA : case 0xDB :
    case 0xDC : case 0xDD : case 0xDE : case 0xDF :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0xE0 : case 0xE1 : case 0xE2 : case 0xE3 :
    case 0xE4 : case 0xE5 : case 0xE6 : case 0xE7 :
    case 0xE8 : case 0xE9 : case 0xEA : case 0xEB :
    case 0xEC : case 0xED : case 0xEE : case 0xEF :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    case 0xF0 : case 0xF1 : case 0xF2 : case 0xF3 :
    case 0xF4 : case 0xF5 : case 0xF6 : case 0xF7 :
    case 0xF8 : case 0xF9 : case 0xFA : case 0xFB :
    case 0xFC : case 0xFD : case 0xFE : case 0xFF :
      __raise(us, US_IRQ_UNDEFINED_INST) ;
    
    default :
      __raise(us, US_IRQ_NON_MASKABLE) ;
    }
  } break ;
  
  case 0x10 : case 0x11 : case 0x12 : case 0x13 :
  case 0x14 : case 0x15 : case 0x16 : case 0x17 :
  case 0x18 : case 0x19 : case 0x1A : case 0x1B :
  case 0x1C : case 0x1D : case 0x1E : case 0x1F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0x20 : case 0x21 : case 0x22 : case 0x23 :
  case 0x24 : case 0x25 : case 0x26 : case 0x27 :
  case 0x28 : case 0x29 : case 0x2A : case 0x2B :
  case 0x2C : case 0x2D : case 0x2E : case 0x2F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0x30 : case 0x31 : case 0x32 : case 0x33 :
  case 0x34 : case 0x35 : case 0x36 : case 0x37 :
  case 0x38 : case 0x39 : case 0x3A : case 0x3B :
  case 0x3C : case 0x3D : case 0x3E : case 0x3F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0x40 : case 0x41 : case 0x42 : case 0x43 :
  case 0x44 : case 0x45 : case 0x46 : case 0x47 :
  case 0x48 : case 0x49 : case 0x4A : case 0x4B :
  case 0x4C : case 0x4D : case 0x4E : case 0x4F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0x50 : case 0x51 : case 0x52 : case 0x53 :
  case 0x54 : case 0x55 : case 0x56 : case 0x57 :
  case 0x58 : case 0x59 : case 0x5A : case 0x5B :
  case 0x5C : case 0x5D : case 0x5E : case 0x5F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0x60 : case 0x61 : case 0x62 : case 0x63 :
  case 0x64 : case 0x65 : case 0x66 : case 0x67 :
    __raise(us, US_IRQ_NON_MASKABLE) ;
  
  case 0x68 : case 0x69 : case 0x6A : case 0x6B :
  case 0x6C : case 0x6D : case 0x6E : case 0x6F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0x70 : case 0x71 : case 0x72 : case 0x73 :
  case 0x74 : case 0x75 : case 0x76 : case 0x77 :
  case 0x78 : case 0x79 : case 0x7A : case 0x7B :
  case 0x7C : case 0x7D : case 0x7E : case 0x7F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0x80 : case 0x81 : case 0x82 : case 0x83 :
  case 0x84 : case 0x85 : case 0x86 : case 0x87 :
  case 0x88 : case 0x89 : case 0x8A : case 0x8B :
  case 0x8C : case 0x8D : case 0x8E : case 0x8F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0x90 : case 0x91 : case 0x92 : case 0x93 :
  case 0x94 : case 0x95 : case 0x96 : case 0x97 :
  case 0x98 : case 0x99 : case 0x9A : case 0x9B :
  case 0x9C : case 0x9D : case 0x9E : case 0x9F :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0xA0 : case 0xA1 : case 0xA2 : case 0xA3 :
  case 0xA4 : case 0xA5 : case 0xA6 : case 0xA7 :
  case 0xA8 : case 0xA9 : case 0xAA : case 0xAB :
  case 0xAC : case 0xAD : case 0xAE : case 0xAF :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0xB0 : case 0xB1 : case 0xB2 : case 0xB3 :
  case 0xB4 : case 0xB5 : case 0xB6 : case 0xB7 :
  case 0xB8 : case 0xB9 : case 0xBA : case 0xBB :
  case 0xBC : case 0xBD : case 0xBE : case 0xBF :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0xC0 : case 0xC1 : case 0xC2 : case 0xC3 :
  case 0xC4 : case 0xC5 : case 0xC6 : case 0xC7 :
  case 0xC8 : case 0xC9 : case 0xCA : case 0xCB :
  case 0xCC : case 0xCD : case 0xCE : case 0xCF :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0xD0 : case 0xD1 : case 0xD2 : case 0xD3 :
  case 0xD4 : case 0xD5 : case 0xD6 : case 0xD7 :
  case 0xD8 : case 0xD9 : case 0xDA : case 0xDB :
  case 0xDC : case 0xDD : case 0xDE : case 0xDF :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0xE0 : case 0xE1 : case 0xE2 : case 0xE3 :
  case 0xE4 : case 0xE5 : case 0xE6 : case 0xE7 :
  case 0xE8 : case 0xE9 : case 0xEA : case 0xEB :
  case 0xEC : case 0xED : case 0xEE : case 0xEF :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  case 0xF0 : case 0xF1 : case 0xF2 : case 0xF3 :
  case 0xF4 : case 0xF5 : case 0xF6 : case 0xF7 :
  case 0xF8 : case 0xF9 : case 0xFA : case 0xFB :
  case 0xFC : case 0xFD : case 0xFE : case 0xFF :
    __raise(us, US_IRQ_UNDEFINED_INST) ;
  
  default :
    __raise(us, US_IRQ_NON_MASKABLE) ;
  }
  
  // update the next instruction pointer
  us->ker.reg[US_REG_IP] += us->inst.cp ;
  
  return US_N_IRQS ;
}

u32_t us_clock (
  us_t * us
)
{
  // check the clocks
  if (us->opt.max_clocks != us->ker.reg[US_REG_CLOCK])
    return us_int(us, US_IRQ_OUT_OF_CLOCKS) ;

  if (0 == us->inst.has_REP) {
    // clear the previous instruction
    memset(&us->inst, 0, sizeof(us->inst)) ;
    
    // fetch the instruction
    if (US_N_IRQS != __fetch_inst(us))
      return us->IRQ ;
  }
  
  if (0 != us->opt.verbose) {
    fprintf(
      stderr                    ,
      ">>> Clock %llu (0x%04X:%012llX)\n"
      "... | %02X %02X %02X %02X\n"
      "... | %02X %02X %02X %02X\n"
      "... | %02X %02X %02X %02X\n"
      "... | %02X %02X %02X %02X\n" ,
      us->ker.reg[US_REG_CLOCK] ,
      us->ker.seg[US_SEG_CODE]  ,
      us->ker.reg[US_REG_IP]    ,
      us->inst.code[0], us->inst.code[1], us->inst.code[2], us->inst.code[3],
      us->inst.code[8], us->inst.code[5], us->inst.code[6], us->inst.code[7],
      us->inst.code[8], us->inst.code[9], us->inst.code[10], us->inst.code[11],
      us->inst.code[12], us->inst.code[13], us->inst.code[14], us->inst.code[15]
    ) ;
  }
  
  // execute the instruction
  if (US_N_IRQS != __exec_inst(us))
    return us->IRQ ;
  
  // update the clock counter
  ++us->ker.reg[US_REG_CLOCK] ;
  
  return US_N_IRQS ;
}
