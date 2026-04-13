#pragma once

#include <stddef.h>
#include <stdbool.h>

/*
 * Look up a Bambu Lab print error code and return a human-readable message.
 *
 * error_code: The numeric error code from the printer's print_error field.
 *             This is typically a hex value like 0x03004000.
 * buf:        Output buffer for the error message.
 * buf_size:   Size of the output buffer.
 *
 * Returns true if a matching error was found, false if not (buf will contain
 * a fallback string like "Error 0x03004000").
 */
bool error_lookup(int error_code, char *buf, size_t buf_size);
