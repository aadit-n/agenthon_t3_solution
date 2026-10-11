"""Differentially check the complete native legacy distribution stream."""
import os
os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
os.environ.setdefault('OMP_NUM_THREADS','1')
import argparse
import json
from pathlib import Path
import subprocess
import numpy as np

def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser()
    parser.add_argument('--image',default='agenthon-t3-cpp:dev')
    parser.add_argument('--reference-image',default='agenthon-t3:v2')
    parser.add_argument('--reference-only',action='store_true')
    parser.add_argument('--output-dir',type=Path,default=root/'results/cpp-random')
    args=parser.parse_args();args.output_dir.mkdir(parents=True,exist_ok=True)
    if not args.reference_only:
        subprocess.run(['docker','run','--rm','--network','none','--entrypoint','python',
            '-v',str(Path(__file__).resolve().parent)+':/scripts:ro','-v',str(args.output_dir.resolve())+':/test',
            args.reference_image,'/scripts/check_cpp_random.py','--reference-only','--output-dir','/test'],check=True)
        image=json.loads(subprocess.check_output(['docker','image','inspect',args.image]))[0]['Id']
    checks=[]
    for seed in (0,1234567,2**32-1):
        for upper in (1,2,6,17,65537,2**32+3):
            config={'seed':seed,'upper':upper,'count':5000,'mean':10.3,'scale':4.2}
            reference_path=args.output_dir/f'expected-{seed}-{upper}.bin'
            if args.reference_only:
                rng=np.random.RandomState(seed)
                expected=np.array([(rng.normal(10.3,4.2),rng.randint(2),rng.randint(upper),
                    rng.lognormal(6.214608098422191,.3),rng.pareto(1.5),rng.exponential(100),rng.uniform(100,2000))
                    for _ in range(config['count'])])
                expected.tofile(reference_path)
                continue
            (args.output_dir/'config.json').write_text(json.dumps(config))
            subprocess.run(['docker','run','--rm','--network','none','--cpus','4','-v',str(args.output_dir.resolve())+':/test',image,
                            'random-test','--config','/test/config.json','--out','/test/draws.bin'],check=True,capture_output=True)
            actual=np.fromfile(args.output_dir/'draws.bin',dtype=np.float64).reshape(-1,7)
            expected=np.fromfile(reference_path,dtype=np.float64).reshape(-1,7)
            np.testing.assert_array_equal(actual,expected,err_msg=str((seed,upper)))
            checks.append({'seed':seed,'upper':upper,'draws':int(actual.size),'exact':True})
            print(f'EXACT seed={seed} upper={upper}: {actual.size} draws',flush=True)
    if args.reference_only:
        (args.output_dir/'reference.json').write_text(json.dumps({'numpy_version':np.__version__})+'\n')
        return 0
    (args.output_dir/'validation.json').write_text(json.dumps({'image_id':image,'reference':json.loads((args.output_dir/'reference.json').read_text()),
        'draws':sum(c['draws'] for c in checks),'checks':checks},indent=2)+'\n')
    return 0

if __name__=='__main__':
    raise SystemExit(main())
