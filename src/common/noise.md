# Noise — informational, warning, and error messages

## Headers

### `#include "noise.h"`

## Constants

### `enum { SILENT, QUIET, NORMAL, VERBOSE, DEBUG }`

These constants represent the different levels of noise emitted by programs using the functions provided herein.
The numerical values of these constants are such that `SILENT < QUIET`, `QUIET < NORMAL`, `NORMAL < VERBOSE`, and `VERBOSE < DEBUG`.

The levels are as follows:

| level | effect |
|-|-|
| `SILENT` | Prints only error messages. |
| `QUIET` | Also prints warning messages. |
| `NORMAL` | Also prints informational messages. |
| `VERBOSE` | Also prints verbose messages. |
| `DEBUG` | Also prints debugging messages. |

## Variables

### `int noisy`

The `noisy` variable sets the noise level to one of the constants mentioned above: `SILENT`, `QUIET`, `NORMAL` (default), `VERBOSE`, or `DEBUG`.
Values below `SILENT` or above `DEBUG` as treated as equal to `SILENT` or `DEBUG`, respectively, but this may change if additional levels are added.

## Functions

The functions below preserve `errno` and are safe to use in error cleanup.
They are not, however, signal-safe.

Note that some or all of these functions may actually be function-like macros wrapping differently-named functions.
Programmers are advised to avoid passing arguments with side effects.

### `int debug(const char *fmt, ...)`

If `noisy` is `DEBUG` or higher, prints the specified message to `stderr` as if with `fprintf()`, preceded by `#` and followed by a newline character, and returns the total number of characters printed.
Otherwise, does nothing and returns 0.

### `int verbose(const char *fmt, ...)`

If `noisy` is `VERBOSE` or higher, prints the specified message to `stderr` as if with `fprintf()`, followed by a newline character, and returns the total number of characters printed.
Otherwise, does nothing and returns 0.

### `int info(const char *fmt, ...)`

If `noisy` is `NORMAL` or higher, prints the specified message to `stderr` as if with `fprintf()`, followed by a newline character, and returns the total number of characters printed.
Otherwise, does nothing and returns 0.

### `int warning(const char *fmt, ...)`

If `noisy` is `QUIET` or higher, prints the specified message to `stderr` as if with `fprintf()`, preceded by “`WARNING:`” and followed by a newline character, and returns the total number of characters printed.

### `int error(const char *fmt, ...)`

Prints the specified message to `stderr` as if with `fprintf()`, preceded by “`ERROR:`” and followed by a newline character, and returns the total number of characters printed.

### `int fatal(const char *fmt, ...)`

Prints the specified message to `stderr` as if with `fprintf()`, preceded by “`ERROR:`” and followed by a newline character, then exits with the exit code `EXIT_FAILURE`.
Does not return.

### `int fatalx(int code, const char *fmt, ...)`

Prints the specified message to `stderr` as if with `fprintf()`, preceded by “`ERROR:`” and followed by a newline character, then exits with the specified exit code.
Does not return.
