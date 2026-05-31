#!/usr/bin/env python3
"""
Push firmware.bin to one or more NerdMiner units over WiFi.

Usage:
  python tools/ota_push.py
  python tools/ota_push.py path/to/firmware.bin
  python tools/ota_push.py path/to/firmware.bin 192.168.87.21 192.168.87.22 192.168.87.23
"""
import sys, threading, time, socket
from pathlib import Path

DEFAULT_FIRMWARE = Path(".pio/build/ESP32_2432S028_2USB/firmware.bin")
DEFAULT_IPS      = ["192.168.87.21", "192.168.87.22", "192.168.87.23"]
PORT             = 8080

def device_alive(ip, timeout=3):
    try:
        s = socket.create_connection((ip, PORT), timeout=timeout)
        s.close()
        return True
    except OSError:
        return False

def push(ip, firmware_bytes):
    import urllib.request
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
        # Verify device comes back up
        _wait_for_reboot(ip)
    except (OSError, ConnectionResetError) as e:
        # Device may have rebooted before TCP close completed
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

def _wait_for_reboot(ip, wait=60, poll=2):
    # Give device time to flash + restart + reconnect WiFi
    deadline = time.time() + wait
    time.sleep(5)  # initial pause while device resets
    while time.time() < deadline:
        if device_alive(ip, timeout=2):
            print(f"[{ip}] Back online - OTA complete")
            return
        time.sleep(poll)
    print(f"[{ip}] WARNING: device did not respond within {wait}s")

if __name__ == "__main__":
    args = sys.argv[1:]

    firmware_path = DEFAULT_FIRMWARE
    ips = []
    for a in args:
        if a[0].isdigit() and "." in a:
            ips.append(a)
        else:
            firmware_path = Path(a)

    if not ips:
        ips = DEFAULT_IPS

    if not firmware_path.exists():
        print(f"Firmware not found: {firmware_path}")
        print(f"Build first:  pio run -e ESP32_2432S028_2USB")
        sys.exit(1)

    firmware_bytes = firmware_path.read_bytes()
    print(f"Firmware : {firmware_path}  ({len(firmware_bytes)//1024} KB)")
    print(f"Targets  : {', '.join(ips)}")
    print()

    threads = [
        threading.Thread(target=push, args=(ip, firmware_bytes), daemon=True)
        for ip in ips
    ]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print("\nDone.")
