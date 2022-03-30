# TODO

## common

* Add unit tests for the `text` API, possibly also for `pair` and `environment`.

### procwatch

* Consider never disabling the process event stream, to avoid the possibility of accidentally decrementing the reference counter to a negative value, which would DoS future instances.  The cost of generating evens that no-one is listening for is minimal.

## `systemctl`

* For `is-enabled`, consider returning true if and only if the service is enabled at the current runlevel, as determined by executing `runlevel(8)`, and fall back to the current behavior if the current runlevel could not be determined.
* Consider using stop + start for the restart and try-restart commands.
* Unless `--quiet` was specified, `enable` and `disable` should print information about what they do.
* In `service_manip_rl()`, avoid deleting a symlink and recreating the same symlink.
* Allow finer-grain control over runlevels in `service_manip()`.
* Implement the `reenable` command.
* Consider using `fork_function()` in `service_invoke()`.
* Verify that sequencing works properly on sysvinit systems.  If not, `enable` / `disable` should probably exec `chkconfig` instead of creating symlinks themselves, since `chkconfig` will create numbered symlinks which will DTRT when run in lexical order.
* Start and kill priorities are hardcoded at 20 and 80 respectively.  Normally chkconfig would use priority 50 (or more in case of dependencies).  We should consider switching to 50, or at least making the priority configurable.

## `sysvrun`

* Add module tests.
* Review [exit codes](https://refspecs.linuxbase.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/iniscrptact.html) for the start, stop, restart, reload service commands.
* Move PID file and possibly credentials from `command` to `service`.
* Finish decoupling `command` from `service` and move `command.c` into `libcommon`.
* Consider moving as much of `systemd.c` and `sysvinit.c` as possible into `libcommon`.
* Consider implementing `CapabilityBoundingSet`.
* `command_resolve_path()` does not fully take `--root` into account.  This may be more trouble than it's worth; perhaps we should reduce the scope of `--root` so that it only applies to the service search path (which is where we really need it, for module tests).
* Consider reading `/etc/locale.conf` if present and setting the correct environment variables accordingly.
* Consider removing chroot support as it renders path resolution extremely complicated.
* Consider implementing `$INVOCATION_ID`.
* Consider implementing specifiers, so we can e.g. set `PIDFile=/var/run/%N.pid`.
* Consider making the PATH prepend logic conditional on a command-line option.
* Add signal handlers to the monitor so we clean up if killed (stop the service, shut down cn_proc).
* We should probably not report readiness if the main process of a service returns a non-zero exit code.  Instead, we should either report failure, or move directly to the restart logic, and not report readiness until we either succeed or give up.
* Look into `GuessMainPID`.
* Move `fd_to_log()` into `libcommon`, integrate it with `fork_io`, and implement line buffering.
* Move the monitor client API into a separate file, and possibly rename it, to avoid confusion with the monitor itself.
