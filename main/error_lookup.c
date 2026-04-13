#include "error_lookup.h"
#include "esp_log.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "error_lookup";

/* Embedded error lookup TSV */
extern const uint8_t error_tsv_start[] asm("_binary_error_lookup_tsv_start");
extern const uint8_t error_tsv_end[]   asm("_binary_error_lookup_tsv_end");

bool error_lookup(int error_code, char *buf, size_t buf_size)
{
    if (error_code == 0 || buf == NULL || buf_size == 0) {
        if (buf && buf_size > 0) buf[0] = '\0';
        return false;
    }

    /* Format the error code as uppercase hex for matching */
    char hex_code[16];
    snprintf(hex_code, sizeof(hex_code), "%08X", (unsigned int)error_code);

    /* Search through the embedded TSV.
     * Format: domain<TAB>code<TAB>models<TAB>message
     * We look for lines starting with "E\t" followed by our hex code. */
    const char *data = (const char *)error_tsv_start;
    const char *end = (const char *)error_tsv_end;
    const char *line = data;

    while (line < end) {
        /* Find end of this line */
        const char *eol = memchr(line, '\n', end - line);
        if (!eol) eol = end;

        /* Skip comments */
        if (line[0] == '#' || line >= eol) {
            line = eol + 1;
            continue;
        }

        /* Check if line starts with "E\t" */
        if (eol - line > 2 && line[0] == 'E' && line[1] == '\t') {
            /* Extract code field (after first tab) */
            const char *code_start = line + 2;
            const char *code_end = memchr(code_start, '\t', eol - code_start);
            if (code_end) {
                size_t code_len = code_end - code_start;
                /* Compare hex codes (case-insensitive) */
                if (code_len == strlen(hex_code) &&
                    strncasecmp(code_start, hex_code, code_len) == 0) {
                    /* Skip the models field (third column) */
                    const char *models_end = memchr(code_end + 1, '\t', eol - (code_end + 1));
                    if (models_end) {
                        /* Message is after the third tab */
                        const char *msg = models_end + 1;
                        size_t msg_len = eol - msg;
                        /* Trim trailing whitespace */
                        while (msg_len > 0 && (msg[msg_len-1] == '\r' || msg[msg_len-1] == '\n' || msg[msg_len-1] == ' '))
                            msg_len--;
                        if (msg_len > 0) {
                            size_t copy_len = msg_len < buf_size - 1 ? msg_len : buf_size - 1;
                            memcpy(buf, msg, copy_len);
                            buf[copy_len] = '\0';
                            return true;
                        }
                    }
                }
            }
        }

        line = eol + 1;
    }

    /* Fallback: show hex error code */
    snprintf(buf, buf_size, "Error 0x%08X", (unsigned int)error_code);
    ESP_LOGW(TAG, "Unknown error code: 0x%08X", (unsigned int)error_code);

    return false;
}  /* end error_lookup */
