# systemctl enable: successful.
def test_systemctl_enable(sysvenv):
    service = sysvenv.create_service("foo")
    out, err, status = service.invoke("enable")
    assert status == 0
    assert service.is_enabled()
    assert not service.did("start")


# systemctl is-enabled: the service is not enabled.
def test_systemctl_is_enabled_no(sysvenv):
    service = sysvenv.create_service("foo")
    out, err, status = service.invoke("is-enabled")
    assert out and "disabled" in out.decode("utf-8")
    assert status != 0


# systemctl is-enabled: the service is enabled.
def test_systemctl_is_enabled_yes(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    out, err, status = service.invoke("is-enabled")
    assert out
    out = out.decode("utf-8")
    assert out and "enabled" in out
    assert status == 0


# systemctl is-enabled: multiple services, none of which are enabled.
def test_systemctl_is_enabled_multi_none(sysvenv):
    svc0 = sysvenv.create_service("foo")
    svc1 = sysvenv.create_service("bar")
    svc2 = sysvenv.create_service("baz")
    out, err, status = sysvenv.systemctl("is-enabled", svc0, svc1, svc2)
    assert status != 0


# systemctl is-enabled: multiple services, one of which is enabled.
def test_systemctl_is_enabled_multi_one(sysvenv):
    svc0 = sysvenv.create_service("foo")
    svc1 = sysvenv.create_service("bar")
    svc1.direct_enable()
    svc2 = sysvenv.create_service("baz")
    out, err, status = sysvenv.systemctl("is-enabled", svc0, svc1, svc2)
    assert out
    out = out.decode("utf-8")
    lines = out.splitlines()
    assert "disabled" in lines[0]
    assert "enabled" in lines[1]
    assert "disabled" in lines[2]
    assert status == 0


# systemctl is-enabled: multiple services, all of which are enabled.
def test_systemctl_is_enabled_multi_all(sysvenv):
    svc0 = sysvenv.create_service("foo")
    svc0.direct_enable()
    svc1 = sysvenv.create_service("bar")
    svc1.direct_enable()
    svc2 = sysvenv.create_service("baz")
    svc2.direct_enable()
    out, err, status = sysvenv.systemctl("is-enabled", svc0, svc1, svc2)
    assert status == 0


# systemctl disable: the service was not enabled.
def test_systemctl_disable(sysvenv):
    service = sysvenv.create_service("foo")
    out, err, status = service.invoke("disable")
    assert status == 0
    assert not service.is_enabled()
    assert not service.did("stop")


# systemctl disable: the service was enabled.
def test_systemctl_disable(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    out, err, status = service.invoke("disable")
    assert status == 0
    assert not service.is_enabled()
    assert not service.did("stop")
