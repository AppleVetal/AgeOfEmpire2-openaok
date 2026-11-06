"""Verify earthworks together with planting, persistence, fog and real TCP peers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', default='earthworks,earthnet,worldjobs,forestnet,compatibility,help,save,recording,net,lobby,fog,foundations,combat')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo = here.parent if (here.parent / 'CMakeLists.txt').is_file() else here.parent / 'OpenAoK-source'
    support = repo.parent / 'OpenAoK-build-support'
    logs = support / 'logs'
    artifacts = support / 'captures/earthworks'
    logs.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith('AOE_')}
    env.update(AOE_VERIFY_ONLY=args.only, AOE_EARTHWORK_ARTIFACTS=str(artifacts))
    started = time.monotonic()
    with (logs / 'earthworks.stdout.log').open('wb') as output, (logs / 'earthworks.stderr.log').open('wb') as errors:
        result = subprocess.run([str(repo / 'build/OpenAoK.exe'), '--verify', '10'], cwd=repo / 'build',
            env=env, stdout=output, stderr=errors, timeout=900, creationflags=subprocess.CREATE_NO_WINDOW)
    lines = (logs / 'earthworks.stdout.log').read_text(encoding='utf-8', errors='replace').splitlines()
    verdicts = {name: [line for line in lines if line.startswith(name + '=')] for name in args.only.split(',')}
    with (repo / 'build/OpenAoK.exe').open('rb') as binary:
        engine_hash = hashlib.file_digest(binary, 'sha256').hexdigest()
    summary = dict(exit_code=result.returncode, seconds=round(time.monotonic() - started, 2),
        engineSha256=engine_hash, verdicts=verdicts)
    (logs / 'earthworks.summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2), flush=True)
    raise SystemExit(0 if result.returncode == 0 and all(value == [name + '=ok'] for name, value in verdicts.items()) else 1)


if __name__ == '__main__':
    main()
