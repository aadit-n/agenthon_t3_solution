"""Generate public outputs sequentially, then score in a separate Linux container.

This avoids Windows filesystem restrictions and limits validation memory. Only
the scoring container receives reference traces; the simulator receives configs.
"""
import argparse
import json
from pathlib import Path
import subprocess

from validate import launch


def main():
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--image", default="agenthon-t3:latest")
    parser.add_argument("--validator-image", default="agenthon-t3-validator:local")
    parser.add_argument("--track-repo", type=Path, default=root.parent / "track3-simulation-public")
    parser.add_argument("--fixture", type=Path, default=root / "results/reference-public")
    parser.add_argument("--output-dir", type=Path, default=root / "results/public")
    parser.add_argument("--unit", action="append")
    parser.add_argument("--score-only", action="store_true")
    parser.add_argument('--isolated-scoring',action='store_true',help='fresh bounded validator for each unit')
    parser.add_argument('--resume-scoring',action='store_true',help='retain already passing unit reports')
    parser.add_argument('--scorer-memory',default='1g')
    parser.add_argument('--scorer-memory-swap',default='3g')
    args = parser.parse_args()
    image_id=json.loads(subprocess.check_output(['docker','image','inspect',args.image],text=True))[0]['Id']
    output = args.output_dir.resolve()
    output.mkdir(parents=True,exist_ok=True)
    binding_path=output/'image.json'
    binding=json.loads(binding_path.read_text()) if binding_path.exists() else {}
    if args.score_only and binding.get('image_id')!=image_id:
        raise ValueError('Score-only requires outputs bound to this immutable image; generate fresh outputs first')
    if args.unit and binding and binding.get('image_id')!=image_id:
        raise ValueError('Use a fresh output directory for a different image with selected units')
    if not args.score_only:
        binding_path.write_text(json.dumps({'image_label':args.image,'image_id':image_id},indent=2)+'\n')
    runs = []
    if not args.score_only:
        for unit in sorted((args.fixture / "units").iterdir()):
            if not unit.is_dir() or (args.unit and unit.name not in args.unit):
                continue
            batch = (unit / "batch.json").is_file()
            elapsed = launch(image_id, unit / "scenarios" if batch else unit / "scenario.json",
                             output / unit.name, "simulate-batch" if batch else "simulate",
                             ["--batch-dir", "/input", "--out-dir", "/output"] if batch else
                             ["--config", "/input/scenario.json", "--out", "/output/trace.parquet"])
            runs.append({"unit": unit.name, "container_elapsed_sec": elapsed})
            (output / "run-times.json").write_text(json.dumps(runs, indent=2) + "\n")
            print(f"SIMULATED {unit.name} in {elapsed:.3f}s", flush=True)
    command = ["docker", "run", "--rm", "--network", "none", "--cpus", "4",
               "--memory", args.scorer_memory, "--memory-swap", args.scorer_memory_swap,
               "-v", str(args.fixture.resolve()) + ":/references:ro",
               "-v", str(args.track_repo.resolve()) + ":/track:ro",
               "-v", str(root / "scripts") + ":/scripts:ro",
               "-v", str(output) + ":/reports", args.validator_image,
               "/scripts/validate.py", "--reuse", "--image", args.image,
               "--track-repo", "/references", "--scoring-repo", "/track", "--output-dir", "/reports"]
    if not args.isolated_scoring:
        for unit in args.unit or []:
            command.extend(["--unit", unit])
        return subprocess.run(command).returncode
    report_path=output/'validation.json'
    collected=json.loads(report_path.read_text()) if args.resume_scoring and report_path.exists() else {
        'image':args.image,'image_id':image_id,'rankable':False,'toolkit_version':'2.5.1','units':[]}
    if args.resume_scoring and collected.get('image_id')!=image_id:
        raise ValueError('Cannot resume unbound/different image results; rerun without --resume-scoring')
    retained={row['unit']:row for row in collected['units']}
    for unit in sorted((args.fixture/'units').iterdir()):
        if not unit.is_dir() or (args.unit and unit.name not in args.unit):
            continue
        if args.resume_scoring and retained.get(unit.name,{}).get('passed'):
            print('RETAINED '+unit.name,flush=True)
            continue
        status=subprocess.run(command+['--unit',unit.name]).returncode
        if status not in (0,1):
            # A killed container must not erase evidence from completed units.
            report_path.write_text(json.dumps(collected,indent=2)+'\n')
            return status
        fresh=json.loads(report_path.read_text())
        for row in fresh['units']:
            retained[row['unit']]=row
        collected['units']=[retained[key] for key in sorted(retained)]
        collected['passed']=sum(row['passed'] for row in collected['units'])
        collected['failed']=len(collected['units'])-collected['passed']
        report_path.write_text(json.dumps(collected,indent=2,allow_nan=False)+'\n')
    print(f"{collected['passed']} passed, {collected['failed']} failed",flush=True)
    return bool(collected['failed'])


if __name__ == "__main__":
    raise SystemExit(main())
