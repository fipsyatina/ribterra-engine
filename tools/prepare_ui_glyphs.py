"""Trim and resize the approved transparent gold UI glyph sheet."""
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
plate=Image.open(ROOT/'art-source/production-v3/gold-glyphs.png').convert('RGBA')
assert plate.getpixel((0,0))[3]==0
for i in range(14):
    x,y=i%7,i//7
    cell=plate.crop((round(x*plate.width/7),round(y*plate.height/2),round((x+1)*plate.width/7),round((y+1)*plate.height/2)))
    bbox=cell.getchannel('A').point(lambda a:255 if a>32 else 0).getbbox()
    assert bbox
    cell=cell.crop(bbox)
    cell.thumbnail((80,80),Image.Resampling.LANCZOS)
    canvas=Image.new('RGBA',(88,88))
    canvas.alpha_composite(cell,((88-cell.width)//2,(88-cell.height)//2))
    canvas.save(ROOT/f'assets/ui/art/glyph-{i}.png')
