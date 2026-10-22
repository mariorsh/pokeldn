"""Host tests for the P4/C6 record boundary and passive screen event model."""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.skipif(shutil.which("cc") is None, reason="no C compiler")
def test_sdio_records_and_screen_events(tmp_path):
    board = ROOT / "firmware/jc8012p4"
    binary = tmp_path / "jc8012-test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
        "-I", str(board / "common"), "-I", str(board / "main"),
        "-I", str(ROOT / "firmware/esp32/main"),
        str(ROOT / "tests/native/jc8012.c"), str(board / "common/packet.c"),
        str(board / "main/events.c"), str(board / "main/dashboard.c"),
        str(ROOT / "firmware/esp32/main/screen.c"), "-o", str(binary),
    ], check=True, capture_output=True, text=True)
    result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
    assert "checks passed" in result.stdout


def test_p4_identity_uses_usb_bridge_and_has_no_stock_firmware_update():
    from gui import board
    from pokeldn.ldn import esp32
    text = "pokeldn-radio esp32p4 version=1.6.3 board=JC8012P4A1C_I_W_Y radio=esp32c6"
    ident = board.Identity("02:11:22:33:44:55", "02:66:77:88:99:aa", 1, text, 1, "1.6.3")
    assert ident.current and ident.chip == "ESP32-P4"
    assert not board.wrong_port(board.Port("COM3", "WCH CH340", ""), ident.chip)
    assert board.update_for(ident.firmware_version, "radio", ident.chip) == ""
    info = esp32.Info(1, bytes(6), bytes(6), 1, text)
    assert esp32.firmware_version(info) >= esp32.ALIVE_FIRMWARE


@pytest.mark.parametrize('setting', ['921600', '1500000'])
def test_app_caps_jc8012_speed_and_rediscovers_it_after_close(monkeypatch, setting):
    import serial
    from pokeldn.ldn import esp32, esp32_sim

    class P4Board(esp32_sim.SimulatedBoard):
        def _emit(self, kind, payload):
            if kind == esp32.MSG_INFO:
                payload = payload[:14] + b'pokeldn-radio esp32p4 version=1.6.3 board=JC8012P4A1C_I_W_Y radio=esp32c6'
            return super()._emit(kind, payload)

    board = P4Board(esp32_sim.Air())
    board.version = '1.6.3'
    monkeypatch.setattr(serial, 'Serial', lambda: esp32_sim.UartPort(board))
    monkeypatch.setenv('POKELDN_ESP32_BAUD', setting)
    radio = esp32.Radio.open_serial('TEST')
    try:
        assert board.line_rate == 460800
        assert 'board=JC8012P4A1C_I_W_Y' in radio.hello().text
    finally:
        radio.close()
    # An identify/reconnect must find the retained rate without a hardware reset.
    radio = esp32.Radio.open_serial('TEST', fast_baud=0)
    try:
        assert radio._stream.baudrate == 460800
        assert radio.hello().version == esp32.PROTOCOL_VERSION
    finally:
        radio.close()
