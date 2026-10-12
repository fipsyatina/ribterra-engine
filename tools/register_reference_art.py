"""Register generated plates without resampling or editing source image pixels."""
from pathlib import Path
import json, shutil, hashlib
from PIL import Image
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT/'art-source/reference-refresh'
records=[]
for f in ['sources-buildings.json','sources-environment.json','sources-units.json']:
    if (WORK/f).exists(): records += json.loads((WORK/f).read_text(encoding='utf-8-sig'))
world=json.loads((WORK/'world-before.json').read_text(encoding='utf-8'))
newpages=[]; replacement=set(); report=[]
for item in records:
    ident=item['id']; src=Path(item['source'])
    original=WORK/'sources'/(ident+'.png')
    if not src.exists():src=original
    if src.resolve()!=original.resolve():shutil.copy2(src,original)
    name='refresh-'+ident+'.png';shutil.copy2(src,ROOT/'assets/art'/name)
    im=Image.open(src);im.load();w,h=im.size
    cols=item.get('cols',3);rows=item.get('rows',2);mode=item.get('mode','magenta')
    page={'path':'art/'+name,'sprites':[]}
    if mode:page['alpha_mode']=mode
    if ident=='trees':page['foliage_despill']=True
    boxes=[]
    pixels=np.asarray(im.convert('RGB')).astype(np.int16)
    for row in range(rows):
        for col in range(cols):
            x0,x1=round(col*w/cols),round((col+1)*w/cols)
            y0,y1=round(row*h/rows),round((row+1)*h/rows)
            cw,ch=x1-x0,y1-y0
            cell=pixels[y0:y1,x0:x1]
            if mode=='magenta':
                mask=(np.minimum(cell[:,:,0],cell[:,:,2])-cell[:,:,1])<140
                yy,xx=np.where(mask)
                assert len(xx)>20,(ident,row,col,'empty')
                # Read-only rectangle detection; original raster remains byte-identical.
                bbox=[int(xx.min()),int(yy.min()),int(xx.max())+1,int(yy.max())+1]
            else:bbox=[3,3,cw-3,ch-3]
            boxes.append((x0,y0,cw,ch,bbox))
    for index,(x0,y0,cw,ch,bbox) in enumerate(boxes):
        row,col=divmod(index,cols)
        if ident=='materials':ids=['terrain-materials.'+str(index)]
        elif ident=='grass':ids=['grass.018.'+str(index)]
        elif ident=='trees':ids=['trees.018.'+str(index)]
        elif ident=='rocks':ids=['rocks-mountains.'+str(index)]
        elif ident=='detail':ids=['detail.'+str(index)]+(['nature.8'] if index==4 else ['nature.9'] if index==5 else [])
        elif ident=='effects':ids=['fx.'+str(index)]
        elif ident.startswith('walk-'):
            offset=0 if ident=='walk-blue' else 2
            ids=[f'walk.{offset+row//2}.{row%2}.{col}']
        elif ident.startswith('animals-'):
            species=['deer','hare','boar','wolf'][row]
            ids=[f'animal.{species}.{ident.split("-")[1]}{col}']
        else:ids=['building.'+ident+'.'+['stage1','stage2','stage3','ready','winter','level2'][index]]
        if mode=='magenta':
            if ident.startswith('walk-'):
                # One common crop width/height per actor direction; separate foot anchors.
                group=boxes[row*cols:(row+1)*cols]
                rw=max(v[4][2]-v[4][0] for v in group)+8
                rh=max(v[4][3]-v[4][1] for v in group)+8
                cx=(bbox[0]+bbox[2])//2
                left=max(0,min(cw-rw,cx-rw//2));top=max(0,min(ch-rh,bbox[3]-rh+4))
                rect=[x0+left,y0+top,rw,rh]
                pivot=[(cx-left)/rw,(bbox[3]-top)/rh]
            else:
                left,top,right,bottom=bbox
                rect=[x0+max(0,left-3),y0+max(0,top-3),min(cw,right+3)-max(0,left-3),min(ch,bottom+3)-max(0,top-3)]
                pivot=[.5,1-3/rect[3]]
        else:
            rect=[x0+3,y0+3,cw-6,ch-6];pivot=[.5,.5]
        for sprite_id in ids:
            assert rect[0]>=0 and rect[1]>=0 and rect[0]+rect[2]<=w and rect[1]+rect[3]<=h,(ident,sprite_id,rect)
            replacement.add(sprite_id)
            page['sprites'].append({'id':sprite_id,'rect':rect,'pivot':pivot})
    newpages.append(page)
    report.append({'id':ident,'file':'assets/art/'+name,'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'width':w,'height':h,'sprites':len(page['sprites']),'alpha_mode':mode})
for page in world['pages']:page['sprites']=[s for s in page['sprites'] if s['id'] not in replacement]
world['pages']=[p for p in world['pages'] if p['sprites']]+newpages
allids=[s['id'] for p in world['pages'] for s in p['sprites']]
assert len(allids)==len(set(allids))
(ROOT/'assets/art/world.json').write_text(json.dumps(world,indent=2)+'\n',encoding='utf-8')
(WORK/'integration-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
catalog_path=ROOT/'content/asset_sources.json'
catalog=json.loads(catalog_path.read_text(encoding='utf-8-sig'))
for item in report:
    catalog['overrides'][item['file'].removeprefix('assets/')]={
        'id':'art.reference-refresh.'+item['id'],'license':'LicenseRef-Proprietary',
        'author':'Ribterra project / OpenAI image generation','source':'project',
        'notice':'licenses/Ribterra-Assets.txt'}
catalog_path.write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8')
print('Registered',len(replacement),'sprites in',len(newpages),'new plates; preserved remaining IDs.')
