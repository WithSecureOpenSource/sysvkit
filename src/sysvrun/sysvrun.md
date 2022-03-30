# `sysvrun`

The `sysvrun` program runs the service described in the Systemd unit file it is provided, daemonizing and monitoring as necessary to approximate the behavior of Systemd.

## Synopsis

    sysvrun --help
    sysvrun [options] service command

## Supported options

The following options are supported:

### `--debug`

The `--debug` option causes certain operations to produce additional output useful to developers.

### `--help`

The `--help` option causes `sysvrun` to print a brief usage message and immediately exit.

### `--output`

When invoked with `--output=`**`path`**, `sysvrun` will print its output to the specified file instead of standard output.
If the file already exists, it will be truncated.
In either case, it will be deleted in case of error.
This option is mostly useful for the `convert` and `show` commands.

### `--quiet`

The `--quiet` option suppresses informative output.

### `--root`

When invoked with `--root=`**`path`**, `sysvrun` will perform all operations relative to the specified path instead of the filesystem root.

### `--unit-file`

When invoked with `--unit-file=`**`path`**, `sysvrun` will read the specified file instead of searching for a unit file that matches the service name.

### `--verbose`

The `--verbose` option causes certain operations to produce additional output.

## Supported commands

The following commands are supported:

### `convert`

Generates a SysV-style init script for the specified service.
The script is printed to the standard output unless a path was specified using the `--output` option.
In the latter case, the resulting file will have mode 0755 modified by the current umask.

### `show`

Displays the original unit file for the specified service.
The unit file is printed to the standard output unless a path was specified using the `--output` option.
In the latter case, the resulting file will have mode 0644 modified by the current umask.

### `start`

TBW

### `stop`

TBW

### `reload`

TBW

### `status`

TBW

## Known Limitations

The `--root` option is poorly thought out and may not fully work as expected, particularly in conjunction with the `RootDirectory` service option.

The `stop` command does not take service settings such as `KillMode`, `TimeoutStopSec`, `TimeoutStopFailureMode` etc. into account.

TBW
