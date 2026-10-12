"""Prepare derived runtime copies of the approved generated art; never edit sources.

Run with Pillow and numpy. AtlasTool supplies deterministic packing/extrusion.
"""
import json
import shutil
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'art-source/production-v1'
WORK = ROOT / 'art-source/runtime-0.11.1'
DEST = ROOT / 'assets/art'
UI = ROOT / 'assets/ui/art'
STATES = ['stage1', 'stage2', 'stage3', 'ready', 'winter']
GROUPS = {
    'buildings-core': (5, [0, 286, 568, 850, 1122], ['house', 'camp', 'farm', 'stockpile']),
    'buildings-civic': (5, [0, 260, 510, 772, 1122], ['tavern', 'barracks', 'temple', 'townhall']),
    'buildings-production': (5, [0, 318, 622, 971], ['lumbermill', 'blacksmith', 'mine']),
    'units': (6, [0, 248, 488, 739, 1024], None),
    'nature': (4, [0, 344, 695, 942, 1254], None),
    'rocks-mountains': (4, [0, 310, 613, 916, 1254], None),
    'terrain-materials': (4, [0, 314, 627, 941, 1254], None),
    'ui-kit': (4, [0, 310, 606, 882, 1254], None),
    'icons-cursors': (6, [0, 270, 505, 725, 939, 1145], None),
    'fog-masks': (4, [0, 314, 627, 941, 1254], None),
}


def matte_to_alpha(rgb):
    p = np.asarray(rgb.convert('RGB')).astype(np.float32)
    # Sample the actual source background, not an assumed exact generator color.
    bg = np.median(np.concatenate([p[:4].reshape(-1, 3), p[-4:].reshape(-1, 3)]), axis=0)
    dist = np.max(np.abs(p-bg), axis=2)
    chroma = np.abs((p[:,:,2]-p[:,:,0])-(bg[2]-bg[0]))
    possible = (dist < 30) & (chroma < 10)
    flood = ImageOps.expand(Image.fromarray((possible*255).astype('uint8')), border=1, fill=255)
    ImageDraw.floodfill(flood, (0,0), 128)
    outer = np.asarray(flood)[1:-1,1:-1] == 128
    # Exact-matte enclosed gaps include scaffolds, gear centers and foliage gaps.
    keyed = outer | (dist < 4)
    alpha = np.where(keyed, np.clip((dist-4)/18, 0, 1), 1)
    # Unmix the navy matte on edge pixels before interpolation over terrain.
    straight = (p - bg*(1-alpha[..., None])) / np.maximum(alpha[..., None], .01)
    rgba = np.concatenate([np.clip(straight, 0, 255), (alpha*255)[..., None]], axis=2).astype('uint8')
    rgba[alpha == 0, :3] = 0
    return Image.fromarray(rgba)


def remove_boundary_fragments(cell):
    # Remove only tiny disconnected pieces at cell boundaries, never main details.
    mask = cell.getchannel('A').point(lambda v: 255 if v > 80 else 0)
    remaining = mask.copy()
    while (box := remaining.getbbox()) is not None:
        a = np.asarray(remaining)
        y, x = np.argwhere(a > 0)[0]
        component = remaining.copy()
        ImageDraw.floodfill(component, (int(x), int(y)), 128)
        pixels = np.asarray(component) == 128
        data = np.array(remaining); data[pixels] = 0
        remaining = Image.fromarray(data)
        ys, xs = np.where(pixels)
        boundary = xs.min() < 3 or ys.min() < 3 or xs.max() >= cell.width-3 or ys.max() >= cell.height-3
        if boundary and len(xs) < 100:
            rgba = np.array(cell); rgba[pixels, 3] = 0; cell = Image.fromarray(rgba)
    return cell


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    (WORK / 'sprites').mkdir(exist_ok=True)
    DEST.mkdir(parents=True, exist_ok=True)
    UI.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'licenses/Ribterra-Assets.txt', WORK / 'NOTICE.txt')
    records, review = [], []
    for name, (cols, ys, names) in GROUPS.items():
        source = Image.open(SOURCE / (name+'.png')).convert('RGB')
        opaque = name in ('terrain-materials', 'fog-masks')
        processed = source.convert('RGBA') if opaque else matte_to_alpha(source)
        for row in range(len(ys)-1):
            cells = []
            for col in range(cols):
                x0, x1 = round(col*source.width/cols), round((col+1)*source.width/cols)
                cell = processed.crop((x0, ys[row], x1, ys[row+1]))
                if names:
                    ident = 'building.'+names[row]+'.'+STATES[col]
                else:
                    ident = name+'.'+str(row*cols+col)
                if opaque:
                    # Stay inside each swatch; avoid a neighbouring material at its edge.
                    cell = cell.crop((2,2,cell.width-2,cell.height-2))
                else:
                    cell = remove_boundary_fragments(cell)
                    box = cell.getchannel('A').point(lambda v: 255 if v>80 else 0).getbbox()
                    if not box: raise ValueError('Empty sprite '+ident)
                    box = (max(0,box[0]-2),max(0,box[1]-2),min(cell.width,box[2]+2),min(cell.height,box[3]+2))
                    cell = cell.crop(box)
                cells.append((ident,cell))
            # Building stages keep a common scale/canvas and bottom anchor.
            common_w = max(cell.width for _,cell in cells)
            common_h = max(cell.height for _,cell in cells)
            for ident, cell in cells:
                if names:
                    canvas=Image.new('RGBA',(common_w+8,common_h+8))
                    canvas.alpha_composite(cell,((canvas.width-cell.width)//2,canvas.height-cell.height-4))
                    cell=canvas
                elif not opaque:
                    canvas=Image.new('RGBA',(cell.width+8,cell.height+8))
                    canvas.alpha_composite(cell,(4,4)); cell=canvas
                if name=='terrain-materials':
                    # Blend a narrow seam; preserve the original interior without mirroring.
                    p=np.array(cell.resize((256,256),Image.Resampling.LANCZOS)).astype(float)
                    for axis in (0,1):
                        p=np.swapaxes(p,0,axis)
                        for k in range(12):
                            mix=.5*(1-k/12)
                            a,b=p[k].copy(),p[-1-k].copy()
                            p[k]=a*(1-mix)+b*mix; p[-1-k]=b*(1-mix)+a*mix
                        p=np.swapaxes(p,0,axis)
                    cell=Image.fromarray(p.astype('uint8'))
                pivot=[.5, 1-4/cell.height] if not opaque else [.5,.5]
                if name=='icons-cursors' and int(ident.split('.')[-1])>=24:
                    pivot=[4/cell.width,4/cell.height]
                if name=='fog-masks':
                    value=np.asarray(cell.convert('L'))
                    rgba=np.zeros((cell.height,cell.width,4),dtype='uint8')
                    rgba[:,:,3]=value
                    cell=Image.fromarray(rgba)
                path='sprites/'+ident+'.png'
                cell.save(WORK/path)
                records.append(dict(id=ident,path=path,pivot=pivot,license='LicenseRef-Proprietary',source='project',author='Ribterra project',notice='NOTICE.txt'))
                if name in ('ui-kit','icons-cursors'):
                    cell.save(UI/(ident+'.png'))
                review.append((ident,cell.copy()))
    config=dict(schema=1,page_size=2048,padding=3,max_pages=12,images=records)
    (WORK/'atlas.json').write_text(json.dumps(config,indent=2),encoding='utf8')
    # AtlasTool deliberately requires a fresh destination. Preserve earlier exports.
    exports=sorted(WORK.glob('export-*'))
    export=WORK/('export-'+str(len(exports)+1).zfill(3))
    subprocess.run([str(ROOT/'build/engine-windows/AtlasTool.exe'),str(WORK/'atlas.json'),str(export)],check=True)
    packed=json.loads((export/'manifest.json').read_text())
    pages=[]
    for index,page in enumerate(packed['pages']):
        shutil.copyfile(export/page['path'],DEST/page['path'])
        entries=[dict(id=e['id'],rect=e['rect'],pivot=e['pivot']) for e in packed['entries'] if e['page']==index]
        pages.append(dict(path='art/'+page['path'],sprites=entries))
    (DEST/'world.json').write_text(json.dumps(dict(schema=1,pages=pages),indent=2),encoding='utf8')
    theme=[]
    for index in (0,1,2,3,4,5):
        size=Image.open(UI/('ui-kit.'+str(index)+'.png')).size
        w,h=size
        theme.append(f'@spritesheet art{index} {{ src: ui-kit.{index}.png; outer{index}: 0px 0px {w}px {h}px; inner{index}: 24px 24px {w-48}px {h-48}px; }}')
    theme.extend([
        '.card { background: transparent; border-color: transparent; decorator: ninepatch(outer4, inner4, 16dp) border-box; }',
        'button { background: transparent; border-color: transparent; decorator: ninepatch(outer0, inner0, 9dp) border-box; }',
        'button:hover { background: transparent; border-color: transparent; decorator: ninepatch(outer1, inner1, 9dp) border-box; }',
        'button:focus, button:active, button.active { background: transparent; border-color: transparent; decorator: ninepatch(outer2, inner2, 9dp) border-box; }',
        'button:disabled { background: transparent; border-color: transparent; decorator: ninepatch(outer3, inner3, 9dp) border-box; }',
        '.resource-icon { width: 26dp; height: 26dp; vertical-align: middle; margin-right: 5dp; }',
    ])
    (UI/'theme.rcss').write_text('\n'.join(theme)+'\n',encoding='utf8')
    # Contact sheets are QA artifacts only and never shipped as gameplay imagery.
    for start in range(0,len(review),40):
        subset=review[start:start+40]
        sheet=Image.new('RGB',(1200,((len(subset)+7)//8)*174),(70,90,64))
        draw=ImageDraw.Draw(sheet)
        for i,(ident,cell) in enumerate(subset):
            cell.thumbnail((138,145),Image.Resampling.LANCZOS)
            x=(i%8)*150; y=(i//8)*174
            sheet.paste(cell,(x+(150-cell.width)//2,y+145-cell.height),cell)
            draw.text((x+2,y+148),ident,fill='white')
        sheet.save(WORK/('review-'+str(start//40)+'.png'))
    print(json.dumps(dict(sprites=len(records),pages=len(pages),output=str(DEST))))


if __name__=='__main__': main()
