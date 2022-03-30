# `sysvrun` internals

## Process monitoring

When a service daemonizes, it is reparented to PID 1 — in other words, either `init` or `systemd`.  This means that `systemd` can monitor services simply by...  monitoring its children. But we're not PID 1, so we can't.  Instead, we use the process event connector to keep track of our descendents.  The mechanisms to do so are found in the `cn_proc` and `procwatch` subsystems.

### The mechanics

#### The Linux process model

Linux has a very idiosyncratic process model.  When kernel support for multithreading was added, instead of separating threads (schedulable entities) from processes (one or more threads which share the same address space, descriptor space, credentials, capabilities etc.), they redefined processes as threads and introduced the concept of thread groups, which correspond to what Unix traditionally calls processes.  Unfortunately, this creates a lot of friction between kernel and userland, including but not limited to terminological confusion.  Some kernel facilities never really caught up; the audit system, for instance, does not know the difference between a thread and a process.

To summarize:

- What we call threads, the kernel calls processes.
- What we call processes, the kernel calls thread groups.
- What the kernel calls a process id (PID) is actually the thread id (TID).
- What we call a process id (PID) is actually the thread group id (TGID), which is equal to the thread id (TID) of the first thread in the thread group.
- What we call the parent process id (PPID) is actually the thread group id of the thread that forked the new thread group.
- The session id (SID) is the TGID of the thread that created the session.

#### The process event connector

The process event connector is a kernel facility based on Netlink which provides a multicast stream of process events: initially `fork`, `exec`, `setuid`, `setgid`, `setsid`, and `exit`; later also `ptrace`, process name changes, and core dumps.  The ones we are interested in are `fork`, `setsid`, and `exit`.  Simply put, what we want to do is build a table of our descendants and keep track of their actions.

We use the process event connector by opening a Netlink socket and binding to `CN_PROC_IDX`.  With that done, we send a short message to start the flow of events.  Conversely, when we shut down, we must first notify the connector to stop sending events; in typical Linux fashion, there is no proper tracking of listeners, just a simple counter, so failing to unregister before closing the socket will leave the kernel in a state where it continues to generate events with noone to receive them.  One can also trivially cause the kernel to decrement the counter to a large negative number, thus preventing anyone from enabling process events unless they do so repeatedly until they start receiving events.

#### Process events

Once the event stream is enabled, we can read one event at a time from the netlink socket into a `struct proc_event` (while the Netlink “wire protocol” so to speak supports batching messages, the current implementation never does).  This struct is defined in `<linux/cn_proc.h>`, but we use our own definition, for two reasons: first, because the official definition has very long and inconsistent field names (e.g. `ev->event_data.id.r.ruid`, which we call `ev->id.r.uid`), and second, to fix the set of events that we recognize and know how to decode independently of the headers avaiable on the build platform.

Process event messages contain an event type code, the 32-bit numeric identifier of the CPU core on which the event occurred, a 64-bit timestamp giving the time the event occurred in nanoseconds since boot, and additional data about the event.  The contents and layout of the latter depend on the event type, but it always starts with a process identifier (32-bit TID + 32-bit TGID).  For `fork` events, this identifier refers to the parent process (more on that below), and is followed by the child identifier.  For most other events, it refers to the thread that performed the action described by the message.

The parent field in process event messages always references the parent of the the thread that performed the action that triggered the event.  Thus, if a thread in thread group A forks thread group B, then thread group B starts a new thread C that forks thread group D which immediately execs a new binary, the latter two of the four fork events will reference A as the parent while the exec event will reference B.

Note that `exit` events contain both a `code` field and a `signal` field, which may cause some confusion.  The `code` field has the same semantics as the status value returned by the `wait` family of syscalls and is the only one of interest.  The `signal` field does not indicate that the thread was terminated by a signal; instead, it is the signal raised in the parent as a consequence of the termination, and should always be either `SIGCHLD` if the entire thread group exited or -1 otherwise.

#### Forking and daemonizing

Since we have a frequent need for forking and / or daemonizing various operations, we introduce the `fork` subsystem with the following features:

- A primitive for executing a caller-provided function in a child process.
- A primitive for executing a caller-provided function in a daemon process using the double-fork (or `fork()`-`setsid()`-`fork()`) idiom, which also takes care of closing file descriptors (with the exception of the standard input / output / error descriptors, which are redirected to `/dev/null`) and changing the working directory to the filesystem root.
- In both cases, a mechanism for passing a status report back to the parent process, in order to distinguish a failure to prepare the child from a failure _of_ the child, using a pipe between the original process and the child or daemon.

During the initial fork, all descriptors except the standard input / output / error and the write end of the reporting pipe are closed, and the pipe is relocated to a known descriptor (`REPORT_FILENO` which is defined as 3).  The child or daemon starts by writing its own PID to the pipe, then performs whatever initialization it needs to before calling the caller-provided function.  This function then has the choice between:

- Closing the pipe, either explicitly or by calling `execve()`, as the pipe is marked close-on-exec.
- Returning zero, upon which the child or daemon exits with status code zero.
- Returning a non-zero value, which is then written to the pipe before the child or daemon exits with a non-zero status code equal to the return value if it is a positive number less than or equal to 255 and `EXIT_FAILURE` otherwise.

The parent process, on its end, will first read the PID from the pipe, then attempt to read an error code.  It reports success if it received a PID and the pipe was closed without any further transmission.

A function is provided to close the pipe atomically and idempotently, by replacing it with a close-on-exec duplicate of `stderr`.

In addition, the caller can pass a set of descriptors which will replace the child's stdin / stdout / stderr instead of redirecting them to `/dev/null`.

#### Executing a command

TBW - explain `command_exec_func()`

### The lifetime of a service

The most common case is also the most complex: start a service that daemonizes itself and return control when its main process exits (`Type=forking`), then watch it and restart it if it terminates abnormally (`Restart=on-failure`).  Finally, on normal termination, we stop monitoring and terminate.

Variations on this include:

- The process does not daemonize.
- The service is considered ready as soon as the process has forked (`Type=simple`) or execed (`Type=exec`).
- The service does not need to be restarted (`Restart=no`)

#### Starting a service

To start a service, we daemonize a function that first enables process watching, then forks a child that executes the appropriate command.  It then loops, ingesting process events, until all descendants have terminated.

The daemon reports readiness at one of two points: either after the fork-exec (for `Type=simple` or `Type=exec`) or after the immediate child terminates (for `Type=forking`).  Note that, strictly speaking, waiting until after the exec is incorrect for `Type=simple`, but this is a distinction without a difference.

Once the last descendant has been collected, its outcome is used as a proxy for the outcome of the service as a whole.  This appears to be what systemd does, and makes as much sense as any other solution, even if it can be defeated by contrived scenarios (such as a shell script that ends in `kill -9 $$`).

With the outcome now known, we check the restart policy to decide what to do next, in accordance with table 2 in the [systemd.service documentation](https://www.freedesktop.org/software/systemd/man/systemd.service.html#id-1.8.3.20.2.3) (with the caveat that we do not support the `timeout` or `watchdog` restart policies, or the `SuccessExitStatus`, `RestartPreventExitStatus`, or `RestartForceExitStatus` options).

If we decide to restart, we first sleep for the amount of time specified by the `RestartSec` option.  If not, we disable process watching and terminate.

#### Stopping a service

Currently, we only support stopping services that use a PID file.  We simply read the PID file and attempt to send a `SIGTERM` to the process it references.  Under normal circumstances, this will cause the process to terminate and the daemon will not restart it.

Unfortunately, this means that if we try to stop a service that has already exited abnormally before the daemon has gotten around to restarting it, we will fail (because the process listed in the PID file no longer exist) but the daemon won't know that we tried and will restart the service.

One possible solution would be to give the daemon its own PID file and have the stop command signal the daemon instead of the service.  Another would be for the daemon to create a Unix socket which can be used to query and control it.  This could also be used to implement support for `Type=notify`.
