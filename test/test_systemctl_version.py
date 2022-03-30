# systemctl version
def test_systemctl_version(sysvenv):
    out, err, status = sysvenv.systemctl("--version")
    assert status == 0
    lines = out.decode("utf-8").splitlines()
    assert len(lines) == 1
    words = lines[0].split()
    assert len(words) >= 2
    assert words[0] == "systemctl"
    assert words[1].isnumeric()
