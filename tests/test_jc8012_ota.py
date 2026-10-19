"""Migration must not arm a different image and must restore P4 after OTA failure."""
import importlib.util
import io
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location('jc_ota', Path(__file__).resolve().parents[1] / 'tools/ldn/jc8012_ota.py')
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


@pytest.mark.parametrize('matching', [False, True])
def test_serial_arming_checks_image_digest(monkeypatch, matching):
    digest = 'a' * 64
    class Link:
        def __init__(self, **kwargs):
            self.chunks = [b'sha256=' + (digest if matching else 'b'*64).encode(),
                           b'READY_FOR_INSTALL', b'OTA_ACTIVATED']
            self.sent = []
            self.closed = False
        def open(self): pass
        def close(self): self.closed = True
        def flush(self): pass
        @property
        def in_waiting(self): return 1
        def read(self, size): return self.chunks.pop(0)
        def write(self, data): self.sent.append(data)
    link = Link()
    monkeypatch.setattr(ota.serial, 'Serial', lambda **kwargs: link)
    monkeypatch.setattr(ota.time, 'sleep', lambda _: None)
    if matching:
        ota.arm('TEST', digest, io.BytesIO())
        assert link.sent == [('\nINSTALL '+digest+'\n').encode()]
    else:
        with pytest.raises(RuntimeError, match='differs'):
            ota.arm('TEST', digest, io.BytesIO())
        assert not link.sent
    assert link.closed


def test_failed_ota_restores_monitor(monkeypatch, tmp_path):
    p4 = bytearray(0x10000)
    p4[0x2000] = 0xe9
    p4[0x200c] = 18
    c6 = bytearray(512)
    c6[0] = 0xe9
    c6[12] = 13
    installer, monitor, app = (tmp_path / name for name in ('installer.bin', 'monitor.bin', 'c6.bin'))
    installer.write_bytes(p4)
    monitor.write_bytes(p4)
    app.write_bytes(c6)
    monkeypatch.setattr(ota.sys, 'argv', ['ota', '--port', 'TEST', '--installer', str(installer),
                        '--monitor', str(monitor), '--c6-app', str(app),
                        '--backup-dir', str(tmp_path / 'backup')])
    calls = []
    monkeypatch.setattr(ota.subprocess, 'run', lambda cmd, **kwargs: calls.append(cmd))
    def fail(*args): raise RuntimeError('C6 rejected OTA')
    monkeypatch.setattr(ota, 'arm', fail)
    with pytest.raises(RuntimeError, match='C6 rejected'):
        ota.main()
    assert 'read-flash' in calls[0]
    assert calls[1][-1] == str(installer)
    assert calls[-1][-1] == str(monitor)
    assert 'hard-reset' in calls[-1]
