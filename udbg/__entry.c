#include "usdbg.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int main (int argc, char ** argv)
{
  static us_t us ;
  static us_dbg_t dbg ;

  // check the arguments
  if (2 != argc) {
    fprintf(stderr, "fatal: no image\n") ;
    fprintf(stderr, "usage: %s [<option>...] <image>\n", argv[0]) ;
    exit(EXIT_FAILURE) ;
  }
  
  if ( // print the help page
    0 == strcmp(argv[1], "--help") ||
    0 == strcmp(argv[1], "-h")
  ) {
    fprintf(stderr, "usage: %s [<option>...] <image>\n", argv[0]) ;
    
    fprintf(
      stderr ,
      "options:\n"
      "  -h, --help            | print this help page\n"
      "  -v, --version         | print the version\n"
      "      --verbose         | print additional information\n"
      "  -c, --clocks <number> | set the limit of clocks\n"
    ) ;
    
    exit(EXIT_SUCCESS) ;
  }
  
  if ( // print the version
    0 == strcmp(argv[1], "--version") ||
    0 == strcmp(argv[1], "-v")
  ) {
    fprintf(
      stderr            ,
      "us %u.%u.%u\n"   ,
      _US_VERSION_MAJOR ,
      _US_VERSION_MINOR ,
      _US_VERSION_PATCH
    ) ;
    
    exit(EXIT_SUCCESS) ;
  }
  
  // init the debugger
  
  memset(&dbg, 0, sizeof(us_dbg_t)) ;
  
  // init the machine
  
  memset(&us, 0, sizeof(us_t)) ;
  us.opt.max_clocks = (u64_t)-1 ;
  
  // scan the arguments
  
  char * img = NULL ;
  
  for (int i = 1 ; i < argc ; ++i) {
    if (
      0 == strcmp(argv[i], "--clocks") ||
      0 == strcmp(argv[i], "-c")
    ) {
      if (i + 1 != argc) {
        ++i ;
        us.opt.max_clocks = strtoull(argv[i], NULL, 10) ;
      } else {
        fprintf(stderr, "error: missing argument for option `%s`\n", argv[i]) ;
        fprintf(stderr, "warning: option `%s` is ignored\n", argv[i]) ;
      }
    } else if (0 == strcmp(argv[i], "--verbose"))
      us.opt.verbose = 1 ;
    else
      img = argv[i] ;
  }
  
  // check the image existence
  if (NULL == img) {
    fprintf(stderr, "fatal: no image\n") ;
    fprintf(stderr, "usage: %s [<option>...] <image>\n", argv[0]) ;
    exit(EXIT_FAILURE) ;
  }
  
  // load the operating system
  if (0 != us_load_img(&us, img)) {
    fprintf(stderr, "fatal: something has gone wrong loading `%s`\n", img) ;
    exit(EXIT_FAILURE) ;
  }
  
  // start the machine
  us.ker.reg[US_REG_FLAGS] |= US_FLAG_1 ;
  
  // machine loop
  while (0 != (us.ker.reg[US_REG_FLAGS] & US_FLAG_1)) {
    u32_t IRQ = us_clock(&us) ;
    
    if (US_N_IRQS != IRQ) {
      if (US_IRQ_BREAKPOINT != IRQ && 0 != us.opt.verbose)
        fprintf(stderr, "interrupt: 0x%02X\n", us.IRQ) ;
      else { // start the debug
        u32_t res = us_debug(&us, &dbg) ;
        
        if (res < 0)
          break ;      
      }
    }
  }

  // deallocate the memory
  free(us.mem.data) ;
  
  // deallocate the breakpoints
  if (NULL == dbg.breakpointv)
    free(dbg.breakpointv) ;
  
  // close the debug file
  if (NULL != dbg.fp && dbg.fp != stdout && dbg.fp != stderr)
    fclose(dbg.fp) ;

  exit(EXIT_SUCCESS) ;
}
