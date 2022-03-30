# systemctl try-reload-or-restart: successful reload.
def test_systemctl_try_reload_or_restart_ok(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 0)
    service.will_do("reload", 0)
    out, err, status = service.invoke("try-reload-or-restart")
    assert status == 0
    assert service.did("status")
    assert service.did("reload")
    assert not service.did("restart")


# systemctl try-reload-or-restart: reload is unsupported.
def test_systemctl_try_reload_or_restart_unsup(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 0)
    service.will_do("reload", 3)
    service.will_do("restart", 0)
    out, err, status = service.invoke("try-reload-or-restart")
    assert status == 0
    assert service.did("status")
    assert service.did("reload")
    assert service.did("restart")


# systemctl try-reload-or-restart: service is not running.
def test_systemctl_try_reload_or_restart_stopped(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 3)
    out, err, status = service.invoke("try-reload-or-restart")
    assert status == 0
    assert service.did("status")
    assert not service.did("reload")
    assert not service.did("restart")


# systemctl try-reload-or-restart: both commands failed.
def test_systemctl_try_reload_or_restart_fail(sysvenv):
    service = sysvenv.create_service("foo")
    service.direct_enable()
    service.will_do("status", 0)
    service.will_do("reload", 3)
    service.will_do("restart", 1)
    out, err, status = service.invoke("try-reload-or-restart")
    assert status == 1
    assert service.did("status")
    assert service.did("reload")
    assert service.did("restart")
