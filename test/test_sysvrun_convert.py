import pytest

# sysvrun convert
def test_convert(sysdenv):
    sysdsvc = sysdenv.create_service("foo")
    sysvsvc = sysdsvc.convert(debug=True)
    assert sysvsvc.script.exists()
    embed = ":<<SYSVKIT\n" + sysdsvc.unit() + "SYSVKIT\n"
    with sysvsvc.script.open() as file:
        script = file.read()
        assert script and embed in script


# sysvrun convert: check effect of umask
@pytest.mark.parametrize("n", range(0, 8))
def test_convert_umask(sysdenv, umask, n):
    sysdsvc = sysdenv.create_service("foo")
    mask = n << 6 | n << 3 | n
    umask(mask)
    sysvsvc = sysdsvc.convert(debug=True)
    assert sysvsvc.script.exists()
    mode = 0o755 & ~mask
    st_mode = sysvsvc.script.stat().st_mode & 0o777
    assert (
        mode == st_mode
    ), f"expected {mode:04o} got {st_mode:04o} with umask {mask:04o}"
