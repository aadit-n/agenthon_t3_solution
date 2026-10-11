"""Copy only public configuration inputs into diagnostic benchmark mounts."""
from pathlib import Path
import shutil

root=Path(__file__).resolve().parents[1]
source=root.parent/'track3-simulation-public/units';target=root/'results/v4-configs'
for unit in sorted(source.iterdir()):
    if not unit.is_dir():continue
    out=target/unit.name;out.mkdir(parents=True,exist_ok=True)
    if (unit/'batch.json').exists():
        (out/'batch.json').write_text('{}\n')
        (out/'scenarios').mkdir(exist_ok=True)
        for config in (unit/'scenarios').glob('*.json'):shutil.copyfile(config,out/'scenarios'/config.name)
    else:shutil.copyfile(unit/'scenario.json',out/'scenario.json')
print(target)
