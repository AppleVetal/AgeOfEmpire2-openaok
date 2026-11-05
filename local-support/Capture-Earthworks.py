"""Capture earthwork controls or a saved match through the game's own test interface."""
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
repo = here.parent if (here.parent / 'CMakeLists.txt').is_file() else here.parent / 'OpenAoK-source'
support = repo.parent / 'OpenAoK-build-support'
folder = support / 'captures/earthworks'
folder.mkdir(parents=True, exist_ok=True)
logs = support / 'logs'
logs.mkdir(parents=True, exist_ok=True)
name = sys.argv[1] if len(sys.argv) > 1 else 'controls'
if not name or any(character in name for character in '/\\:'):
    raise SystemExit('Use a capture name without path separators')
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
with (logs / f'earthworks-{name}.stdout.log').open('wb') as output, (logs / f'earthworks-{name}.stderr.log').open('wb') as errors:
    result = subprocess.run([str(repo / 'build/OpenAoK.exe'), '--windowed', '--mute',
        '--resolution', '1024x768', '--game', '--no-ai', '--select', 'VMBAS',
        '--frames', '90', '--screenshot', str(folder / f'{name}.png'), *sys.argv[2:]],
        cwd=repo / 'build', stdout=output, stderr=errors, startupinfo=startup,
        creationflags=subprocess.CREATE_NO_WINDOW, timeout=180)
print(f'{name}: exit={result.returncode}', flush=True)
raise SystemExit(result.returncode)
