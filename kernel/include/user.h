#ifndef USER_H
#define USER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "minemu/boot.h"

/*
 * User address space (adapted from examples/user-mode-hello).
 *
 * One page table covers VA [0x00400000, 0x00800000). Program segments are
 * loaded from USER_IMAGE_BASE up, and the last page is the user stack.
 */
#define USER_IMAGE_BASE UINT32_C(0x00400000)
#define USER_STACK_BOTTOM UINT32_C(0x007ff000)
#define USER_STACK_TOP UINT32_C(0x00800000)

/* Map the system image, find the module called name, copy its segments
 * into user pages, and create its stack. On success *entry is set to the
 * program's entry VA. */
bool user_load(const struct minemu_boot_info *boot_info, const char *name,
               uint32_t *entry);

/* Switch to USR mode and start running at entry with sp = stack_top.
 * Implemented in src/core/user_entry.S. */
void user_enter(uint32_t entry, uint32_t stack_top) __attribute__((noreturn));

/* Return true if every page in [vaddr, vaddr + length) is a user page
 * with the given access bit (MINEMU_PTE_READABLE or MINEMU_PTE_WRITABLE). */
bool user_range_ok(uint32_t vaddr, size_t length, uint32_t access);

/* Copy between kernel memory and user memory. Both check the user range
 * first and copy nothing (returning false) if it is not accessible. */
bool copy_from_user(void *dst, uint32_t user_src, size_t length);
bool copy_to_user(uint32_t user_dst, const void *src, size_t length);

#endif
