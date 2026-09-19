from pathlib import Path
import json
import numpy as np
from PIL import Image, ImageDraw, ImageFont

root=Path(__file__).resolve().parent
manifest=json.loads((root/'GeometryManifest.json').read_text(encoding='utf-8'))
assets=manifest['assets']

def read_obj(path):
    v=[];f=[]
    for l in path.read_text().splitlines():
        if l.startswith('v '):v.append(list(map(float,l.split()[1:])))
        elif l.startswith('f '):f.append([int(t.split('/')[0])-1 for t in l.split()[1:]])
    return np.array(v),np.array(f)

try:
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',21)
    small=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',16)
except OSError:font=small=ImageFont.load_default()

W,H=440,350
sheet=Image.new('RGB',(W*4,H*4+64),(238,232,218))
draw=ImageDraw.Draw(sheet)
draw.text((20,12),'DoughWorld geometry source previews | centimeter OBJ | not UE runtime captures',font=font,fill=(46,39,31))
jobs=[(a,np.array([1.1,-1.6,1.0]),None) for a in assets]
jobs.append((assets[1],np.array([1.1,-1.6,-1.25]),'Mushroom cap / underside pleats'))
jobs.append((assets[4],np.array([.1,-.1,2.]),'Pore ring / hollow top view'))

checks=[]
for i,(asset,eye,label) in enumerate(jobs):
    v,f=read_obj(Path(asset['obj_path']))
    assert len(v)==asset['validation']['vertex_count']
    assert len(f)==asset['validation']['triangle_count']
    bounds=v.max(0)-v.min(0)
    assert np.max(abs(bounds-np.array(asset['dimensions_cm'])))<1e-5
    eye=eye/np.linalg.norm(eye)
    right=np.cross(eye,[0,0,1]);right/=np.linalg.norm(right)
    up=np.cross(right,eye)
    center=(v.min(0)+v.max(0))/2
    p=v-center
    screen=np.stack([p@right,p@up],axis=1)
    scale=min((W-70)/(screen[:,0].max()-screen[:,0].min()),(H-95)/(screen[:,1].max()-screen[:,1].min()))
    screen*=scale
    x0=(i%4)*W;y0=(i//4)*H+64
    screen[:,0]+=x0+W/2
    screen[:,1]=y0+(H-65)/2-screen[:,1]
    draw.rounded_rectangle((x0+9,y0+6,x0+W-9,y0+H-8),radius=12,fill=(250,247,239),outline=(221,211,190),width=1)
    triangles=v[f]
    normals=np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0]);normals/=np.linalg.norm(normals,axis=1)[:,None]
    depths=triangles.mean(1)@eye
    light=np.array([-.4,-.5,1.]);light/=np.linalg.norm(light)
    if eye[2]<0:light=-light
    base=np.array([206,161,85])
    if 'Mushroom' in asset['key']:base=np.array([183,140,178]) if 'Cap' in asset['key'] else np.array([230,212,166])
    if 'Pore' in asset['key']:base=np.array([181,142,92])
    if 'Gluten' in asset['key']:base=np.array([198,175,78])
    for j in np.argsort(depths):
        if normals[j]@eye<=0:continue
        shade=.5+.5*max(0,float(normals[j]@light))
        color=tuple(np.clip(base*shade,0,255).astype(int))
        draw.polygon([tuple(q) for q in screen[f[j]]],fill=color)
    name=label or asset['key'].removeprefix('SM_DW_')
    draw.text((x0+20,y0+H-65),name,font=font,fill=(48,40,32))
    draw.text((x0+20,y0+H-34),' x '.join(str(d) for d in asset['dimensions_cm'])+' cm   |   '+str(len(f))+' tris',font=small,fill=(101,83,61))
    checks.append({'key':asset['key'],'obj_reopened':True,'vertex_triangle_count_match':True,'bounds_match':True})

sheet.save(root/'GeometryPreview.png')
(root/'OBJSerializationChecks.json').write_text(json.dumps(checks[:14],indent=2),encoding='utf-8')
print(root/'GeometryPreview.png')
