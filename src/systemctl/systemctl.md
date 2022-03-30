# `systemctl`

The `systemctl` command provides a subset of the functionality of Systemd's `systemctl`; specifically, most of the service-related commands.

## Synopsis

    systemctl --help
    systemctl [options] version
    systemctl [options] daemon-reload
    systemctl [options] <command> service [...]

## Supported options

The following options are supported:

### `--debug`

The `--debug` option causes certain operations to produce additional output useful to developers.

Note that this option is not supported by the real systemctl.

### `--help`

The `--help` option causes `systemctl` to print a brief usage message and immediately exit.

### `--root`

When invoked with `--root=`**`path`**, `systemctl` will perform all operations relative to the specified path instead of the filesystem root.

Note that this is not suitable for preparing a chroot environment or similar as all symlinks created by systemctl have absolute targets.

### `--quiet`

The `--quiet` option suppresses output from `is-active`, `is-enabled`, and `status`.

### `--verbose`

The `--verbose` option causes certain operations to produce additional output.  Most notably, any operation that invokes a service's init script will suppress the script's output _unless_ the `--verbose` option was specified.

Note that this option is not supported by the real systemctl.

### `--version`

The `--version` option causes `systemctl` to print a fictive version number in a format similar to the one used by the real systemctl and immediately exit.

## Supported commands

Every command that operates on a service will print an error message and return a non-zero exit code if the service does not exist, i.e. there is no file with that name in `/etc/init.d`.

Every command that operates on a service can operate on multiple services.  The output will be the same as if the command was executed for each service individually.  The exit code varies from one command to another and is not always what you expect.

The following commands are currently supported:

### `daemon-reload`

The `daemon-reload` command does nothing.

### `status`, `is-enabled`, `is-active`

The `status` command prints information about a service in human-readable form and returns an exit code which conforms vaguely to the Linux Standard Base conventions for init scripts.  It returns a non-zero exit code if it fails to invoke the init script or the init script returns a non-zero exit code, and zero otherwise.

When operating on multiple services, the `status` command returns a non-zero exit code if any of the individual operations fail, and zero otherwise.

The `is-enabled` command print a single word indicating whether a service is enabled.  It returns a non-zero exit code if a filesystem operation fails, if it fails to invoke the init script, if the init script fails, or if the service is disabled, and zero otherwise.

The `is-active` commands print a single word indicating whether a service is active (i.e. running).  It returns a non-zero exit code if a filesystem operation fails, if it fails to invoke the init script, if the init script fails, or if the service is stopped or dead.

When operating on multiple services, the `is-enabled` and `is-active` commands return a non-zero exit code if all of the individual operations fail, and zero otherwise.

### `enable`, `disable`

The `enable` command creates symlinks to the service's init script in the `rc` directories for runlevels 2 through 5.  It also deletes any existing symlinks, to avoid duplicates.

The `disable` command deletes any symlinks to the service's init script that it finds in the `rc` directories for runlevels 2 through 5.

Both commands return a non-zero exit code if a filesystem operation fails, and zero otherwise.  When operating on multiple services, they return a non-zero exit code if any of the individual operations fail, and zero otherwise.

### `start`, `stop`, `try-restart`, `restart`

The `start` command starts a service by invoking its init script if it is not already running, and does nothing if it is.

The `stop` command stops a service by invoking its init script if it is running, and does nothing if it is not.

The `restart` command restarts a service if it is running, or starts it if it is not.

The `try-restart` command restarts a service by invoking its init script if it is running, and does nothing if it is not.

The `start`, `stop`, and `try-restart` commands start by performing the equivalent of a `status` command, and return a non-zero exit code if they fail to invoke the init script or interpret the result.  If they determine that no further action is necessary, they immediately return an exit code of zero.  If not, they attempt to invoke the requisite command and return a non-zero exit code if they fail to invoke the init script or the init script returns a non-zero exit code, and zero otherwise.

The `restart` command returns a non-zero exit code if it fails to invoke the init script or the init script returns a non-zero exit code, and zero otherwise.

When operating on multiple services, all four commands return a non-zero exit code if any of the individual operations fail, and zero otherwise.

### `reload`, `reload-or-restart`, `reload-or-try-restart`, `try-reload-or-restart`

The `reload` command reloads a service by invoking its init script.

The `reload-or-restart` command reloads a service by invoking its init script, and if that fails, restarts it instead.  If the service is not running, it will be started.

The `reload-or-try-restart` command is a compatibility alias for `try-reload-or-restart`.

The `try-reload-or-restart` command reloads a service by invoking its init script if it is running, and does nothing if it is not.  If the service is running but the reload fails, it restarts the service instead.

The `reload-or-restart` returns a non-zero exit code if it fails to invoke the init script or its the final invocation returns a non-zero exit code, and zero otherwise.

The `try-reload-or-restart` command first performs the equivalent of a `status` command, and returns a non-zero exit code if it fails to invoke the init script or the init script returns a non-zero exit code.  Otherwise, it performs the equivalent of a `reload-or-restart` command, with the same results.

When operating on multiple services, all four commands return a non-zero exit code if any of the individual operations fail, and zero otherwise.

### `show`

The `show` command prints each listed service's service file to standard output.  It returns a non-zero exit code if any of the service files can't be found or contains a syntax error, and zero otherwise.

## Known limitations

For the purposes of enabling and disabling services, the definition of a link to the service's init script is a link whose target has the same device and inode numbers as the init script.  This can lead to surprising results if multiple services are symlinked to a single script that uses `$0` to differentiate between them.  Don't do that.

The reason for comparing targets rather than names is that other management tools such as `chkconfig` may create links with names that include additional information, such as a numerical prefix.

Commands that directly or indirectly query the service's current status (`is-active` or `try-reload-or-restart`, for instance) assume that the init script conforms to the Linux Standard Base Core Specification.  There is no shortage of real-world examples of scripts that don't.  At the time of writing, one such example is the Oracle VirtualBox guest additions service: its `status` command returns 0 regardless of whether the service is running.
