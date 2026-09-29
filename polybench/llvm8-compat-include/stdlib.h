#ifndef LLVM8_COMPAT_STDLIB_H
#define LLVM8_COMPAT_STDLIB_H
typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);
void free(void *);
int atoi(const char *);
#endif
