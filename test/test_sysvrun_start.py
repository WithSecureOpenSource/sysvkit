# sysvrun start: start directly
def test_start_direct(sysdenv, root):
    sysdsvc = sysdenv.create_service("foo")
    sysdsvc.execstart = [sysdenv.mockd, "syslog", "pidfile", "sleep"]
    sysdsvc.pidfile = True
    _, _, status = sysdsvc.invoke("start", debug=True)
    assert status == 0
    _, _, status = sysdsvc.invoke("status", debug=True)
    assert status == 0
    _, _, status = sysdsvc.invoke("stop", debug=True)
    assert status == 0
    _, _, status = sysdsvc.invoke("status", debug=True)
    assert status == 3


# sysvrun start: start via sysvinit
def test_start_sysvinit(sysdenv, root):
    sysdsvc = sysdenv.create_service("foo")
    sysdsvc.execstart = [sysdenv.mockd, "syslog", "pidfile", "sleep:1"]
    sysvsvc = sysdsvc.convert()
    out, err, status = sysvsvc.direct_invoke("start", debug=True)
    assert status == 0


# sysvrun start: start via systemctl
def test_start_systemctl(sysdenv, root):
    sysdsvc = sysdenv.create_service("foo")
    sysdsvc.execstart = [sysdenv.mockd, "syslog", "pidfile", "sleep:1"]
    sysvsvc = sysdsvc.convert()
    out, err, status = sysvsvc.invoke("start", debug=True)
    assert status == 0
