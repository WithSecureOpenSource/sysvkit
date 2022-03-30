# Systemd compatibility for SysV systems

## Overview

This set of tools allow Systemd services to run unmodified on SysV-style systems.  It consists of two programs:

* [`systemctl`](src/systemctl/systemctl.md) provides a subset of the functionality of Systemd's `systemctl`; specifically, most of the service-related commands.
* [`sysvrun`](src/sysvrun/sysvrun.md) launches and monitors a Systemd service, providing restart functionality which a SysV-style init is not capable of.

In addition, a mock daemon, [`mockd`](src/mockd/mockd.md), is provided for test purposes.

**NOTE: this software is not compatible with Linux kernels older than 2.6.27.**


## Building

sysvkit uses [SCons][] and `pkg-config` for building.

Before building sysvkit for the first time, run
```
git submodule update --init
```

To build sysvkit, run
```
scons [ prefix=<prefix> ]
```
from the top-level sysvkit directory. The optional prefix argument is a
directory, `/usr/local` by default, where the build system installs
sysvkit.

To install sysvkit, run
```
sudo scons [ prefix=<prefix> ] install
```

## Model

### Services

For the purposes of these tools, a service is represented by a script in `/etc/init.d`.  The service name is the name of the script, not the `Provides` line in the LSB comment block.  For interoperability, any `.service` suffix will be stripped from the name passed on the command line before looking for the script, so

    systemctl enable my.service

and

    systemctl enable my

are equivalent, provided `/etc/init.d/my` exists.

A service is considered to be enabled if links to it exist in at least one of the `rc` directories for runlevels 2 through 5.  Enabling a service creates links for all four runlevels, and disabling a service deletes them.

The state of a service is determined by invoking its init script's `status` command and interpreting its exit code as described in the Linux Standard Base Core Specification, cf. [LSB 5.0.0 section 22.2](https://refspecs.linuxbase.org/LSB_5.0.0/LSB-Core-generic/LSB-Core-generic/iniscrptact.html).

### Launcher

The launcher, `sysvrun`, is intended to be invoked from an init script, but can just as easily be run from the command line.  It reads a Systemd unit file describing a service and performs the requested command (`start`, `stop`, `reload`, `status`).  If the service does not daemonize itself, `sysvrun` will do so; and if it requires monitoring, `sysvrun` will monitor and restart it if necessary.
