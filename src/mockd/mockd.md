# `mockd`

The `mockd` command is a mock daemon intended for use in sysvkit's module tests.

## Synopsis

    mockd [-dv] action[:parameter] [...]

## Description

The `mockd` daemon performs the actions specified on the command line in order.  Some actions take an optional parameter, which is appended to the action with a separating colon.

## Supported options

The following options are supported:

### `--debug`

The `--debug` option causes certain operations to produce additional output useful to developers.

### `--verbose`

The `--verbose` option causes certain operations to produce additional output.

## Supported actions

### `block`

The `block` command causes `mockd` to install a process mask that blocks the given signal.  The optional parameter is the numeric signal to block and must be between 1 and 15 inclusive.  The default is 15 (`SIGTERM`).

### `daemon`

The `daemon` command causes `mockd` to daemonize using the `fork()`-`setsid()`-`fork()` paradigm.  In the process, the standard input, output, and error are redirected to `/dev/null`; it is therefore recommended to precede a `daemon` command by a `syslog` command.

### `exit`

The `exit` command causes `mockd` to exit.  The optional parameter specifies the exit status and must be between 0 and 255 inclusive.  The default is 0.

### `pidfile`

The `pidfile` command causes `mockd` to write a PID file.  The optional parameter is the path to said file.  The default is `/var/run/mockd.pid`.

### `raise`

The `raise` command causes `mockd` to raise a signal.  The optional parameter is the numeric signal to raise and must be between 0 and 15 inclusive.  The default is 15 (`SIGTERM`).

### `sleep`

The `sleep` command causes `mockd` to sleep by performing a no-op `poll()` call.  The optional parameter is the length of time to sleep, in [systemd timespan syntax](https://www.freedesktop.org/software/systemd/man/systemd.time.html#id-1.6), or `forever` to sleep forever.

### `syslog`

The `syslog` command causes `mockd` to redirect its output to syslog.  The optional parameter is the identifier to use in the `openlog()` call.

## Examples

### Simple service

The following invocation simulates a typical `Type=simple` service that sleeps forever:

    mockd -v syslog pidfile sleep

### Forking service

The following invocation simulates a typical `Type=forking` service that takes two seconds to initialize, then dies of an assertion failure (represented by a `SIGABRT`) after ten seconds:

    mockd -v sleep:2 syslog daemon pidfile sleep:10 raise:6
