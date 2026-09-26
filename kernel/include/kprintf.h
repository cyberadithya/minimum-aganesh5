#ifndef KPRINTF_H
#define KPRINTF_H

/*
 * Minimal printf-like console logging for the kernel.
 * Supported conversions: %c %s %d %u %x %p %%
 */
void kprintf(const char *fmt, ...);

#endif
