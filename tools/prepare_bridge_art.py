"""Pack the approved generated bridge plate without replacing existing atlases."""
import json
import shutil
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'art-source/runtime-0.17.0'
WORK.mkdir(parents=True, exist_ok=True)
(WORK / 'sprites').mkdir(exist_ok=True)
shutil.copyfile(ROOT / 'licenses/Ribterra-Assets.txt', WORK / 'NOTICE.txt')
plate = Image.open(ROOT / 'art-source/production-v3/bridge.png').convert('RGBA')
assert plate.getpixel((0, 0))[3] == 0, 'Bridge plate requires actual alpha'
records = []
for i, state in enumerate(('stage1', 'stage2', 'stage3', 'ready', 'winter', 'icon')):
    x, y = i % 3, i // 3
    cell = plate.crop((x*512, y*512, (x+1)*512, (y+1)*512))
    # Identical crop and pivot keep the deck at water level throughout construction.
    cell = cell.crop((40, 50, 490, 470))
    if state == 'icon':
        cell.thumbnail((128, 128), Image.Resampling.LANCZOS)
        icon = Image.new('RGBA', (136, 136))
        icon.alpha_composite(cell, ((136-cell.width)//2, (136-cell.height)//2))
        icon.save(ROOT / 'assets/ui/art/building-bridge.png')
        continue
    cell = cell.resize((320, 299), Image.Resampling.LANCZOS)
    ident = 'building.bridge.' + state
    relative = 'sprites/' + ident + '.png'
    cell.save(WORK / relative)
    records.append(dict(id=ident, path=relative, pivot=[.5, .43],
                        license='LicenseRef-Proprietary', source='project',
                        author='Ribterra project', notice='NOTICE.txt'))
config = dict(schema=1, page_size=2048, padding=3, max_pages=1, images=records)
(WORK / 'atlas.json').write_text(json.dumps(config, indent=2), encoding='utf8')
export = WORK / ('export-' + str(len(list(WORK.glob('export-*')))+1).zfill(3))
subprocess.run([str(ROOT / 'build/engine-windows/AtlasTool.exe'), str(WORK / 'atlas.json'), str(export)], check=True)
packed = json.loads((export / 'manifest.json').read_text())
world_path = ROOT / 'assets/art/world.json'
world = json.loads(world_path.read_text())
world['pages'] = [p for p in world['pages'] if not p['path'].startswith('art/bridges-017-')]
for i, page in enumerate(packed['pages']):
    filename = 'bridges-017-' + str(i) + '.png'
    shutil.copyfile(export / page['path'], ROOT / 'assets/art' / filename)
    entries = [dict(id=e['id'], rect=e['rect'], pivot=e['pivot']) for e in packed['entries'] if e['page'] == i]
    world['pages'].append(dict(path='art/' + filename, sprites=entries))
world_path.write_text(json.dumps(world, indent=2)+'\n', encoding='utf8')
print('Packed bridge: 5 world stages, 1 construction icon, 1 new atlas page')
