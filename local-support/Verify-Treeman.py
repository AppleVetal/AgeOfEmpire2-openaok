"""Check walking-tree growth, wild population, permanent taming, saves and TCP lockstep."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    repo = Path(__file__).resolve().parent.parent
    support = repo.parent / 'OpenAoK-build-support'
    logs = support / 'logs'
    artifacts = support / 'captures/treeman'
    logs.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    names = 'treeman,worldjobs,earthworks,earthnet,forestnet,compatibility,help,save,recording,net,lobby,fog,foundations,combat,population'
    env = {key: value for key, value in os.environ.items() if not key.startswith('AOE_')}
    env.update(AOE_VERIFY_ONLY=names, AOE_TREEMAN_ARTIFACTS=str(artifacts))
    started = time.monotonic()
    with (logs / 'treeman.stdout.log').open('wb') as output, (logs / 'treeman.stderr.log').open('wb') as errors:
        result = subprocess.run([str(repo / 'build/OpenAoK.exe'), '--verify', '10'], cwd=repo / 'build',
            env=env, stdout=output, stderr=errors, timeout=1200, creationflags=subprocess.CREATE_NO_WINDOW)
    lines = (logs / 'treeman.stdout.log').read_text(encoding='utf-8', errors='replace').splitlines()
    verdicts = {name: [line for line in lines if line.startswith(name + '=')] for name in names.split(',')}
    with (repo / 'build/OpenAoK.exe').open('rb') as binary:
        engine_hash = hashlib.file_digest(binary, 'sha256').hexdigest()
    summary = dict(exit_code=result.returncode, seconds=round(time.monotonic() - started, 2),
        engineSha256=engine_hash, verdicts=verdicts)
    (logs / 'treeman.summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2), flush=True)
    passed = all(len(value) == 1 and value[0].split(' ', 1)[0] == name + '=ok'
        for name, value in verdicts.items())
    raise SystemExit(0 if result.returncode == 0 and passed else 1)


if __name__ == '__main__':
    main()
