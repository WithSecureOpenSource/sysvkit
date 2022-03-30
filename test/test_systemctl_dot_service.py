# Test that systemctl will accept service names both with or without suffix.
def test_dot_service(sysvenv):
    service = sysvenv.create_service("foo")
    service.will_do("status", 3)
    service.direct_enable()
    out, err, status = sysvenv.systemctl("status", "foo.service")
    assert status == 3
    assert service.did("status")
