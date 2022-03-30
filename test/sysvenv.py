import pathlib
import shutil
import subprocess
import sys

GOOD_RL = ["2", "3", "4", "5"]
BAD_RL = ["0", "1", "6"]
ALL_RL = sorted(GOOD_RL + BAD_RL)


class SysVService:
    def __init__(self, sysvenv, name):
        self.env = sysvenv
        self.name = name
        self.script = self.env.init_d / self.name

    def __repr__(self):
        return self.name

    def invoke(self, command, **kwargs):
        return self.env.systemctl(command, self.name, **kwargs)

    def direct_invoke(self, command, *args, **kwargs):
        cmdline = [self.script, command]
        if args:
            cmdline.extend(args)
        return self.env.execute(cmdline, **kwargs)

    # Enable this service by directly manipulating symlinks in the test
    # environment.
    def direct_enable(self):
        for rl in GOOD_RL:
            if self.env.rc_d[rl].exists():
                link = self.env.rc_d[rl] / ("S20" + self.name)
                link.symlink_to(self.script)
        for rl in BAD_RL:
            if self.env.rc_d[rl].exists():
                link = self.env.rc_d[rl] / ("K80" + self.name)
                link.symlink_to(self.script)

    # Disable this service by directly manipulating symlinks in the test
    # environment.  Assumes no wildcat links.
    def direct_disable(self):
        for rl in GOOD_RL:
            if self.env.rc_d[rl].exists():
                link = self.env.rc_d[rl] / ("S20" + self.name)
                link.unlink()
        for rl in BAD_RL:
            if self.env.rc_d[rl].exists():
                link = self.env.rc_d[rl] / ("K80" + self.name)
                link.unlink()

    # Enable this service by invoking our systemctl replacement.
    def enable(self):
        return self.env.systemctl("enable", self.name)

    # Disable this service by invoking our systemctl replacement.
    def disable(self):
        return self.env.systemctl("disable", self.name)

    # True if the service is enabled at all the “good” runlevels and disabled at
    # all the “bad” runlevels.
    def is_enabled(self):
        for rl in GOOD_RL:
            if self.env.rc_d[rl].exists():
                link = self.env.rc_d[rl] / ("S20" + self.name)
                if not link.exists() or not link.is_symlink():
                    sys.stderr.write(f"{link} does not exist\n")
                    return False
            # Requires Python >= 3.9
            # if not link.readlink() == self.script:
            #    return False
        for rl in BAD_RL:
            if self.env.rc_d[rl].exists():
                link = self.env.rc_d[rl] / ("K80" + self.name)
                if not link.exists():
                    sys.stderr.write(f"{link} does not exist\n")
                    return False
        return True

    # Creates a file which instructs the service script to return a specific
    # result in response to a specific command.
    def will_do(self, command, status=0):
        outcome_file = self.env.root / "test-init-{}-{}-outcome".format(
            self.name, command
        )
        with outcome_file.open("w") as file:
            file.write("{}\n".format(status))

    # True if the service script was invoked for the specified service with the
    # specified command.
    def did(self, command, expect_status=None):
        witness_file = self.env.root / "test-init-{}-{}-witness".format(
            self.name, command
        )
        if not witness_file.exists():
            print("{} does not exist".format(witness_file), file=sys.stderr)
            return False
        if expect_status != None:
            with witness_file.open("r") as file:
                line = file.readline().strip()
                status = int(line)
            if status != expect_status:
                return False
        return True


class SysVEnv:
    def __init__(self, arch, tmp_path):
        self.test_path = pathlib.Path(__file__).resolve().parent
        self.repo_path = self.test_path.parent
        self.stage_path = self.repo_path / "stage" / arch
        self.component_path = (
            self.stage_path / "build" / "components" / "sysvkit"
        )
        self.sbin_path = self.component_path / "sbin"
        self.systemctl_bin = self.sbin_path / "systemctl"
        self.tmp_path = tmp_path
        self.root = self.tmp_path / "root"
        self.etc = self.root / "etc"
        self.init_d = self.etc / "init.d"
        self.run_d = self.root / "var" / "run"
        self.rc_d = {}
        for rl in ALL_RL:
            self.rc_d[rl] = self.etc / "rc{}.d".format(rl)

    def __repr__(self):
        return str(self.root)

    def arrange(self):
        self.init_d.mkdir(0o750, parents=True, exist_ok=True)
        for rl in ALL_RL:
            self.rc_d[rl].mkdir(0o750, parents=True, exist_ok=True)
        self.run_d.mkdir(0o750, parents=True, exist_ok=True)

    def cleanup(self):
        shutil.rmtree(self.tmp_path)

    # Creates a dummy service within the test environment by copying our service
    # script to the desired name in the init script directory.  Note that we
    # cannot simply symlink the script, as our systemctl uses the device / inode
    # tuple to tell services apart.
    def create_service(self, name):
        service = SysVService(self, name)
        shutil.copy2(self.test_path / "service.sh", service.script)
        return service

    # Invokes our systemctl within the test environment.  Returns a thruple of
    # stdout, stderr, exit status.
    def systemctl(self, *args, **kwargs):
        cmdline = [self.systemctl_bin]
        if args:
            cmdline.extend(args)
        return self.execute(cmdline, **kwargs)

    # Executes a command within the test environment.  Returns a thruple of
    # stdout, stderr, exit status.
    def execute(
        self,
        argv,
        env=None,
        timeout=5,
        quiet=False,
        verbose=False,
        debug=False,
        output=None,
    ):
        extra = ["--root", self.root]
        if quiet:
            extra.append("--quiet")
        if verbose:
            extra.append("--verbose")
        if debug:
            extra.append("--debug")
        if output:
            extra.extend(["--output", output])
        argv = [str(arg) for arg in argv[:1] + extra + argv[1:]]
        if not env:
            env = {}
        if not "ROOT" in env:
            env["ROOT"] = self.root
        if not "PWD" in env:
            env["PWD"] = self.root
        if not "PATH" in env:
            env["PATH"] = "/usr/sbin:/usr/bin:/sbin:/bin"
        for key, value in env.items():
            print("{}={}".format(key, value), file=sys.stderr)
        print(" ".join(arg for arg in argv), file=sys.stderr)
        proc = subprocess.Popen(
            argv,
            cwd=env["PWD"],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            out, err = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            out, err = proc.communicate()
        sys.stderr.write(err.decode("utf-8"))
        return (out, err, proc.returncode)
