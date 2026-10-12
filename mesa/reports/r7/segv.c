// LD_PRELOAD: print a backtrace on SIGSEGV (no gdb on the device).  cc -O2 -shared -fPIC -o segv.so segv.c
#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static void handler(int sig)
{
	void *bt[64];
	int n = backtrace(bt, 64);
	fprintf(stderr, "SIGNAL %d, backtrace:\n", sig);
	backtrace_symbols_fd(bt, n, 2);
	_exit(139);
}
__attribute__((constructor)) static void init(void)
{
	signal(SIGSEGV, handler);
	signal(SIGBUS, handler);
}
