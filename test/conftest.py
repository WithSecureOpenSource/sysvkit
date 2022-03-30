import os
import pytest
import sys
from sysvenv import SysVEnv
from sysdenv import SysdEnv


@pytest.fixture(scope="session")
def arch():
    arch = os.environ.get("FSARCH")
    if arch:
        return arch
    uname = os.uname()
    if uname.sysname == "Linux" and uname.machine == "x86_64":
        return "linux64"
    print("Unsupported architecture", file=sys.stderr)
    assert False


@pytest.fixture
def sysvenv(arch, tmp_path):
    sve = SysVEnv(arch, tmp_path)
    sve.arrange()
    return sve


@pytest.fixture
def sysdenv(arch, tmp_path):
    sde = SysdEnv(arch, tmp_path)
    sde.arrange()
    return sde


@pytest.fixture
def root():
    if os.geteuid() > 0:
        pytest.skip("requires root privileges")


@pytest.fixture
def umask():
    orig_umask = None

    def set(mask):
        nonlocal orig_umask
        prev_umask = os.umask(mask)
        if orig_umask is None:
            orig_umask = prev_umask
        return prev_umask

    yield set
    if orig_umask is not None:
        os.umask(orig_umask)
