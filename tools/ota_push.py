#!/usr/bin/env python3
"""
Push firmware to one or more NerdMiner units.

WiFi OTA (default):
  python tools/ota_push.py
  python tools/ota_push.py path/to/firmware.bin
  python tools/ota_push.py path/to/firmware.bin 192.168.87.21 192.168.87.22

Full USB flash — use this once per unit to fix partition table:
  python tools/ota_push.py --port COM3
  python tools/ota_push.py --port COM3 path/to/firmware.bin

The --port mode writes bootloader + partitions + firmware via esptool.
Required for any new/fresh unit before WiFi OTA will work (default partition
table ships with 1280KB app slots; firmware is ~2MB, OTA truncates it and
the device rolls back on every push).
"""
import sys, subprocess, threading, time, socket
from pathlib import Path

BUILD_DIR        = Path(".pio/build/ESP32_2432S028_2USB")
DEFAULT_FIRMWARE = BUILD_DIR / "firmware.bin"
DEFAULT_IPS      = ["192.168.87.21", "192.168.87.22", "192.168.87.23", "192.168.87.24"]
PORT             = 8080
BAUD             = 921600

def _find_esptool():
    candidates = [
        Path.home() / ".platformio/packages/tool-esptoolpy/esptool.py",
        Path("C:/Users") / Path.home().name / ".platformio/packages/tool-esptoolpy/esptool.py",
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    return None  # fall back to python -m esptool

def usb_flash(port, firmware_path):
    bl  = BUILD_DIR / "bootloader.bin"
    pts = BUILD_DIR / "partitions.bin"
    fw  = firmware_path

    missing = [str(p) for p in (bl, pts, fw) if not p.exists()]
    if missing:
        print(f"[{port}] Missing build artifacts: {', '.join(missing)}")
        print(f"[{port}] Build first: pio run -e ESP32_2432S028_2USB")
        return False

    esptool = _find_esptool()
    if esptool:
        cmd = [sys.executable, esptool]
    else:
        cmd = [sys.executable, "-m", "esptool"]

    cmd += [
        "--port", port,
        "--baud", str(BAUD),
        "--chip", "esp32",
        "write_flash",
        "0x1000",  str(bl),
        "0x8000",  str(pts),
        "0x10000", str(fw),
    ]

    print(f"[{port}] Full flash: bootloader + partitions + firmware ({fw.stat().st_size // 1024} KB)")
    print(f"[{port}] {' '.join(cmd)}")
    t0 = time.time()
    result = subprocess.run(cmd, capture_output=False)
    elapsed = time.time() - t0
    if result.returncode == 0:
        print(f"[{port}] Flash complete ({elapsed:.1f}s)")
        return True
    else:
        print(f"[{port}] esptool exited {result.returncode}")
        return False

def device_alive(ip, timeout=3):
    try:
        s = socket.create_connection((ip, PORT), timeout=timeout)
        s.close()
        return True
    except OSError:
        return False

def _wait_for_reboot(ip, wait=120, poll=2):
    deadline = time.time() + wait
    time.sleep(5)
    while time.time() < deadline:
        if device_alive(ip, timeout=2):
            print(f"[{ip}] Back online — OTA complete")
            return
        time.sleep(poll)
    print(f"[{ip}] WARNING: device did not respond within {wait}s")

def wifi_push(ip, firmware_bytes):
    import urllib.request, urllib.error
    boundary = b"----NerdMinerOTA"
    body = (
        b"--" + boundary + b"\r\n"
        b'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
        b"Content-Type: application/octet-stream\r\n\r\n"
        + firmware_bytes
        + b"\r\n--" + boundary + b"--\r\n"
    )
    url = f"http://{ip}:{PORT}/update"
    req = urllib.request.Request(url, data=body, method="POST")
    req.add_header("Content-Type", f"multipart/form-data; boundary={boundary.decode()}")
    req.add_header("Content-Length", str(len(body)))
    t0 = time.time()
    print(f"[{ip}] Pushing {len(firmware_bytes)//1024} KB...")
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            text = resp.read().decode()
        elapsed = time.time() - t0
        print(f"[{ip}] {resp.status} {text}  ({elapsed:.1f}s)")
        _wait_for_reboot(ip)
    except (OSError, ConnectionResetError) as e:
        if any(x in str(e) for x in ("10054", "10053", "reset", "closed", "abort", "forcibly")):
            elapsed = time.time() - t0
            print(f"[{ip}] Upload sent ({elapsed:.1f}s) — device restarting...")
            _wait_for_reboot(ip)
        else:
            print(f"[{ip}] FAILED: {e}")
    except urllib.error.HTTPError as e:
        print(f"[{ip}] HTTP {e.code}: {e.read().decode()}")
    except Exception as e:
        print(f"[{ip}] FAILED: {e}")

if __name__ == "__main__":
    args = sys.argv[1:]

    # Parse --port for USB flash mode
    usb_port = None
    filtered = []
    i = 0
    while i < len(args):
        if args[i] == "--port" and i + 1 < len(args):
            usb_port = args[i + 1]
            i += 2
        elif args[i].startswith("--port="):
            usb_port = args[i].split("=", 1)[1]
            i += 1
        else:
            filtered.append(args[i])
            i += 1
    args = filtered

    firmware_path = DEFAULT_FIRMWARE
    ips = []
    for a in args:
        if a[0].isdigit() and "." in a:
            ips.append(a)
        else:
            firmware_path = Path(a)

    if usb_port:
        print(f"Mode     : USB full flash -> {usb_port}")
        print(f"Firmware : {firmware_path}")
        print()
        usb_flash(usb_port, firmware_path)
        print("\nDone.")
        sys.exit(0)

    # WiFi OTA mode
    if not ips:
        ips = DEFAULT_IPS

    if not firmware_path.exists():
        print(f"Firmware not found: {firmware_path}")
        print(f"Build first:  pio run -e ESP32_2432S028_2USB")
        sys.exit(1)

    firmware_bytes = firmware_path.read_bytes()
    print(f"Mode     : WiFi OTA")
    print(f"Firmware : {firmware_path}  ({len(firmware_bytes)//1024} KB)")
    print(f"Targets  : {', '.join(ips)}")
    print()

    threads = [
        threading.Thread(target=wifi_push, args=(ip, firmware_bytes), daemon=True)
        for ip in ips
    ]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print("\nDone.")
