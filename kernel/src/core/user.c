#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "minemu/boot.h"
#include "minemu/mmu.h"
#include "minemu/platform.h"
#include "user.h"

/*
 * Loads a user module from the system image into its own address space.
 * The loader is adapted from examples/user-mode-hello/kernel/main.c.
 */

/* Page directory set up by src/startup/boot.S. */
#define BOOTSTRAP_PAGE_DIRECTORY_PADDR UINT32_C(0x40010000)
#define MAX_USER_IMAGE_PAGES 16U
#define SYSTEM_ROM_TABLE_COUNT 4U

/*
 * Physical pages for page tables, the program and its stack come from free
 * RAM right after the kernel image instead of from big static arrays.
 * The bootloader zeroes the kernel's .bss one byte at a time, so ~100 KiB of
 * static page arrays made booting take about a million ticks.
 *
 * The SVC/IRQ/ABT/UND stacks are the last thing in kernel.ld, so the UND
 * stack top marks the end of the kernel image.
 */
extern char __minemu_und_stack_top[];
static uint32_t next_free_paddr;

static uint32_t *user_page_table; /* kernel (direct map) VA of the table */
static uint32_t user_page_vaddrs[MAX_USER_IMAGE_PAGES];
static uint32_t user_page_paddrs[MAX_USER_IMAGE_PAGES];
static uint32_t user_page_flags[MAX_USER_IMAGE_PAGES];
static size_t user_page_count;

static void zero_bytes(void *destination, size_t length) {
    uint8_t *bytes = destination;
    for (size_t index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t index = 0; index < length; ++index) {
        out[index] = in[index];
    }
}

static bool bytes_equal(const uint8_t *left, const char *right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != (uint8_t)right[index]) {
            return false;
        }
    }
    return true;
}

static size_t string_length(const char *s) {
    size_t length = 0;
    while (s[length] != '\0') {
        length++;
    }
    return length;
}

/* Bounds checks for offsets into the system image. Records are 32 bytes. */
static bool image_range_valid(uint32_t offset, uint32_t length, uint32_t image_size) {
    return offset <= image_size && length <= image_size - offset;
}

static bool image_records_valid(uint32_t offset, uint32_t count, uint32_t image_size) {
    return offset <= image_size && count <= ((image_size - offset) >> 5);
}

/* Kernel statics live in the direct map, so VA -> PA is a fixed offset. */
static uint32_t kernel_vaddr_to_paddr(const void *address) {
    return (uint32_t)(uintptr_t)address - MINEMU_KERNEL_DIRECT_BASE + MINEMU_RAM_BASE;
}

static void *paddr_to_kernel_vaddr(uint32_t paddr) {
    return (void *)(uintptr_t)(paddr - MINEMU_RAM_BASE + MINEMU_KERNEL_DIRECT_BASE);
}

static volatile uint32_t *active_page_directory(void) {
    return paddr_to_kernel_vaddr(BOOTSTRAP_PAGE_DIRECTORY_PADDR);
}

/* Hand out the next free physical page, zeroed. Returns 0 if RAM is full.
 * Pages are never freed yet since there is only one program. */
static uint32_t alloc_page(void) {
    if (next_free_paddr == 0) {
        uint32_t kernel_end = kernel_vaddr_to_paddr(__minemu_und_stack_top);
        next_free_paddr = (kernel_end + MINEMU_PAGE_SIZE - 1U) & MINEMU_PTE_PAGE_MASK;
    }
    if (next_free_paddr >= MINEMU_RAM_BASE + MINEMU_RAM_SIZE) {
        return 0;
    }
    uint32_t paddr = next_free_paddr;
    next_free_paddr += MINEMU_PAGE_SIZE;

    /* Zero a word at a time; it's 4x fewer stores than bytes. */
    uint32_t *words = paddr_to_kernel_vaddr(paddr);
    for (uint32_t index = 0; index < MINEMU_PAGE_SIZE / 4U; ++index) {
        words[index] = 0;
    }
    return paddr;
}

/* Map the system image read-only (supervisor only) at its own address so
 * the kernel can read the module table and copy segments out of it. */
static bool map_system_rom(const struct minemu_boot_info *boot_info) {
    if (boot_info->system_rom_base != MINEMU_SYSTEM_ROM_BASE || boot_info->image_size == 0 ||
        boot_info->image_size > MINEMU_SYSTEM_ROM_SIZE) {
        return false;
    }

    uint32_t page_count = (boot_info->image_size + MINEMU_PAGE_SIZE - 1U) / MINEMU_PAGE_SIZE;
    uint32_t table_count = (page_count + 1023U) / 1024U;
    if (table_count > SYSTEM_ROM_TABLE_COUNT) {
        return false;
    }

    volatile uint32_t *directory = active_page_directory();
    for (uint32_t table = 0; table < table_count; ++table) {
        uint32_t table_paddr = alloc_page();
        if (table_paddr == 0) {
            return false;
        }
        uint32_t *entries_va = paddr_to_kernel_vaddr(table_paddr);
        uint32_t remaining = page_count - table * 1024U;
        uint32_t entries = remaining < 1024U ? remaining : 1024U;
        for (uint32_t index = 0; index < entries; ++index) {
            uint32_t page = table * 1024U + index;
            entries_va[index] =
                (boot_info->system_rom_base + page * MINEMU_PAGE_SIZE) | MINEMU_PTE_VALID |
                MINEMU_PTE_READABLE;
        }
        uint32_t directory_index = (boot_info->system_rom_base >> 22) + table;
        directory[directory_index] = table_paddr | MINEMU_PDE_VALID;
    }
    minemu_mmu_tlbiall();
    return true;
}

static const struct minemu_module_record *find_module(const struct minemu_boot_info *boot_info,
                                                       const char *name) {
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    size_t name_length = string_length(name);
    if (!image_records_valid(boot_info->module_table_offset, boot_info->module_count,
                             boot_info->image_size)) {
        return NULL;
    }

    const struct minemu_module_record *modules =
        (const struct minemu_module_record *)(image + boot_info->module_table_offset);
    for (uint32_t index = 0; index < boot_info->module_count; ++index) {
        const struct minemu_module_record *module = &modules[index];
        if (module->name_length == name_length &&
            image_range_valid(module->name_offset, module->name_length, boot_info->image_size) &&
            bytes_equal(image + module->name_offset, name, name_length)) {
            return module;
        }
    }
    return NULL;
}

static uint32_t pte_flags_from_segment(uint32_t segment_flags) {
    uint32_t pte_flags = 0;
    if ((segment_flags & MINEMU_SEGMENT_READABLE) != 0) {
        pte_flags |= MINEMU_PTE_READABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_WRITABLE) != 0) {
        pte_flags |= MINEMU_PTE_WRITABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_EXECUTABLE) != 0) {
        pte_flags |= MINEMU_PTE_EXECUTABLE;
    }
    return pte_flags;
}

/* Find the physical page backing a user VA page, optionally allocating it.
 * Two segments sharing a page must want the same permissions. */
static int user_page_index(uint32_t page_vaddr, uint32_t flags, bool create) {
    for (size_t index = 0; index < user_page_count; ++index) {
        if (user_page_vaddrs[index] == page_vaddr) {
            if (create && user_page_flags[index] != flags) {
                return -1;
            }
            return (int)index;
        }
    }
    if (!create || user_page_count == MAX_USER_IMAGE_PAGES) {
        return -1;
    }
    uint32_t paddr = alloc_page();
    if (paddr == 0) {
        return -1;
    }
    size_t index = user_page_count++;
    user_page_vaddrs[index] = page_vaddr;
    user_page_paddrs[index] = paddr;
    user_page_flags[index] = flags;
    return (int)index;
}

static bool prepare_user_range(uint32_t vaddr, uint32_t length, uint32_t flags) {
    uint32_t end = vaddr + length;
    uint32_t page = vaddr & MINEMU_PTE_PAGE_MASK;
    while (page < end) {
        if (user_page_index(page, flags, true) < 0) {
            return false;
        }
        page += MINEMU_PAGE_SIZE;
    }
    return true;
}

/* Copy source into the user pages backing [vaddr, vaddr + length), or
 * zero them if source is NULL (used for BSS). */
static bool write_user_range(uint32_t vaddr, const uint8_t *source, uint32_t length) {
    while (length != 0) {
        uint32_t page_vaddr = vaddr & MINEMU_PTE_PAGE_MASK;
        int index = user_page_index(page_vaddr, 0, false);
        if (index < 0) {
            return false;
        }
        uint32_t offset = vaddr - page_vaddr;
        uint32_t chunk = MINEMU_PAGE_SIZE - offset;
        if (chunk > length) {
            chunk = length;
        }
        uint8_t *page = paddr_to_kernel_vaddr(user_page_paddrs[index]);
        if (source == NULL) {
            zero_bytes(&page[offset], chunk);
        } else {
            copy_bytes(&page[offset], source, chunk);
            source += chunk;
        }
        vaddr += chunk;
        length -= chunk;
    }
    return true;
}

static bool load_user_module(const struct minemu_boot_info *boot_info,
                             const struct minemu_module_record *module) {
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    if (module->segment_count == 0 ||
        !image_records_valid(module->segment_table_offset, module->segment_count,
                             boot_info->image_size) ||
        (module->entry_vaddr & 3U) != 0) {
        return false;
    }

    const struct minemu_module_segment *segments =
        (const struct minemu_module_segment *)(image + module->segment_table_offset);
    bool entry_is_executable = false;
    user_page_count = 0;
    uint32_t table_paddr = alloc_page();
    uint32_t stack_paddr = alloc_page();
    if (table_paddr == 0 || stack_paddr == 0) {
        return false;
    }
    user_page_table = paddr_to_kernel_vaddr(table_paddr);

    for (uint32_t index = 0; index < module->segment_count; ++index) {
        const struct minemu_module_segment *segment = &segments[index];
        if (segment->memory_size == 0 || segment->file_size > segment->memory_size ||
            (segment->flags & ~(MINEMU_SEGMENT_READABLE | MINEMU_SEGMENT_WRITABLE |
                                MINEMU_SEGMENT_EXECUTABLE)) != 0 ||
            ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) != 0 &&
             (segment->flags & MINEMU_SEGMENT_READABLE) == 0) ||
            segment->virtual_address < USER_IMAGE_BASE ||
            segment->virtual_address >= USER_STACK_BOTTOM ||
            segment->memory_size > USER_STACK_BOTTOM - segment->virtual_address ||
            !image_range_valid(segment->data_offset, segment->file_size, boot_info->image_size)) {
            return false;
        }

        /* Allocate pages, zero all of memory_size (BSS), then copy the
         * initialized file_size bytes from the image. */
        uint32_t pte_flags = pte_flags_from_segment(segment->flags);
        if (!prepare_user_range(segment->virtual_address, segment->memory_size, pte_flags) ||
            !write_user_range(segment->virtual_address, NULL, segment->memory_size) ||
            !write_user_range(segment->virtual_address, image + segment->data_offset,
                              segment->file_size)) {
            return false;
        }

        if ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) != 0 && segment->file_size >= 4U &&
            module->entry_vaddr >= segment->virtual_address &&
            module->entry_vaddr - segment->virtual_address <= segment->file_size - 4U) {
            entry_is_executable = true;
        }
    }
    if (!entry_is_executable) {
        return false;
    }

    /* Build the user page table: program pages, then a writable,
     * non-executable stack page. Every entry has the User bit. */
    for (size_t index = 0; index < user_page_count; ++index) {
        uint32_t table_index = (user_page_vaddrs[index] >> 12) & 1023U;
        user_page_table[table_index] = user_page_paddrs[index] | MINEMU_PTE_VALID |
                                       MINEMU_PTE_USER | user_page_flags[index];
    }
    user_page_table[(USER_STACK_BOTTOM >> 12) & 1023U] =
        stack_paddr | MINEMU_PTE_VALID | MINEMU_PTE_USER | MINEMU_PTE_READABLE |
        MINEMU_PTE_WRITABLE;

    volatile uint32_t *directory = active_page_directory();
    directory[USER_IMAGE_BASE >> 22] = table_paddr | MINEMU_PDE_VALID;
    minemu_mmu_tlbiall();
    return true;
}

bool user_load(const struct minemu_boot_info *boot_info, const char *name, uint32_t *entry) {
    if (!map_system_rom(boot_info)) {
        return false;
    }
    const struct minemu_module_record *module = find_module(boot_info, name);
    if (module == NULL || !load_user_module(boot_info, module)) {
        return false;
    }
    *entry = module->entry_vaddr;
    return true;
}

bool user_range_ok(uint32_t vaddr, size_t length, uint32_t access) {
    if (length == 0) {
        return true;
    }

    /* The whole range has to be inside the user region. Checking it this
     * way also rejects kernel addresses, NULL, and vaddr + length
     * wrapping around. */
    if (vaddr < USER_IMAGE_BASE || vaddr >= USER_STACK_TOP ||
        length > USER_STACK_TOP - vaddr) {
        return false;
    }

    if (user_page_table == NULL) {
        return false;
    }

    /* Then check the page table entry for every page the range touches. */
    uint32_t required = MINEMU_PTE_VALID | MINEMU_PTE_USER | access;
    uint32_t end = vaddr + (uint32_t)length;
    for (uint32_t page = vaddr & MINEMU_PTE_PAGE_MASK; page < end; page += MINEMU_PAGE_SIZE) {
        uint32_t pte = user_page_table[(page >> 12) & 1023U];
        if ((pte & required) != required) {
            return false;
        }
    }
    return true;
}

bool copy_from_user(void *dst, uint32_t user_src, size_t length) {
    if (!user_range_ok(user_src, length, MINEMU_PTE_READABLE)) {
        return false;
    }
    /* The user page table is installed in the kernel's page directory, so
     * a checked user VA can be read directly. */
    copy_bytes(dst, (const void *)(uintptr_t)user_src, length);
    return true;
}

bool copy_to_user(uint32_t user_dst, const void *src, size_t length) {
    if (!user_range_ok(user_dst, length, MINEMU_PTE_WRITABLE)) {
        return false;
    }
    copy_bytes((void *)(uintptr_t)user_dst, src, length);
    return true;
}
