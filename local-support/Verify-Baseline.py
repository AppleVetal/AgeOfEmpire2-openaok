"""Run the already-built Windows engine and judge the upstream gate's scenarios."""
import json
import os
from pathlib import Path
import re
import subprocess
import time


support = Path(__file__).resolve().parent
repo = support.parent / "OpenAoK-source"
logs = support / "logs"
gate = (repo / "tools/check_determinism.sh").read_text(encoding="utf-8")
expected = list(dict.fromkeys(re.findall(r"grep '\^([a-z0-9]+)='", gate)))
golden = re.search(r'^GOLDEN="([^"]+)"', gate, re.MULTILINE).group(1)
float_pattern = re.compile(re.search(r"^FLOAT_PATTERN='([^']+)'", gate, re.MULTILINE).group(1))
assert len(expected) > 100 and "save" in expected and "net" in expected
env = os.environ.copy()
for name in tuple(env):
    if name.startswith("AOE_"):
        del env[name]
logs.mkdir(exist_ok=True)
started = time.monotonic()
with (logs / "verify-full.stdout.log").open("wb") as output, (logs / "verify-full.stderr.log").open("wb") as errors:
    result = subprocess.run([str(repo / "build/OpenAoK.exe"), "--verify", "3000"],
                            cwd=repo / "build", env=env, stdout=output, stderr=errors,
                            timeout=1800, creationflags=subprocess.CREATE_NO_WINDOW)
text = (logs / "verify-full.stdout.log").read_text(encoding="utf-8", errors="replace")
passed, failed = [], {}
for name in expected:
    lines = [line for line in text.splitlines() if line.startswith(name + "=")]
    line = lines[0] if len(lines) == 1 else "missing or duplicate verdict"
    if name == "entities":
        is_ok = line == golden
    elif name == "arrived":
        match = re.match(r"arrived=(\d+)/(\d+)(?: |$)", line)
        is_ok = bool(match and int(match[1]) > 0 and match[1] == match[2])
    else:
        is_ok = bool(re.match(re.escape(name) + r"=ok(?: |$)", line))
    if is_ok:
        passed.append(name)
    else:
        failed[name] = line
floating = []
for directory in ("ecs", "pathfinding", "physics", "ai"):
    for path in sorted((repo / "src" / directory).rglob("*")):
        if not path.is_file():
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
            if float_pattern.search(line.split("//", 1)[0]):
                floating.append(f"{path.relative_to(repo)}:{number}: {line}")
summary = {"exit_code": result.returncode, "seconds": round(time.monotonic() - started, 2),
           "expected_count": len(expected), "passed_count": len(passed), "passed": passed,
           "failed": failed, "golden": golden, "floating_point_violations": floating}
(logs / "verify-full.summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
print(f"ExitCode={result.returncode}; passed={len(passed)}/{len(expected)}; seconds={summary['seconds']}")
for name, line in failed.items():
    print(f"FAIL {name}: {line}")
print(f"Fixed-point source violations: {len(floating)}")
raise SystemExit(0 if result.returncode == 0 and not failed and not floating else 1)
