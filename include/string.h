#if !defined(TARGET_N64) && defined(__cplusplus)
/* PC port: this header shadows the toolchain's <string.h>; C++ TUs (fast3d)
 * defer to the real one, which libstdc++ needs. C TUs keep the minimal stub,
 * because the hosted header's bcopy/bzero/bcmp conflict with PR/os.h. */
#include_next <string.h>
#else

#ifndef _STRING_H_
#define _STRING_H_
#include <PR/ultratypes.h>

extern void *memcpy(void *, const void *, size_t);
extern unsigned char *strchr(const unsigned char *, int);
extern size_t strlen(const unsigned char *);

#ifndef TARGET_N64
/* PC port: the rest of the C library's string functions the port layer calls. Without these the
 * C TUs (built -ffreestanding, so no builtins) declared them implicitly as int f(): harmless for
 * int/pointer returns on i386 by luck, but -Werror=implicit-function-declaration (CMakeLists.txt,
 * port/ sources) now keeps them declared. strcpy/strncpy/strcat/strcmp/strncmp are src/str.c's
 * (same signatures, src/str.h). */
extern void *memset(void *, int, size_t);
extern int memcmp(const void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern void *memchr(const void *, int, size_t);
extern char *strcpy(char *, const char *);
extern char *strncpy(char *, const char *, size_t);
extern char *strcat(char *, const char *);
extern int strcmp(const char *, const char *);
extern int strncmp(const char *, const char *, size_t);
extern char *strstr(const char *, const char *);
extern char *strrchr(const char *, int);
extern char *strtok(char *, const char *);
extern size_t strspn(const char *, const char *);
extern size_t strcspn(const char *, const char *);
extern char *strerror(int);
#ifdef _WIN32
extern int _stricmp(const char *, const char *);   /* msvcrt */
#endif
#endif

#endif
#endif /* TARGET_N64 */
