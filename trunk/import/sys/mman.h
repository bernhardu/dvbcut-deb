/* sys/mman.h

   Copyright 1996, 1997, 1998, 2000, 2001 Red Hat, Inc.

This file is part of Cygwin.

This software is a copyrighted work licensed under the terms of the
Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
details. */

#ifndef _SYS_MMAN_H_
#define _SYS_MMAN_H_
#include "windows.h"
#include "io.h"

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

#define MAP_FILE 0
#define MAP_SHARED 1
#define MAP_PRIVATE 2
#define MAP_TYPE 0xF
#define MAP_FIXED 0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
/* Non-standard flag */
#define MAP_NORESERVE 0x4000	/* Don't reserve swap space for this mapping.
				   Page protection must be set explicitely
				   to access page. Only supported for anonymous
				   private mappings. */
#define MAP_AUTOGROW 0x8000	/* Grow underlying object to mapping size.
				   File must be opened for writing. */

#define MAP_FAILED ((void *)0)

/*
 * Flags for msync.
 */
#define MS_ASYNC 1
#define MS_SYNC 2
#define MS_INVALIDATE 4
#include <unistd.h>
#include <stdio.h>

#define L 10

struct ah { void *addr; HANDLE h; };
static struct ah ahs[L];
void breakit() {}
static void *mmap (void *__addr, size_t __len, int __prot, int __flags, int __fd, off64_t __off)
{
	HANDLE FILE=(HANDLE)_get_osfhandle(__fd);
	HANDLE fm=CreateFileMapping(FILE,NULL,(__prot==PROT_READ ? PAGE_READONLY : PAGE_READWRITE),0,0,NULL);
	if(fm==0)
	{
	 DWORD error=GetLastError();
	 return MAP_FAILED;
	}	
	void *d=MapViewOfFile( fm,(__prot==PROT_READ ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS),__off>>32,__off&0xFFFFFFFF,__len);
	
	for(int i=0;i<L;++i)
	{
		if(ahs[i].addr==0)
		   {
			ahs[i].addr=d;
			ahs[i].h=fm;
			break;
		   }
	}			
	if(d==0)
	{
	breakit();
	   DWORD error=GetLastError();
	   d=(void*)malloc(__len);
	   lseek(__fd,__off,SEEK_SET);
	   read(__fd,d,__len);
//	   fprintf(stderr,"MALLOC pos %d len %d at %x: error %x\n",(long)__off & 0xFFFFFFFF,__len,d,GetLastError());
	}	
	else
	{
	//	  fprintf(stderr,"MAP pos %d len %d at %x: error %x\n",(long)__off & 0xFFFFFFFF,__len,d,GetLastError());
	}	
	return d;
}
static int munmap (void *__addr, size_t __len)
{
//	fprintf(stderr,"UNMAP %x\n",__addr);
	if(UnmapViewOfFile(__addr)==0)  free(__addr);
	for(int i=0;i<L;++i)
	{
		if(ahs[i].addr==__addr)
		   {
			CloseHandle(ahs[i].h);
			DWORD dw=GetLastError();
			ahs[i].addr=0;
			break;
		   }
	}			

}

extern int mprotect (void *__addr, size_t __len, int __prot);
extern int msync (void *__addr, size_t __len, int __flags);
extern int mlock (const void *__addr, size_t __len);
extern int munlock (const void *__addr, size_t __len);

static long sysconf(int name)
{
 SYSTEM_INFO info;
 GetSystemInfo(&info);
 return info.dwAllocationGranularity;
}

#define _SC_PAGESIZE 4096

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /*  _SYS_MMAN_H_ */
