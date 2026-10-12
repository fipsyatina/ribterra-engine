"""Extract generated RGBA sprites and pack a new atlas; preserve source plates."""
import json
import shutil
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'art-source/production-v4'
WORK = ROOT / 'art-source/runtime-0.18.0'
WORK.mkdir(parents=True, exist_ok=True)
(WORK / 'sprites').mkdir(exist_ok=True)
shutil.copyfile(ROOT / 'licenses/Ribterra-Assets.txt', WORK / 'NOTICE.txt')
records = []

def record(ident, image, pivot):
    relative = 'sprites/' + ident + '.png'
    image.save(WORK / relative)
    records.append(dict(id=ident, path=relative, pivot=pivot,
                        license='LicenseRef-Proprietary', source='project',
                        author='Ribterra project', notice='NOTICE.txt'))

for sheet, offset in [('walk-blue.png', 0), ('walk-earth.png', 2)]:
    plate = Image.open(SOURCE / sheet)
    assert plate.mode == 'RGBA' and plate.getchannel('A').getextrema()[0] == 0
    for row in range(4):
        cells = []
        for col in range(4):
            cell = plate.crop((round(col*plate.width/4), round(row*plate.height/4),
                               round((col+1)*plate.width/4), round((row+1)*plate.height/4)))
            box = cell.getchannel('A').point(lambda a: 255 if a > 48 else 0).getbbox()
            assert box
            cells.append(cell.crop(box))
        # One scale per direction, common foot anchor and canvas prevent frame-size jitter.
        scale = min(140 / max(c.height for c in cells), 88 / max(c.width for c in cells))
        for col, cell in enumerate(cells):
            cell = cell.resize((round(cell.width*scale), round(cell.height*scale)), Image.Resampling.LANCZOS)
            canvas = Image.new('RGBA', (96, 160))
            canvas.alpha_composite(cell, ((96-cell.width)//2, 154-cell.height))
            record(f'walk.{offset+row//2}.{row%2}.{col}', canvas, [.5, 154/160])

plate = Image.open(SOURCE / 'trees.png')
assert plate.mode == 'RGBA' and plate.getchannel('A').getextrema()[0] == 0
xs = [0, 385, 633, 926, 1254]
ys = [0, 416, 813, 1254]
for row in range(3):
    for col in range(4):
        cell = plate.crop((xs[col], ys[row], xs[col+1], ys[row+1]))
        box = cell.getchannel('A').point(lambda a: 255 if a > 64 else 0).getbbox()
        assert box
        cell = cell.crop(box)
        cell.thumbnail((256, 300), Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', (cell.width+8, cell.height+8))
        canvas.alpha_composite(cell, (4, 4))
        record(f'trees.018.{row*4+col}', canvas, [.5, (canvas.height-4)/canvas.height])

plate = Image.open(SOURCE / 'grass.png').convert('RGBA')
for row in range(2):
    for col in range(3):
        cell = plate.crop((col*512+3, row*512+3, (col+1)*512-3, (row+1)*512-3))
        cell = cell.resize((256, 256), Image.Resampling.LANCZOS)
        record(f'grass.018.{row*3+col}', cell, [.5, .5])

(WORK / 'atlas.json').write_text(json.dumps(dict(schema=1, page_size=2048, padding=3,
                                               max_pages=4, images=records), indent=2))
export = WORK / ('export-' + str(len(list(WORK.glob('export-*')))+1).zfill(3))
subprocess.run([str(ROOT/'build/engine-windows/AtlasTool.exe'), str(WORK/'atlas.json'), str(export)], check=True)
packed = json.loads((export/'manifest.json').read_text())
world_path = ROOT/'assets/art/world.json'
world = json.loads(world_path.read_text())
world['pages'] = [p for p in world['pages'] if not p['path'].startswith('art/living-018-')]
for i, page in enumerate(packed['pages']):
    filename = f'living-018-{i}.png'
    shutil.copyfile(export/page['path'], ROOT/'assets/art'/filename)
    entries = [dict(id=e['id'], rect=e['rect'], pivot=e['pivot']) for e in packed['entries'] if e['page'] == i]
    world['pages'].append(dict(path='art/'+filename, sprites=entries))
world_path.write_text(json.dumps(world, indent=2)+'\n')
print(f'Packed {len(records)} sprites into {len(packed["pages"])} new pages')
