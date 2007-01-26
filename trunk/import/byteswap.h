/* byteswap.h

Copyright 2005 Red Hat, Inc.

This file is part of Cygwin.

This software is a copyrighted work licensed under the terms of the
Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
details. */

#ifndef _BYTESWAP_H
#define _BYTESWAP_H

#ifdef __cplusplus
extern "C" {
#endif
#define __bswap_constant_16 __bswap_16
#define __bswap_constant_32 __bswap_32
#define __bswap_constant_64 __bswap_64
#define __bswap_16(x) \
     ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))

#define __bswap_32(x) \
      ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |               \
       (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))


#define __bswap_64(x) \
     ((((x) & 0xff00000000000000ull) >> 56)				      \
      | (((x) & 0x00ff000000000000ull) >> 40)				      \
      | (((x) & 0x0000ff0000000000ull) >> 24)				      \
      | (((x) & 0x000000ff00000000ull) >> 8)				      \
      | (((x) & 0x00000000ff000000ull) << 8)				      \
      | (((x) & 0x0000000000ff0000ull) << 24)				      \
      | (((x) & 0x000000000000ff00ull) << 40)				      \
      | (((x) & 0x00000000000000ffull) << 56))

#ifdef __cplusplus
}
#endif
#endif /* _BYTESWAP_H */
