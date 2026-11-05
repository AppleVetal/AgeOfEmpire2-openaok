"""Capture training started by the Russian UI's Town Center and Barracks hotkeys."""
from pathlib import Path
import subprocess

support = Path(__file__).resolve().parent
repo = support.parent / "OpenAoK-source"
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
cases = [("town-center-hotkey", "RTWC", "c", []),
         ("barracks-hotkey", "BRKS", "s", ["--place", "BRKS"])]
for name, unit, key, extra in cases:
    with (support / "logs" / f"{name}.stdout.log").open("wb") as output, \
         (support / "logs" / f"{name}.stderr.log").open("wb") as errors:
        result = subprocess.run([str(repo / "build/OpenAoK.exe"), "--windowed", "--mute",
                                 "--resolution", "1024x768", "--game", "--no-ai", *extra,
                                 "--select", unit, "--clicks", f"key:{key}", "--frames", "90",
                                 "--screenshot", str(support / "captures" / f"{name}.png")],
                                cwd=repo / "build", stdout=output, stderr=errors,
                                startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW,
                                timeout=90)
    print(f"{name}: exit={result.returncode}", flush=True)
    if result.returncode != 0:
        raise RuntimeError(f"Hotkey capture failed: {name}")
