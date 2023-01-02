#include "us.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// =============================================================================
// Image Loader
// -----------------------------------------------------------------------------
// Load the operating system image:
//   1.  open the file
//   2.  check the magic number (4-byte)
//   3.  read the memory size (in KiB)
//   4.  read the kernel address (8-bit)
//   5.  read the kernel size (8-byte)
//   6.  read the kernel entry point (8-byte)
//   7.  check the bounds
//   8.  allocate the memory 
//   9.  read the kernel from the image and write it into the memory using the
//       previous address
//   9.  close the file
//   10. clear the registers and segment registers, instruction data
//   11. set the instruction pointer to the entry point
// =============================================================================

u32_t us_load_img (
        us_t * us ,
  const char * fn
)
{
  FILE * fp ;
  
#ifdef _WIN32
  fp = fopen(fn, "rb") ; // Windows needs `b` (binary) flag to work correctly
#else
  fp = fopen(fn, "r") ;
#endif
  
  if (NULL == fp) {
    fprintf(stderr, "error: cannot open the image `%s`\n", fn) ;
    return 1 ;
  }
  
  // image follows the big endian byte order
  
  u8_t mag_num [4] ;
  mag_num[0] = fgetc(fp) ;
  mag_num[1] = fgetc(fp) ;
  mag_num[2] = fgetc(fp) ;
  mag_num[3] = fgetc(fp) ;
  
  if (
    0x45 != mag_num[0] || 0x45 != mag_num[1] ||
    0xFA != mag_num[2] || 0xDE != mag_num[3] ||
  ) {
    fprintf(
      stderr, "error: unknown image magic number 0x%02X%02X%02X%02X\n",
      mag_num[0], mag_num[1], mag_num[2], mag_num[3]
    ) ;
    return 1 ;
  }
  
  // read the header
  
  u64_t mem_size ;
  u64_t ker_addr ;
  u64_t ker_size ;
  u64_t ker_jump ;
  
  if (1 != fread(&mem_size, sizeof(u64_t), 1, fp)) {
    fclose(fp) ;
    fprintf(stderr, "error: cannot read the memory size: %s", strerror(errno)) ;
    return 1 ;
  }
  
  mem_size <<= 10 ; // `mem_size` * 1 KiB
  
  if (1 != fread(&ker_addr, sizeof(u64_t), 1, fp)) {
    fclose(fp) ;
    fprintf(stderr, "error: cannot read the kernel address: %s", strerror(errno)) ;
    return 1 ;
  }
  
  if (1 != fread(&ker_size, sizeof(u64_t), 1, fp)) {
    fclose(fp) ;
    fprintf(stderr, "error: cannot read the kernel size: %s", strerror(errno)) ;
    return 1 ;
  }
  
  if (1 != fread(&ker_jump, sizeof(u64_t), 1, fp)) {
    fclose(fp) ;
    fprintf(stderr, "error: cannot read the kernel entry point\n") ;
    return 1 ;
  }
  
  // check the bounds
  
  if (mem_size < ker_addr + ker_size) {
    fclose(fp) ;
    fprintf(stderr, "error: kernel is out of memory\n") ;
    return 1 ;
  }
  
  if (ker_size <= ker_jump) {
    fclose(fp) ;
    fprintf(stderr, "error: kernel entry point is out of kernel\n") ;
    return 1 ;
  }
  
  // allocate the memory
  
  us->mem.size = mem_size ;
  us->mem.data = (u8_t *)malloc(mem_size * sizeof(u8_t)) ;
  
  if (NULL == us->mem.data) {
    fprintf(stderr, "error: cannot allocate the memory: %s", strerror(errno)) ;
    return 1 ;
  }
  
  // read the kernel
  
  if (ker_size != fread(us->mem.data + ker_addr, sizeof(u8_t), ker_size, fp)) {
    fclose(fp) ;
    free(us->mem.data) ;
    fprintf(stderr, "error: cannot read the kernel: %s", strerror(errno)) ;
    return 1 ;
  }
  
  // close the image
  
  fclose(fp) ;
  
  // clear the registers, segment registers and instruction data
  
  for (int i = 0 ; i < US_N_REGS ; ++i)
    us->ker.reg[i] = 0 ;
  
  for (int i = 0 ; i < US_N_SEGS ; ++i)
    us->ker.seg[i] = 0 ;
  
  memset(&us->inst, 0, sizeof(us->inst)) ;
  
  // set the entry point

  us->ker.reg[US_REG_IP] = ker_addr + ker_jump ;
  
  return 0 ;
}

// =============================================================================
// Memory and Segments
// -----------------------------------------------------------------------------
// There are two types of address spaces: physical and virtual. The machine
// starts using the physical address space and, then, the operating system
// switches to the virtual loading its Segment Descriptor Table (SDT)
// -----------------------------------------------------------------------------
// Convert the address:
//   1. check if the machine are using the virtualalization
//   If yes:
//     1. read the Segment Descriptor Entry (SDE) of the passed segment
//     2. decode the size and the origin address of the segment
//     3. check the permissions
//     4. check the bounds according to the flags
//   If not:
//     1. check the bounds according to the flags
// Write/read to/from memory:
//   1. convert the virtual address into a physical address
//   2. write/read the data
// =============================================================================

u32_t __convert_addr (
  us_t *  us    ,
  u16_t   _segx ,
  u64_t * _addr ,
  u64_t * _size ,
  u32_t   _perm
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
    ) // raise a special interrupt
      return us_int(us, US_IRQ_SEGMENT_FAULT) ;
    
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
    
    if (0 != us->opt.verbose) {
      fprintf(
        stderr                             ,
        ">>> Segment 0x%04X :\n"
        "... | address      : 0x%012llX\n"
        "... | size         : 0x%012llX\n"
        "... | permissions  : 0x%02X\n"    ,
        addr, size, ((SDE >> 24) & 15)
      ) ;
    }
    
    // check permissions and privilege level
    
    _perm |= US_SEG_PERM_P ;
    
    if (
      (_perm & ((SDE >> 24) & 15)) != _perm                       || // check permissions
      ((SDE >> 28) & 3) < ((us->ker.reg[US_REG_FLAGS] >> 12) & 3)    // check privilege level
    ) // raise the interrupt
      return us_int(us, US_IRQ_SEGMENT_PROTECT) ;
    
    // check bounds
    
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_IB)) {
      // check address
      if (size < *_addr)
        return us_int(us, US_IRQ_SEGMENT_FAULT) ;
    
      // resize the data
      if (size < *_addr + *_size)
        *_size = size - *_addr ;
    } else if (size < *_addr + *_size)
      return us_int(us, US_IRQ_SEGMENT_FAULT) ;
    
    // set the physical address
    *_addr += addr ;
  } else {
    if (0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_IB)) {
      // check address
      if (us->mem.size < *_addr)
        return us_int(us, US_IRQ_SEGMENT_FAULT) ;
    
      // resize the data
      if (us->mem.size < *_addr + *_size)
        *_size = us->mem.size - *_addr ;
    } else if (us->mem.size < *_addr + *_size)
      return us_int(us, US_IRQ_SEGMENT_FAULT) ;
  }
  
  return US_N_IRQS ;
}

u32_t us_write (
        us_t * us   ,
        u16_t  segx ,
        u64_t  addr ,
        u64_t  size ,
  const any_t  data
)
{
  // convert the virtual address `segx`:`addr` to a physical address
  if (US_N_IRQS != __convert_addr(us, segx, &addr, &size, US_SEG_PERM_W))
    return us->IRQ ;
    
  // write `data` into the memory
  
  u8_t * dst = (u8_t *)(us->mem.data + addr) ;
  u8_t * src = (u8_t *)data ;
  
  if (0 != us->opt.verbose) {
    fprintf(
      stderr                                        ,
      ">>> Write at 0x%012llX (size: %llu bytes)\n" ,
      addr, size
    ) ;
  }
  
  for (u64_t i = 0 ; i < size ; ++i)
    dst[i] = src[i] ;
  
  return US_N_IRQS ;
}

u32_t us_read (
  us_t * us   ,
  u16_t  segx ,
  u64_t  addr ,
  u64_t  size ,
  any_t  data
)
{
  // convert the virtual address `segx`:`addr` to a physical address
  if (US_N_IRQS != __convert_addr(us, segx, &addr, &size, US_SEG_PERM_R))
    return us->IRQ ;
    
  // read `data` from memory
  
  u8_t * dst = (u8_t *)data ;
  u8_t * src = (u8_t *)(us->mem.data + addr) ;
  
  if (0 != us->opt.verbose) {
    fprintf(
      stderr                                       ,
      ">>> Read at 0x%012llX (size: %llu bytes)\n" ,
      addr, size
    ) ;
  }
  
  for (u64_t i = 0 ; i < size ; ++i)
    dst[i] = src[i] ;
  
  return US_N_IRQS ;
}

// =============================================================================
// Interrupt
// -----------------------------------------------------------------------------
// Interrupt:
//   1. read from memory the interrupt service routine
//   2. push onto the stack the basic context:
//     1. push the register FLAGS (32-bit) onto the stack
//     2. assemble the segment register CODE with the register IP to construct
//        the far pointer (16:48-bit)
//     3. push the constructed far pointer (64-bit) onto the stack
//   3. change the interrupt enable flag to avoid that the called interrupt
//      can raise another interrupt without permission
//   4. disassemble the far pointer of the interrupt service routine into the
//      destination segment and offset
//   5. set the segment register CODE and the register IP
// Return from interrupt:
//   1. pop the return far address from the stack
//   2. disassemble the far pointer of the previous process into the destination
//      segment and offset
//   3. set the segment register CODE and the register IP
//   4. pop the lower 32-bit of the register FLAGS from the stack
// =============================================================================

u32_t us_int (
  us_t * us  ,
  u32_t  IRQ
)
{
  // check if the Interrupt ReQuest (IRQ) is masked
  // then, the VM cannot execute the code of the
  // relative Interrupt Service Routine (ISR)
  if (
    0 != (us->ker.reg[US_REG_FLAGS] & US_FLAG_I) ||
    US_IRQ_NON_MASKABLE == IRQ
  ) {    
    // read the ISR from the Interrupt Descriptor Table (IDT)
    
    u16_t segx = us->ker.reg[US_REG_IDT] >> 48 ;
    u64_t addr = (us->ker.reg[US_REG_IDT] << 16) >> 16 ;
    
    u64_t ISR ;
    
    if (
      US_N_IRQS != us_read(
        us, segx, addr + IRQ * sizeof(ISR), sizeof(ISR), &ISR
      )
    ) // raise a special interrupt
      return us_int(us, US_IRQ_INTERRUPT_FAULT) ;
    
    // push the basic context onto the stack
    
    if (
      US_N_IRQS != us_push(
        us, sizeof(u32_t), us->ker.reg + US_REG_FLAGS
      )
    ) // raise a special interrupt
      return us_int(us, US_IRQ_INTERRUPT_FAULT) ;
    
    addr = ((u64_t)us->ker.seg[US_SEG_CODE] << 48) | us->ker.reg[US_REG_IP] ;
    
    if (
      US_N_IRQS != us_push(
        us, sizeof(u64_t), &addr
      )
    ) // raise a special interrupt
      return us_int(us, US_IRQ_INTERRUPT_FAULT) ;
    
    // reset the interrupt enable flag
    us->ker.reg[US_REG_FLAGS] &= ~US_FLAG_I ;
    
    // call the ISR
    
    us->ker.seg[US_SEG_CODE] = ISR >> 48 ;
    us->ker.reg[US_REG_IP] = (ISR << 16) >> 16 ;
  }
  
  if (0 != us->opt.verbose) {
    fprintf(
      stderr                   ,
      ">>> Interrupt 0x%02X\n" ,
      IRQ
    ) ;
  }
  
  // set the last IRQ
  return us->IRQ = IRQ ;
}

u32_t us_iret (
  us_t * us
)
{
  // pop the basic context from the stack
  
  u64_t addr ;
  
  if (
    US_N_IRQS != us_push(
      us, sizeof(u64_t), &addr
    )
  ) // raise a special interrupt
    return us_int(us, US_IRQ_INTERRUPT_FAULT) ;
  
  us->ker.seg[US_SEG_CODE] = addr >> 48 ;
  us->ker.reg[US_REG_IP] = (addr << 16) >> 16 ;
  
  if (
    US_N_IRQS != us_pop(
      us, sizeof(u32_t), us->ker.reg + US_REG_FLAGS
    )
  ) // raise a special interrupt
    return us_int(us, US_IRQ_INTERRUPT_FAULT) ;
  
  if (0 != us->opt.verbose) {
    fprintf(
      stderr                        ,
      ">>> Returning from 0x%02X\n" ,
      us->IRQ
    ) ;
  }
  
  // reset the interrupt request
  return us->IRQ = US_N_IRQS ;
}

// =============================================================================
// Stack
// -----------------------------------------------------------------------------
//   0                       memory max address
//   | ... |   << Stack | ... |
// -----------------------------------------------------------------------------
// Push onto the stack:
//   1. decrease the stack pointer
//   2. write into the segment
// Pop from the stack:
//   1. read from the segment
//   2. increase the stack pointer
// =============================================================================

u32_t us_push (
        us_t * us   ,
        u64_t  size ,
  const any_t  data
)
{
  // update the Stack top Pointer (SP)
  us->ker.reg[US_REG_SP] -= size ;
  
  // write `data` onto the stack
  if (
    US_N_IRQS != us_write(
      us, us->ker.seg[US_SEG_STACK], us->ker.reg[US_REG_SP], size, data
    )
  ) // raise a special segment fault interrupt
    return us_int(us, US_IRQ_STACK_OVERFLOW) ;
  
  return US_N_IRQS ;
}

u32_t us_pop (
  us_t * us   ,
  u64_t  size ,
  any_t  data
)
{
  // read `data` from the stack
  if (
    US_N_IRQS != us_read(
      us, us->ker.seg[US_SEG_STACK], us->ker.reg[US_REG_SP], size, data
    )
  ) // raise a special segment fault interrupt
    return us_int(us, US_IRQ_STACK_UNDERFLOW) ;
  
  // update the Stack top Pointer (SP)
  us->ker.reg[US_REG_SP] += size ;
  
  return US_N_IRQS ;
}
