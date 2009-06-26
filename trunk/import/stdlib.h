#ifndef MYSTDLIB
#define MYSTDLIB
#include_next <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

char *realpath (const char *path, char *resolved);
int vasprintf (char**s, const char* fmt, va_list va);
int asprintf (char **text, const char *fmt, ...);

typedef unsigned char u_int8_t;
typedef unsigned long u_int32_t; 

#define bzero(p,s) memset(p,0,s)


#endif
