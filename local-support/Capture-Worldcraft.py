"""Capture planting controls and saved stages through the game's mouse-event script."""
from pathlib import Path
import subprocess
import sys

support = Path(__file__).resolve().parent
repo = support.parent / "OpenAoK-source"
folder = support / "captures/worldcraft"
folder.mkdir(parents=True, exist_ok=True)
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
name = sys.argv[1] if len(sys.argv) > 1 else "controls"
extra = sys.argv[2:]
with (support / "logs" / f"worldcraft-{name}.stdout.log").open("wb") as output, \
     (support / "logs" / f"worldcraft-{name}.stderr.log").open("wb") as errors:
    result = subprocess.run([str(repo / "build/OpenAoK.exe"), "--windowed", "--mute",
        "--resolution", "1024x768", "--game", "--no-ai", "--select", "VMBAS",
        "--frames", "90", "--screenshot", str(folder / f"{name}.png"), *extra],
        cwd=repo / "build", stdout=output, stderr=errors, startupinfo=startup,
        creationflags=subprocess.CREATE_NO_WINDOW, timeout=180)
print(f"{name}: exit={result.returncode}", flush=True)
raise SystemExit(result.returncode)
