#include <stddef.h>

#include "kprintf.h"
#include "msh.h"
#include "uart.h"

/* Longest line msh accepts, not counting the '\n'. */
#define MSH_LINE_MAX 20

#define ASCII_BACKSPACE 0x08
#define ASCII_DELETE 0x7f

/* Return 1 if the len bytes at word are exactly the string name. */
static int word_is(const char *word, size_t len, const char *name) {
    size_t i = 0;
    while (i < len && name[i] != '\0' && word[i] == name[i]) {
        i++;
    }
    return i == len && name[i] == '\0';
}

static void msh_execute(const char *line) {
    /* Ignore spaces before the command. */
    while (*line == ' ') {
        line++;
    }

    /* Empty or all-space line: nothing to do. */
    if (*line == '\0') {
        return;
    }

    /* The command is the first space-delimited word. */
    const char *end = line;
    while (*end != '\0' && *end != ' ') {
        end++;
    }
    size_t cmd_len = (size_t)(end - line);

    if (word_is(line, cmd_len, "echo")) {
        /* Repeated spaces after "echo" count as one separator. */
        const char *text = end;
        while (*text == ' ') {
            text++;
        }
        kprintf("%s\n", text);
    } else {
        uart_puts("command not found: ");
        uart_write(line, cmd_len);
        uart_putc('\n');
    }
}

void msh_run(void) {
    char line[MSH_LINE_MAX + 1];

    /* Number of bytes typed on this line so far. It can go past
     * MSH_LINE_MAX; only the first MSH_LINE_MAX bytes are stored, and a
     * line that is still too long at '\n' is rejected. Counting past
     * the limit keeps backspace correct on an overlong line. */
    unsigned int len = 0;

    uart_puts("msh> ");

    for (;;) {
        int c = uart_getc();
        if (c < 0) {
            continue; /* nothing received yet */
        }

        if (c == '\n') {
            if (len > MSH_LINE_MAX) {
                kprintf("msh: line too long (max %d bytes)\n", MSH_LINE_MAX);
            } else {
                line[len] = '\0';
                msh_execute(line);
            }
            len = 0;
            uart_puts("msh> ");
        } else if (c == ASCII_BACKSPACE || c == ASCII_DELETE) {
            /* Backspace on an empty line is ignored. */
            if (len > 0) {
                len--;
            }
        } else {
            if (len < MSH_LINE_MAX) {
                line[len] = (char)c;
            }
            len++;
        }
    }
}
