import pytest

# sysvrun show
def test_show(sysdenv):
    sysdsvc = sysdenv.create_service("foo")
    sysdsvc.write()
    output = sysdsvc.show(debug=True)
    assert output and output.exists()
    with output.open() as file:
        unit = file.read()
        assert unit and unit == sysdsvc.unit()


# sysvrun show: check effect of umask
@pytest.mark.parametrize("n", range(0, 8))
def test_show_umask(sysdenv, umask, n):
    sysdsvc = sysdenv.create_service("foo")
    sysdsvc.write()
    mask = n << 6 | n << 3 | n
    umask(mask)
    output = sysdsvc.show(debug=True)
    assert output and output.exists()
    mode = 0o644 & ~mask
    st_mode = output.stat().st_mode & 0o777
    assert (
        mode == st_mode
    ), f"expected {mode:04o} got {st_mode:04o} with umask {mask:04o}"
