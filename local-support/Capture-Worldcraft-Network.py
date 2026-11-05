"""Exercise planting mouse drags in two separate network game processes."""
from contextlib import ExitStack
import json
from pathlib import Path
import socket
import subprocess
import time

support = Path(__file__).resolve().parent
repo = support.parent / "OpenAoK-source"
folder = support / "captures/worldcraft"
folder.mkdir(parents=True, exist_ok=True)
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
common = [str(repo / "build/OpenAoK.exe"), "--windowed", "--mute", "--resolution", "1024x768",
          "--no-ai", "--select", "VMBAS", "--frames", "900"]
processes = []
results = {}

try:
    with ExitStack() as stack:
        def start(name, arguments):
            output = stack.enter_context((support / "logs" / f"worldcraft-{name}.stdout.log").open("wb"))
            errors = stack.enter_context((support / "logs" / f"worldcraft-{name}.stderr.log").open("wb"))
            process = subprocess.Popen([*common, *arguments, "--screenshot", str(folder / f"{name}.png")],
                cwd=repo / "build", stdout=output, stderr=errors, startupinfo=startup,
                creationflags=subprocess.CREATE_NO_WINDOW)
            processes.append(process)
            return process

        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        host = start("network-line", ["--host", str(port), "--players", "2", "--reveal", "all",
            "--clicks", "56,686;down:600,300;move:750,372;up:750,372;move:950,550"])
        deadline = time.monotonic() + 30
        while f"(port {port})" not in (support / "logs/worldcraft-network-line.stdout.log").read_text(encoding="utf-8", errors="replace"):
            if host.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("Planting host did not open its listener")
            time.sleep(0.1)
        client = start("network-area", ["--join", f"127.0.0.1:{port}",
            "--clicks", "97,686;down:620,300;move:620,372;up:620,372;move:950,550"])
        results["network-line"] = host.wait(timeout=120)
        results["network-area"] = client.wait(timeout=30)
    for name, seat in (("network-line", 1), ("network-area", 2)):
        log = (support / "logs" / f"worldcraft-{name}.stdout.log").read_text(encoding="utf-8", errors="replace")
        if results[name] != 0 or f"[Net] Playing seat {seat} of 2" not in log or not (folder / f"{name}.png").is_file():
            raise RuntimeError(f"Planting window failed: {name}")
    (support / "logs/worldcraft-network-ui.summary.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(results), flush=True)
finally:
    for process in processes:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
