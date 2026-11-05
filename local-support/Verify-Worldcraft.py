"""Run the planting, persistence and real-socket regression checks."""
import json
import os
from pathlib import Path
import subprocess
import time

support = Path(__file__).resolve().parent
repo = support.parent / "OpenAoK-source"
env = {key: value for key, value in os.environ.items() if not key.startswith("AOE_")}
env["AOE_VERIFY_ONLY"] = "worldjobs,forestnet,compatibility,help,save,recording,net,lobby"
artifacts = support / "captures/worldcraft"
artifacts.mkdir(parents=True, exist_ok=True)
env["AOE_WORLDCRAFT_ARTIFACTS"] = str(artifacts)
started = time.monotonic()
with (support / "logs/worldcraft.stdout.log").open("wb") as output, \
     (support / "logs/worldcraft.stderr.log").open("wb") as errors:
    result = subprocess.run([str(repo / "build/OpenAoK.exe"), "--verify", "10"],
        cwd=repo / "build", env=env, stdout=output, stderr=errors,
        timeout=900, creationflags=subprocess.CREATE_NO_WINDOW)
text = (support / "logs/worldcraft.stdout.log").read_text(encoding="utf-8", errors="replace")
expected = env["AOE_VERIFY_ONLY"].split(",")
verdicts = {name: [line for line in text.splitlines() if line.startswith(name + "=")] for name in expected}
passed = all(lines == [name + "=ok"] for name, lines in verdicts.items())
summary = {"exit_code": result.returncode, "seconds": round(time.monotonic() - started, 2), "verdicts": verdicts}
(support / "logs/worldcraft.summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
print(json.dumps(summary, indent=2), flush=True)
raise SystemExit(0 if result.returncode == 0 and passed else 1)
