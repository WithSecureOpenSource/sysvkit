#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

// This is necessary because certain legacy C APIs use unqualified pointers in
// places where we want to pass in const pointers.  Examples include struct
// iovec (when writing) and execve(2)'s argv and envp.
#define DQ(ptr) (void *)(uintptr_t)(ptr)
