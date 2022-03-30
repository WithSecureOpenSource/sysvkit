# systemctl status: service is enabled but not running.
def test_systemctl_status_enabled_stopped(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 3)
    out, err, status = service.invoke("status")
    assert out
    out = out.decode("utf-8")
    assert "enabled" in out and "inactive" in out
    assert status == 3
    assert service.did("status")


# systemctl status --quiet: service is enabled but not running.
def test_systemctl_status_enabled_stopped_quiet(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 3)
    out, err, status = service.invoke("status", quiet=True)
    assert not out
    assert status == 3
    assert service.did("status")


# systemctl status: multiple services, one of which is running.
def test_systemctl_status_multi(sysvenv):
    svc0 = sysvenv.create_service("foo")
    svc0.direct_enable()
    svc0.will_do("status", 3)
    svc1 = sysvenv.create_service("bar")
    svc1.will_do("status", 0)
    svc2 = sysvenv.create_service("baz")
    svc2.will_do("status", 3)
    out, err, status = sysvenv.systemctl("status", svc0, svc1, svc2)
    assert out
    assert status != 0
    lines = out.decode("utf-8").splitlines()
    assert len(lines) == 3
    assert svc0.did("status")
    words = lines[0].split()
    assert words[0] == svc0.name and "enabled" in words and "inactive" in words
    assert svc1.did("status")
    words = lines[1].split()
    assert words[0] == svc1.name and "disabled" in words and "active" in words
    assert svc2.did("status")
    words = lines[2].split()
    assert words[0] == svc2.name and "disabled" in words and "inactive" in words
