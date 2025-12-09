#pragma once

/* Simple utility header for init utilities. Utilities should use the
 * `out_puts` / `out_putchar` provided by the init shell or call the
 * weak kernel-provided symbols directly when available.
 */

void util_ls(const char *path);
void util_cat(const char *path);
