"""Register generated human atlases; original PNG pixels are never rewritten."""
from pathlib import Path
import hashlib
import json
import shutil
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'art-source/directional-residents'
jobs = json.loads((WORK / 'prompts.json').read_text(encoding='utf-8'))['jobs']
world_path = ROOT / 'assets/art/world.json'
world = json.loads(world_path.read_text(encoding='utf-8'))
world['pages'] = [p for p in world['pages'] if not p['path'].startswith('art/resident8-')]
report = []
for job in jobs:
    name = f"resident8-{job['gender']}-{job['load']}.png"
    source = Path(job['source'])
    archive = WORK / 'sources' / name
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not source.exists():
        source = archive
    if source.resolve() != archive.resolve():
        shutil.copy2(source, archive)
    shutil.copy2(source, ROOT / 'assets/art' / name)
    im = Image.open(source)
    w, h = im.size
    pixels = np.asarray(im.convert('RGBA'))
    mode = job.get('alpha_mode')
    if mode == 'magenta':
        rgb = pixels[:, :, :3].astype(np.int16)
        mask = np.minimum(rgb[:, :, 0], rgb[:, :, 2]) - rgb[:, :, 1] < 120
    else:
        assert im.mode == 'RGBA' and (pixels[:, :, 3] == 0).mean() > .3, name
        mask = pixels[:, :, 3] > 96
    page = {'path': 'art/' + name, 'sprites': []}
    if mode:
        page['alpha_mode'] = mode
    for row in range(4):
        for col in range(8):
            x, right = round(col*w/8), round((col+1)*w/8)
            y, bottom = round(row*h/4), round((row+1)*h/4)
            cw, ch = right-x, bottom-y
            cell = mask[y:bottom, x:right]
            yy, xx = np.where(cell)
            assert len(xx) > 1000, (name, row, col)
            if job['load'] == 'cart':
                # Ground anchor is the person's feet, not the forward wheel.
                px = [.34,.36,.5,.66,.70,.67,.50,.38][col]
                py = [.84,.82,.88,.84,.84,.91,.93,.93][col]
            else:
                # Read-only alpha analysis anchors each gait frame at its feet.
                feet = cell.copy()
                feet[:int(yy.max() - (yy.max()-yy.min())*.18)] = False
                fy, fx = np.where(feet)
                px = float((fx.min()+fx.max())/2/cw)
                py = float((yy.max()+1)/ch)
            page['sprites'].append({
                'id': f"resident.{job['gender']}.{job['load']}.{col}.{row}",
                'rect': [x,y,cw,ch], 'pivot': [px,py]})
    world['pages'].append(page)
    report.append({'file': 'assets/art/'+name, 'size': [w,h], 'sprites': 32,
                   'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                   'alpha_mode': mode or 'source-alpha'})
ids = [s['id'] for p in world['pages'] for s in p['sprites']]
assert len(ids) == len(set(ids))
world_path.write_text(json.dumps(world, indent=2)+'\n', encoding='utf-8')
(WORK/'integration-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
catalog_path = ROOT/'content/asset_sources.json'
catalog = json.loads(catalog_path.read_text(encoding='utf-8-sig'))
for item in report:
    catalog['overrides'][item['file'].removeprefix('assets/')] = {
        'id': 'art.directional-residents.'+Path(item['file']).stem,
        'license': 'LicenseRef-Proprietary', 'author': 'Ribterra project / OpenAI image generation',
        'source': 'project', 'notice': 'licenses/Ribterra-Assets.txt'}
catalog_path.write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8')
print('Registered 256 directional resident frames across 8 original PNG plates.')
