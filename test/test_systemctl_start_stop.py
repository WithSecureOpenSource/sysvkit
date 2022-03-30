# systemctl start: successful.
def test_systemctl_start_ok(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 3)
    service.will_do("start", 0)
    out, err, status = service.invoke("start")
    assert status == 0
    assert service.did("status")
    assert service.did("start")


# systemctl start: service is already running.
def test_systemctl_start_running(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 0)
    out, err, status = service.invoke("start")
    assert status == 0
    assert service.did("status")
    assert not service.did("start")


# systemctl start: service is disabled.  Somewhat counter-intuitively, systemctl
# will happily start a disabled service if asked to do so.
def test_systemctl_start_not_enabled(sysvenv):
    service = sysvenv.create_service("foo")
    service.will_do("status", 3)
    service.will_do("start", 0)
    out, err, status = service.invoke("start")
    assert status == 0
    assert service.did("status")
    assert service.did("start")
    assert not service.is_enabled()


# systemctl stop: successful.
def test_systemctl_stop_ok(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 0)
    out, err, status = service.invoke("stop")
    assert status == 0
    assert service.did("status")
    assert service.did("stop")


# systemctl stop: service is not running.
def test_systemctl_stop_stopped(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 3)
    out, err, status = service.invoke("stop")
    assert status == 0
    assert service.did("status")
    assert not service.did("stop")


# systemctl is-active: service is running.
def test_systemctl_is_active_yes(sysvenv):
    service = sysvenv.create_service("foo")
    service.will_do("status", 0)
    out, err, status = service.invoke("is-active")
    assert status == 0
    assert service.did("status")


# systemctl is-active: service is not running.
def test_systemctl_is_active_no(sysvenv):
    service = sysvenv.create_service("foo")
    service.will_do("status", 3)
    out, err, status = service.invoke("is-active")
    assert status != 0
    assert service.did("status")


# systemctl is-active: multiple services, one of them reports running.
def test_systemctl_is_active_multi(sysvenv):
    svc0 = sysvenv.create_service("foo")
    svc0.will_do("status", 3)
    svc1 = sysvenv.create_service("bar")
    svc1.will_do("status", 0)
    svc2 = sysvenv.create_service("baz")
    svc2.will_do("status", 3)
    out, err, status = sysvenv.systemctl("is-active", svc0, svc1, svc2)
    assert status == 0
    assert svc0.did("status")
    assert svc1.did("status")
    assert svc2.did("status")
