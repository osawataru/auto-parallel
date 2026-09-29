#ifndef LLVM8_COMPAT_STDIO_H
#define LLVM8_COMPAT_STDIO_H
typedef struct _IO_FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
int fprintf(FILE *, const char *, ...);
#endif
