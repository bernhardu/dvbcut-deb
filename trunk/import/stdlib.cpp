#include "stdlib.h"

int vasprintf (char**s, const char* fmt, va_list va){
*s=(char *)malloc(1024);
return vsprintf(*s,fmt,va);
}

char *realpath (const char *path, char *resolved) 
{
 strcpy(resolved,path); return resolved; 
}

int asprintf (char **text, const char *fmt, ...)
{
  va_list ap;
  va_start(ap,fmt);
  return vasprintf(text,fmt,ap);
}
