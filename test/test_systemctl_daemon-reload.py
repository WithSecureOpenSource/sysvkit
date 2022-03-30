# systemctl daemon-reload: no-op.
def test_systemctl_daemon_reload(sysvenv):
    out, err, status = sysvenv.systemctl("daemon-reload")
    assert status == 0
