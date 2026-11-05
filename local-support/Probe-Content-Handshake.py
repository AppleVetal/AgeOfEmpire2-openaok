"""Probe the real executable with a peer declaring incompatible game data."""
import argparse
import json
from pathlib import Path
import socket
import struct
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument("--protocol", type=int, required=True)
parser.add_argument("--expect", choices=("accepted", "rejected"), required=True)
args = parser.parse_args()
support = Path(__file__).resolve().parent
repo = support.parent / "OpenAoK-source"
name = f"content-handshake-v{args.protocol}"
with socket.socket() as probe:
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]
with (support / "logs" / f"{name}.stdout.log").open("wb") as output, \
     (support / "logs" / f"{name}.stderr.log").open("wb") as errors:
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    host = subprocess.Popen([str(repo / "build/OpenAoK.exe"), "--host", str(port),
                             "--players", "2", "--no-ai", "--mute", "--windowed"],
                            cwd=repo / "build", stdout=output, stderr=errors,
                            startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        deadline = time.monotonic() + 30
        log = support / "logs" / f"{name}.stdout.log"
        while f"(port {port})" not in log.read_text(encoding="utf-8", errors="replace"):
            if host.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("Host did not open the listening port")
            time.sleep(0.1)
        with socket.create_connection(("127.0.0.1", port), timeout=10) as peer:
            name_bytes = b"incompatible-data"
            # Unknown simulation revision and deliberately different data hash.
            payload = b"H" + bytes([args.protocol]) + struct.pack("<IQ", 999, 1)
            payload += struct.pack("<H", len(name_bytes)) + name_bytes
            peer.sendall(struct.pack("<I", len(payload)) + payload)

            def receive(size):
                result = b""
                while len(result) < size:
                    part = peer.recv(size - len(result))
                    if not part:
                        raise RuntimeError("Peer closed before a complete response")
                    result += part
                return result

            length = struct.unpack("<I", receive(4))[0]
            if not 0 < length < 1_000_000:
                raise RuntimeError("Invalid response frame length")
            answer = receive(length)
            result = "accepted" if answer[:1] == b"W" else "rejected" if answer[:1] == b"E" else "unknown"
            details = {"protocol": args.protocol, "result": result, "expected": args.expect,
                       "response_kind": answer[:1].decode("ascii", errors="replace")}
            (support / "logs" / f"{name}.json").write_text(json.dumps(details, indent=2) + "\n")
            print(json.dumps(details), flush=True)
            if result != args.expect:
                raise RuntimeError("Unexpected compatibility result")
    finally:
        if host.poll() is None:
            host.terminate()
        host.wait(timeout=10)
