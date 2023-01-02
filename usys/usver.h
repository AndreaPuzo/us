#ifndef _USVER_H
# define _USVER_H

# define _US_VERSION_MAJOR 0
# define _US_VERSION_MINOR 0
# define _US_VERSION_PATCH 0

# define _US_VERSION            \
  (                             \
    (_US_VERSION_MAJOR << 16) | \
    (_US_VERSION_MINOR <<  8) | \
    (_US_VERSION_PATCH <<  0)   \
  )

#endif
