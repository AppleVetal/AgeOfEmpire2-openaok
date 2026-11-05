"""Capture a local match and a two-process loopback match using the engine's CLI."""
from contextlib import ExitStack
import json
from pathlib import Path
import socket
import subprocess
import time


support = Path(__file__).resolve().parent
repo = support.parent / "OpenAoK-source"
captures = support / "captures"
logs = support / "logs"
captures.mkdir(exist_ok=True)
common = [str(repo / "build/OpenAoK.exe"), "--windowed", "--resolution", "1024x768", "--mute"]
processes = []
results = {}


def start(stack, name, arguments):
    output = stack.enter_context((logs / f"{name}.stdout.log").open("wb"))
    errors = stack.enter_context((logs / f"{name}.stderr.log").open("wb"))
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = subprocess.SW_HIDE
    process = subprocess.Popen([*common, *arguments, "--screenshot", str(captures / f"{name}.png")],
                               cwd=repo / "build", stdout=output, stderr=errors,
                               startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    processes.append(process)
    return process


try:
    with ExitStack() as stack:
        menu = start(stack, "main-menu", ["--menu", "main", "--frames", "5"])
        results["main-menu"] = menu.wait(timeout=30)
        single = start(stack, "single-player", ["--game", "--advance", "60", "--select", "VMBAS",
                                               "--build-menu", "economic", "--frames", "180"])
        results["single-player"] = single.wait(timeout=90)
        print(f"Single-player capture: exit {results['single-player']}", flush=True)
        if results["single-player"] != 0:
            raise RuntimeError("Single-player launch failed; see logs")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        host = start(stack, "network-host", ["--host", str(port), "--players", "2", "--no-ai",
                                            "--frames", "600", "--perf"])
        deadline = time.monotonic() + 15
        while f"(port {port})" not in (logs / "network-host.stdout.log").read_text(encoding="utf-8", errors="replace"):
            if host.poll() is not None or time.monotonic() >= deadline:
                raise RuntimeError("Host did not open the lobby; see logs")
            time.sleep(0.1)
        client = start(stack, "network-client", ["--join", f"127.0.0.1:{port}", "--no-ai",
                                                "--frames", "600", "--perf"])
        results["network-host"] = host.wait(timeout=90)
        results["network-client"] = client.wait(timeout=30)
        results["port"] = port
        print(f"Network captures: host={results['network-host']}, client={results['network-client']}", flush=True)
    for name in ("main-menu", "single-player", "network-host", "network-client"):
        if results[name] != 0 or not (captures / f"{name}.png").is_file():
            raise RuntimeError(f"Capture failed: {name}")
        errors = (logs / f"{name}.stderr.log").read_text(encoding="utf-8", errors="replace")
        if "[Text] No face" in errors:
            raise RuntimeError(f"Missing font in {name}: {errors}")
    for name, seat in (("network-host", 1), ("network-client", 2)):
        text = (logs / f"{name}.stdout.log").read_text(encoding="utf-8", errors="replace")
        if f"[Net] Playing seat {seat} of 2" not in text:
            raise RuntimeError(f"Network seat was not confirmed: {name}")
    (logs / "capture-summary.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
finally:
    for process in processes:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
