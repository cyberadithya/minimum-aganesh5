#include <stddef.h>
#include <stdint.h>

#include "minemu/user_abi.h"
#include "msh.h"

/*
 * The minimum shell, moved from the kernel (HW1) into a user program.
 * User mode can't touch the UART registers, so all console input and
 * output goes through the ioctl syscall.
 */

/* Longest line msh accepts, not counting the '\n'. A longer line is
 * thrown away without running anything. */
#define MSH_LINE_MAX 128

/* How many bytes to ask the kernel for in one read. */
#define READ_CHUNK 32

#define ASCII_BACKSPACE 0x08
#define ASCII_DELETE 0x7f

static size_t str_len(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static void write_bytes(const char *buf, size_t len) {
    if (len > 0) {
        (void)ioctl(MINEMU_FD_UART_OUT, MINEMU_IOCTL_UART_WRITE, (void *)buf, (int)len);
    }
}

static void write_str(const char *s) {
    write_bytes(s, str_len(s));
}

/* Return 1 if the len bytes at word are exactly the string name. */
static int word_is(const char *word, size_t len, const char *name) {
    size_t i = 0;
    while (i < len && name[i] != '\0' && word[i] == name[i]) {
        i++;
    }
    return i == len && name[i] == '\0';
}

/* Print the words in text separated by single spaces, then a newline.
 * Extra spaces between words and at the end are dropped. */
static void msh_echo(const char *text) {
    char out[MSH_LINE_MAX + 1];
    size_t out_len = 0;

    while (*text != '\0') {
        while (*text == ' ') {
            text++;
        }
        if (*text == '\0') {
            break;
        }
        if (out_len > 0) {
            out[out_len++] = ' ';
        }
        while (*text != '\0' && *text != ' ') {
            out[out_len++] = *text;
            text++;
        }
    }
    out[out_len++] = '\n';
    write_bytes(out, out_len);
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
        msh_echo(end);
    } else {
        write_str("command not found: ");
        write_bytes(line, cmd_len);
        write_bytes("\n", 1);
    }
}

void msh_run(void) {
    char line[MSH_LINE_MAX + 1];
    char input[READ_CHUNK];

    /* Number of bytes typed on this line so far. It can go past
     * MSH_LINE_MAX; only the first MSH_LINE_MAX bytes are stored, and a
     * line that is still too long at '\n' is dropped. Counting past
     * the limit keeps backspace correct on an overlong line. */
    unsigned int len = 0;

    write_str("msh> ");

    for (;;) {
        /* Ask the kernel for whatever has arrived. 0 means nothing yet. */
        int32_t got = ioctl(MINEMU_FD_UART_IN, MINEMU_IOCTL_UART_READ, (void *)input,
                            (int)READ_CHUNK);
        if (got <= 0) {
            continue;
        }

        for (int32_t i = 0; i < got; i++) {
            char c = input[i];

            if (c == '\n') {
                if (len <= MSH_LINE_MAX) {
                    line[len] = '\0';
                    msh_execute(line);
                }
                len = 0;
                write_str("msh> ");
            } else if (c == ASCII_BACKSPACE || c == ASCII_DELETE) {
                /* Backspace on an empty line is ignored. */
                if (len > 0) {
                    len--;
                }
            } else {
                if (len < MSH_LINE_MAX) {
                    line[len] = c;
                }
                len++;
            }
        }
    }
}
