#ifndef __WINE_FFMPEG_CONFIG_H
#define __WINE_FFMPEG_CONFIG_H

#if defined(__i386__)
#  define ARCH_X86                      1
#  define ARCH_X86_32                   1
#  define EXTERN_PREFIX                 "_"
#  define EXTERN_ASM                    _
#elif defined(__x86_64__) || defined(__arm64ec__)
#  define ARCH_X86                      1
#  define ARCH_X86_64                   1
#  define HAVE_ALIGNED_STACK            1
#elif defined(__aarch64__)
#  define ARCH_AARCH64                  1
#  define HAVE_ALIGNED_STACK            1
#elif defined(__arm__)
#  define ARCH_ARM                      1
#else
#  error "Unsupported platform"
#endif

#define HAVE_FAST_64BIT                 1
#define HAVE_FAST_CLZ                   1
#define HAVE_FAST_CMOV                  1
#define HAVE_FAST_FLOAT16               0
#define HAVE_FAST_UNALIGNED             1

#define HAVE_I686_INLINE                (ARCH_X86 && HAVE_INLINE_ASM)
#define HAVE_INTRINSICS_NEON            (ARCH_ARM || ARCH_AARCH64)
#define HAVE_INTRINSICS_SSE2            HAVE_SSE2
#define HAVE_SIMD_ALIGN_16              (HAVE_ALTIVEC || HAVE_NEON || HAVE_SSE)
#define HAVE_SIMD_ALIGN_32              HAVE_AVX
#define HAVE_SIMD_ALIGN_64              HAVE_AVX512
#define PIC                             CONFIG_PIC

#define HAVE_DIRECT_H                   1
#define HAVE_IO_H                       1
#define HAVE_MALLOC_H                   1
#define HAVE_STDARG_H                   1
#define HAVE_WINDOWS_H                  1
#define HAVE_WINSOCK2_H                 1

#define HAVE_ATAN2F                     1
#define HAVE_ATANF                      1
#define HAVE_CBRT                       1
#define HAVE_CBRTF                      1
#define HAVE_COPYSIGN                   1
#define HAVE_COSF                       1
#define HAVE_ERF                        1
#define HAVE_EXP2                       1
#define HAVE_EXP2F                      1
#define HAVE_EXPF                       1
#define HAVE_HYPOT                      1
#define HAVE_ISFINITE                   1
#define HAVE_ISINF                      1
#define HAVE_ISNAN                      1
#define HAVE_LDEXPF                     1
#define HAVE_LLRINT                     1
#define HAVE_LLRINTF                    1
#define HAVE_LOG10F                     1
#define HAVE_LOG2                       1
#define HAVE_LOG2F                      1
#define HAVE_LRINT                      1
#define HAVE_LRINTF                     1
#define HAVE_POWF                       1
#define HAVE_RINT                       1
#define HAVE_ROUND                      1
#define HAVE_ROUNDF                     1
#define HAVE_SINF                       1
#define HAVE_TRUNC                      1
#define HAVE_TRUNCF                     1

#define HAVE_ALIGNED_MALLOC             1
#define HAVE_BCRYPT                     1
#define HAVE_CLOSESOCKET                1
#define HAVE_GETADDRINFO                1
#define HAVE_GETPROCESSAFFINITYMASK     1
#define HAVE_GETSTDHANDLE               1
#define HAVE_GETSYSTEMTIMEASFILETIME    1
#define HAVE_GLOB                       0
#define HAVE_INET_ATON                  1
#define HAVE_ISATTY                     1
#define HAVE_MAPVIEWOFFILE              1
#define HAVE_SETCONSOLETEXTATTRIBUTE    1
#define HAVE_SLEEP                      1
#define HAVE_SOCKLEN_T                  1
#define HAVE_STRUCT_ADDRINFO            1
#define HAVE_STRUCT_POLLFD              1
#define HAVE_STRUCT_SOCKADDR_IN6        1
#define HAVE_STRUCT_SOCKADDR_SA_LEN     1
#define HAVE_STRUCT_SOCKADDR_STORAGE    1
#define HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC 0
#define HAVE_TEMPNAM                    0
#define HAVE_VIRTUALALLOC               1

#define HAVE_THREADS                    1
#define HAVE_W32THREADS                 1
#define HAVE_DOS_PATHS                  1
#define HAVE_PRAGMA_DEPRECATED          1

#include "default_config.h"

#ifndef static_assert
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ < 202311L)
#define static_assert(e, m) _Static_assert(e, m)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ < 201112L)
#define static_assert(e, m) extern void __C_ASSERT__(int [(e)?1:-1])
#else
#define static_assert(e, m) extern void __C_ASSERT__(int [(e)?1:-1])
#endif
#endif

#endif /* __WINE_FFMPEG_CONFIG_H */
