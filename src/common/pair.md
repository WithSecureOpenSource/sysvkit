# Pair — key-value pairs

## Headers

### `#include "pair.h"`

## Types

### `struct pair`

This struct represents a key-value pair.
It contains at least the following fields:

| Name | Content |
|-|-|
| `const char *key` | Pointer to the null-terminated key. |
| `const char *value` | Pointer to the null-terminated value. |

## Functions

### `struct pair *pair_create_len(const char *`**`key`**`, size_t ​`**`keylen`**`, const char *`**`value`**`, size_t ​`**`vlen`**`)`

Constructs and returns a `struct pair` with the specified key and value of the specified lengths.
It is assumed that neither the key nor the value contain null characters within the specified lengths.

### `struct pair *pair_create(const char *`**`key`**`, const char *`**`value`**`)`

Constructs and returns a `struct pair` with the specified null-terminated key and value.

### `struct pair *pair_clone(const struct pair *`**`other`**`)`

Constructs and returns a `struct pair` as an exact copy the one provided.

### `void pair_free(struct pair *`**`p`**`)`

Frees a pair created by `pair_create_len()` or `pair_create()`.
