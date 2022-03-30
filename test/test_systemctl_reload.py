# systemctl reload: successful reload.
def test_systemctl_reload_ok(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("reload", 0)
    out, err, status = service.invoke("reload")
    assert status == 0
    assert not service.did("status")
    assert service.did("reload")


# systemctl reload: failed because the service is not running.
def test_systemctl_reload_stopped(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("reload", 3)
    out, err, status = service.invoke("reload")
    assert status == 3
    assert not service.did("status")
    assert service.did("reload")


# systemctl reload: failed for unspecified reasons.
def test_systemctl_reload_fail(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("reload", 1)
    out, err, status = service.invoke("reload")
    assert status == 1
    assert not service.did("status")
    assert service.did("reload")
