"""Crop generated RGBA construction plates and pack a separate, reproducible atlas."""
import json
import shutil
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'art-source/production-v2'
WORK = ROOT / 'art-source/runtime-0.14.0'
STATES = ('stage1', 'stage2', 'stage3', 'ready', 'winter')

def isolate_cell(cell):
    """Discard only small boundary fragments belonging to an adjacent grid cell."""
    mask = cell.getchannel('A').point(lambda v: 255 if v > 32 else 0)
    remaining = np.array(mask)
    rgba = np.array(cell)
    while np.any(remaining):
        y, x = np.argwhere(remaining > 0)[0]
        component = Image.fromarray(remaining).copy()
        ImageDraw.floodfill(component, (int(x), int(y)), 128)
        pixels = np.asarray(component) == 128
        remaining[pixels] = 0
        ys, xs = np.where(pixels)
        boundary = xs.min() < 3 or ys.min() < 3 or xs.max() >= cell.width-3 or ys.max() >= cell.height-3
        if boundary and len(xs) < 2000:
            rgba[pixels, 3] = 0
    return Image.fromarray(rgba)

def main():
    (WORK / 'sprites').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'licenses/Ribterra-Assets.txt', WORK / 'NOTICE.txt')
    records = []
    plates = json.loads((SOURCE / 'prompts.json').read_text(encoding='utf8'))['plates']
    for plate in plates:
        name = plate['id']
        source = SOURCE / (name + '.png')
        if not source.exists():
            shutil.copyfile(plate['source'], source)
        image = Image.open(source).convert('RGBA')
        assert image.getpixel((0, 0))[3] == 0, 'Generated plate must have real alpha: ' + name
        cells = []
        for i in range(6):
            x, y = i % 3, i // 3
            cell = image.crop((round(x*image.width/3), round(y*image.height/2),
                               round((x+1)*image.width/3), round((y+1)*image.height/2)))
            cell = isolate_cell(cell)
            box = cell.getchannel('A').point(lambda v: 255 if v > 32 else 0).getbbox()
            assert box, 'Empty frame: ' + name
            cells.append(cell.crop((max(0, box[0]-2), max(0, box[1]-2),
                                    min(cell.width, box[2]+2), min(cell.height, box[3]+2))))
        width = max(c.width for c in cells[:5]) + 8
        height = max(c.height for c in cells[:5]) + 8
        for state, cell in zip(STATES, cells):
            canvas = Image.new('RGBA', (width, height))
            canvas.alpha_composite(cell, ((width-cell.width)//2, height-cell.height-4))
            # Uniform reduction preserves common scale and foot anchor across all five stages.
            target = (320, round(height*320/width))
            canvas = canvas.resize(target, Image.Resampling.LANCZOS)
            ident = 'building.' + name + '.' + state
            path = 'sprites/' + ident + '.png'
            canvas.save(WORK / path)
            records.append(dict(id=ident, path=path, pivot=[.5, 1-4/height],
                                license='LicenseRef-Proprietary', source='project',
                                author='Ribterra project', notice='NOTICE.txt'))
        icon = cells[5]
        icon.thumbnail((128, 128), Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', (136, 136))
        canvas.alpha_composite(icon, ((136-icon.width)//2, (136-icon.height)//2))
        canvas.save(ROOT / ('assets/ui/art/building-' + name + '.png'))
    for name in ('house', 'farm', 'stockpile'):
        image = Image.open(ROOT / ('art-source/runtime-0.11.1/sprites/building.'+name+'.ready.png')).convert('RGBA')
        image.thumbnail((128, 128), Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', (136, 136))
        canvas.alpha_composite(image, ((136-image.width)//2, (136-image.height)//2))
        canvas.save(ROOT / ('assets/ui/art/building-'+name+'.png'))
    config = dict(schema=1, page_size=2048, padding=3, max_pages=8, images=records)
    (WORK / 'atlas.json').write_text(json.dumps(config, indent=2), encoding='utf8')
    export = WORK / ('export-' + str(len(list(WORK.glob('export-*')))+1).zfill(3))
    subprocess.run([str(ROOT / 'build/engine-windows/AtlasTool.exe'), str(WORK / 'atlas.json'), str(export)], check=True)
    packed = json.loads((export / 'manifest.json').read_text())
    world_path = ROOT / 'assets/art/world.json'
    world = json.loads(world_path.read_text())
    ids = {record['id'] for record in records}
    world['pages'] = [p for p in world['pages'] if not p['path'].startswith('art/buildings-014-')]
    for page in world['pages']:
        page['sprites'] = [s for s in page['sprites'] if s['id'] not in ids]
    for i, page in enumerate(packed['pages']):
        filename = 'buildings-014-' + str(i) + '.png'
        shutil.copyfile(export / page['path'], ROOT / 'assets/art' / filename)
        entries = [dict(id=e['id'], rect=e['rect'], pivot=e['pivot']) for e in packed['entries'] if e['page'] == i]
        world['pages'].append(dict(path='art/' + filename, sprites=entries))
    world_path.write_text(json.dumps(world, indent=2)+'\n', encoding='utf8')
    print(json.dumps(dict(buildings=len(plates), frames=len(records), icons=13, pages=len(packed['pages']))))

if __name__ == '__main__':
    main()
