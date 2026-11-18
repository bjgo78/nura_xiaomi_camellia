/* Run a command without blocking the UI; the callback gets its exit status and output. */
#ifndef CAMELLIA_EXEC_H
#define CAMELLIA_EXEC_H

typedef void (*exec_cb)(int status, const char *out, void *user);

int exec_async(const char *const argv[], exec_cb cb, void *user);

#endif
