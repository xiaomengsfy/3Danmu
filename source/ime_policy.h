#ifndef IME_POLICY_H
#define IME_POLICY_H
#include <stdbool.h>
#include <stddef.h>
/* Return true only when there is no pending literal composition. */
bool ime_confirm(char *text, size_t cap, char *pending, int *pending_len);
void ime_lookup_key(const char *pending, size_t len, char *key, size_t cap);
#endif
