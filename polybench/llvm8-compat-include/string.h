#ifndef LLVM8_COMPAT_STRING_H
#define LLVM8_COMPAT_STRING_H
typedef __SIZE_TYPE__ size_t;
int strcmp(const char *, const char *);
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
#endif
