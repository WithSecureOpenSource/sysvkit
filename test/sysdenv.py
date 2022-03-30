from shlex import quote as Q
from sysvenv import SysVEnv, SysVService
import types

DOT_SERVICE = ".service"
EXEC_OPS = ["Start", "Stop", "Reload"]


def deservicify(name):
    if name.endswith(DOT_SERVICE):
        return name[: -len(DOT_SERVICE)]
    return name


def servicify(name):
    if not name.endswith(DOT_SERVICE):
        return name + DOT_SERVICE
    return name


class Environment(types.SimpleNamespace):
    def __init__(self):
        pass

    def __repr__(self):
        return " ".join(
            "=".join(key, Q(value)) for key, value in self.__dict__.items()
        )

    def set(self, key, value):
        assert type(key) == str and key.isidentifier()
        self.__dict__[key] = str(value)

    def get(self, key, default=None):
        assert type(key) == str and key.isidentifier()
        return self.__dict__.get(key, default)


class SysdService:  # XXX make this a subclass of SysVService
    def __init__(self, sysdenv, name):
        self.env = sysdenv
        self._name = deservicify(name)
        self._environment = Environment()
        self._exec = {}
        self._pidfile = None
        self.unit_file = self.env.unit_d / servicify(self._name)
        self.init_script = self.env.init_d / self._name

    def __repr__(self):
        return servicify(self.name)

    def write(self, path=None):
        if path is None:
            path = self.unit_file
        with path.open(mode="w") as unit_file:
            unit_file.write(self.unit())
        path.chmod(0o640)

    @property
    def name(self):
        return self._name

    @property
    def description(self):
        if "_description" in self.__dict__:
            return self._description
        return "{} service".format(self._name)

    @description.setter
    def description(self, description):
        self.unit["Description"] = str(description)

    @property
    def type(self):
        if "_type" in self.__dict__:
            return self._type
        return "simple"

    @type.setter
    def type(self, type):
        assert type(type) == str and type in ["simple", "exec", "forking"]
        self._type = type

    def exec(self, op, command, *args):
        assert op in EXEC_OPS
        assert (self.env.root / command).exists()
        self._exec[op] = [command, *args]

    @property
    def execstart(self):
        return self._exec.get("Start", None)

    @execstart.setter
    def execstart(self, cmdline):
        return self.exec("Start", *cmdline)

    def setenv(self, key, value):
        self.environment.set(key, value)

    def getenv(self, key):
        return self.environment.get(key)

    @property
    def pidfile(self):
        return self._pidfile

    @pidfile.setter
    def pidfile(self, value):
        if value is True:
            value = self._name + ".pid"
        if not "/" in str(value):
            value = self.env.run_d / value
        self._pidfile = value
        return self._pidfile

    def unit(self):
        lines = []
        lines.append("[Unit]")
        lines.append("Description=" + self.description)
        lines.append("[Service]")
        lines.append("Type=" + self.type)
        for op in EXEC_OPS:
            if op in self._exec:
                lines.append(
                    "Exec{}={}".format(
                        op, " ".join(Q(str(arg)) for arg in self._exec[op])
                    )
                )
        if self._pidfile:
            lines.append("PIDFile={}".format(str(self._pidfile)))
        lines.append("[Install]")
        lines.append("WantedBy=multi-user.target")  # XXX hardcode for now
        lines.append("")
        return "\n".join(lines)

    def convert(self, **kwargs):
        self.write()
        sysvsvc = SysVService(self.env, self._name)
        _, _, status = self.env.sysvrun(
            self.name, "convert", output=sysvsvc.script, **kwargs
        )
        if status != 0:
            return None
        return sysvsvc

    def show(self, **kwargs):
        self.write()
        output = self.env.tmp_path / f"show-{self.name}"
        _, _, status = self.env.sysvrun(
            self.name, "show", output=output, **kwargs
        )
        if status != 0:
            return None
        return output

    def direct_invoke(self, command, **kwargs):
        assert False

    def invoke(self, command, **kwargs):
        self.write()
        args = []
        args.extend(["--unit-file", self.unit_file])
        args.extend([deservicify(self.name), command])
        return self.env.sysvrun(*args, **kwargs)


class SysdEnv(SysVEnv):
    def __init__(self, arch, tmp_path):
        SysVEnv.__init__(self, arch, tmp_path)
        self.unit_d = self.root / "usr" / "lib" / "systemd" / "system"
        self.sysvrun_bin = self.sbin_path / "sysvrun"
        self.mockd_bin = self.sbin_path / "mockd"

    def arrange(self):
        SysVEnv.arrange(self)
        self.unit_d.mkdir(0o750, parents=True, exist_ok=True)

    # Creates a dummy service within the test environment using mockd.
    def create_service(self, name):
        return SysdService(self, name)

    @property
    def mockd(self):
        return self.mockd_bin

    # Invokes sysvrun within the test environment.  Returns a thruple of stdout,
    # stderr, exit status.
    def sysvrun(self, *args, **kwargs):
        cmdline = [self.sysvrun_bin]
        if args:
            cmdline.extend(args)
        return self.execute(cmdline, **kwargs)
