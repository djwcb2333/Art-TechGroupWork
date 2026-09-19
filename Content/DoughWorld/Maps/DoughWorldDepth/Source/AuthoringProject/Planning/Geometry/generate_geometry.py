"""Deterministic closed low-poly dough meshes. Output positions are UE centimeters.

Requires numpy only. OBJ imports should keep uniform import scale 1.0.
GeometryManifest.json records exact bounds, pivot, topology and module recipes.
"""
from pathlib import Path
from collections import defaultdict, deque
import json, math, hashlib
import numpy as np

ROOT=Path(__file__).resolve().parent
OBJ=ROOT/'OBJ'
OBJ.mkdir(parents=True,exist_ok=True)
TAU=math.tau

def sgnpow(v,p):
    return math.copysign(abs(v)**p,v)

def revolve(profile, segments=40, xy_power=1.0, deform=None):
    """Closed revolution/superellipse. Profile ordered from bottom to top.
    Each (radius,z) endpoint can collapse to a single pole.
    """
    verts=[]; rings=[]; faces=[]
    for k,(radius,z) in enumerate(profile):
        if abs(radius)<1e-10:
            p=np.array([0.,0.,float(z)])
            if deform is not None: p=deform(p,0.,k,len(profile),radius,z)
            rings.append([len(verts)]); verts.append(p)
        else:
            ids=[]
            for j in range(segments):
                a=TAU*j/segments
                p=np.array([radius*sgnpow(math.cos(a),xy_power),radius*sgnpow(math.sin(a),xy_power),z],dtype=float)
                if deform is not None: p=deform(p,a,k,len(profile),radius,z)
                ids.append(len(verts)); verts.append(p)
            rings.append(ids)
    for a,b in zip(rings[:-1],rings[1:]):
        if len(a)==1 and len(b)==1: raise ValueError('Adjacent poles')
        for j in range(segments):
            q=(j+1)%segments
            if len(a)==1: faces.append([a[0],b[j],b[q]])
            elif len(b)==1: faces.append([a[j],b[0],a[q]])
            else: faces.extend([[a[j],b[j],b[q]],[a[j],b[q],a[q]]])
    return np.array(verts,float),np.array(faces,int)

def round_brick():
    profile=[(0,0),(.73,0),(.92,.05),(1,.18),(1.02,.43),(.99,.70),(.91,.88),(.66,.98),(0,1)]
    def warp(p,a,k,n,r,z):
        if r: p[:2]*=1+.016*math.sin(5*a+.8)*math.sin(math.pi*z)
        p[2]+=.025*math.cos(2*a)*math.sin(math.pi*z)
        return p
    return revolve(profile,40,.43,warp)

def mushroom_cap():
    # Underside pleats are actual alternating ridges, not texture-only gills.
    profile=[(0,.10),(.15,0),(.36,.06),(.66,.18),(.89,.33),(1,.48),(.99,.57),(.95,.76),(.81,1.02),(.61,1.27),(.34,1.45),(0,1.52)]
    def warp(p,a,k,n,r,z):
        if r:
            p[:2]*=1+.026*math.sin(12*a)+.018*math.sin(5*a+.2)
            if k<=5: p[2]+=(.018+.045*r)*math.cos(12*a)
            elif k<8: p[2]+=.035*r*math.cos(12*a)
            else: p[2]+=.018*r*math.sin(5*a)
        return p
    return revolve(profile,72,1.,warp)

def mushroom_stem():
    profile=[(0,0),(1,0),(.9,.035),(.69,.10),(.58,.28),(.54,.52),(.59,.76),(.67,.93),(.60,1),(0,1)]
    def warp(p,a,k,n,r,z):
        p[0]+=.13*math.sin(math.pi*z)
        p[1]+=.07*math.sin(TAU*z)
        p[:2]+=r*.024*math.sin(9*a)*np.array([math.cos(a),math.sin(a)])
        return p
    return revolve(profile,32,1.,warp)

def bud():
    profile=[(0,0),(.48,0),(.77,.09),(1,.34),(.91,.63),(.62,.86),(.28,.98),(0,1.04)]
    def warp(p,a,k,n,r,z):
        p[:2]*=1+.055*math.cos(5*a)*math.sin(math.pi*min(1,z))
        return p
    return revolve(profile,30,1.,warp)

def gluten_column():
    profile=[(0,0),(.7,0),(1,.05),(.61,.16),(.4,.38),(.31,.61),(.18,.85),(.075,.97),(0,1)]
    def warp(p,a,k,n,r,z):
        p[0]+=.8*z*z
        p[1]+=.17*math.sin(math.pi*z)
        p[:2]+=r*.05*math.cos(7*a)*np.array([math.cos(a),math.sin(a)])
        return p
    return revolve(profile,24,1.,warp)

def annulus(arc=TAU,segments=64,thin=False):
    cross=[(1.,0),(1.02,.15),(1.,.77),(.96,1),(.66,.94),(.62,.73),(.64,.10),(.7,0)]
    if thin: cross=[(1,0),(1,.98),(.97,1),(.86,.98),(.84,.02),(.86,0)]
    closed=abs(arc-TAU)<1e-7
    angles=segments if closed else segments+1
    verts=[]; faces=[]; count=len(cross)
    for i in range(angles):
        a=arc*i/segments
        for r,z in cross:
            rr=r*(1+.025*math.sin(5*a)+.018*math.sin(9*a+.4))
            zz=z*(1+.045*math.sin(3*a)+.025*math.sin(7*a))
            verts.append([rr*math.cos(a),rr*math.sin(a),zz])
    for i in range(segments):
        nxt=(i+1)%angles
        for j in range(count):
            q=(j+1)%count
            a=i*count+j;b=nxt*count+j;c=nxt*count+q;d=i*count+q
            faces.extend([[a,b,c],[a,c,d]])
    if not closed:
        # Cross section is slightly concave; triangulate end polygon by ear clipping.
        def earclip(poly):
            ids=list(range(len(poly))); result=[]
            area=sum(poly[i][0]*poly[(i+1)%len(poly)][1]-poly[(i+1)%len(poly)][0]*poly[i][1] for i in range(len(poly)))
            if area<0: ids.reverse()
            cross2=lambda a,b,c:(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
            while len(ids)>3:
                done=False
                for k in range(len(ids)):
                    a,b,c=ids[k-1],ids[k],ids[(k+1)%len(ids)]
                    if cross2(poly[a],poly[b],poly[c])<=1e-10: continue
                    if any(cross2(poly[a],poly[b],poly[p])>=-1e-10 and cross2(poly[b],poly[c],poly[p])>=-1e-10 and cross2(poly[c],poly[a],poly[p])>=-1e-10 for p in ids if p not in [a,b,c]): continue
                    result.append([a,b,c]); del ids[k];done=True;break
                if not done: raise ValueError('Ear clipping failed')
            result.append(ids)
            return result
        caps=earclip(cross)
        for tri in caps:
            faces.append(tri)
            faces.append([(angles-1)*count+j for j in tri])
    return np.array(verts,float),np.array(faces,int)

def arched_roof(segments=32, tile=False):
    # Barrel shell: open space beneath the curved shell, with closed solid end rims.
    # x spans width, y length, z rise. Both outer/inner arches meet finite foot thickness.
    outer=[];inner=[]
    for i in range(segments+1):
        a=math.pi*i/segments
        outer.append([math.cos(a),math.sin(a)])
        inner.append([.94*math.cos(a),.80*math.sin(a)])
    verts=[]
    for y in [-1.,1.]:
        for points in [outer,inner]:
            for x,z in points: verts.append([x,y,z])
    n=segments+1
    ix=lambda end,side,i:end*2*n+side*n+i
    faces=[]
    def quad(a,b,c,d): faces.extend([[a,b,c],[a,c,d]])
    for i in range(segments):
        quad(ix(0,0,i),ix(1,0,i),ix(1,0,i+1),ix(0,0,i+1))
        quad(ix(0,1,i),ix(1,1,i),ix(1,1,i+1),ix(0,1,i+1))
        for e in [0,1]: quad(ix(e,0,i),ix(e,0,i+1),ix(e,1,i+1),ix(e,1,i))
    for i in [0,segments]: quad(ix(0,0,i),ix(0,1,i),ix(1,1,i),ix(1,0,i))
    return np.array(verts,float),np.array(faces,int)

def support_post():
    return revolve([(0,0),(1,0),(1,.035),(.76,.055),(.68,.92),(.86,.94),(.86,1),(0,1)],12,.86)

def altar_dish():
    # Multiple actual terraces and a recessed bowl. This is not a flat disk.
    return revolve([(0,0),(.88,0),(1,.13),(1,.32),(.91,.42),(.91,.62),(.81,.72),(.77,1),(.59,1),(.51,.72),(.32,.62),(0,.62)],64,1.)

def fit_bottom_pivot(v,size_cm):
    lo=v.min(axis=0); hi=v.max(axis=0)
    scale=np.array(size_cm)/(hi-lo)
    # Center X/Y at bbox midpoint, Z exactly on lowest physical vertex.
    pivot=np.array([(lo[0]+hi[0])/2,(lo[1]+hi[1])/2,lo[2]])
    fitted=(v-pivot)*scale
    return fitted, {'source_pivot':pivot.tolist(),'scale_to_cm':scale.tolist(),
                    'original_origin_in_output_cm':(-pivot*scale).tolist()}

def orient_and_validate(v,f):
    edges=defaultdict(list)
    for ti,tri in enumerate(f):
        for a,b in zip(tri,np.roll(tri,-1)):
            edges[tuple(sorted((int(a),int(b))))].append((ti,int(a),int(b)))
    invalid={k:len(e) for k,e in edges.items() if len(e)!=2}
    if invalid: raise ValueError(f'Nonmanifold/boundary edges: {list(invalid.items())[:10]}')
    adjacency=defaultdict(list)
    for items in edges.values():
        a,b=items
        same=(a[1]==b[1])
        adjacency[a[0]].append((b[0],same));adjacency[b[0]].append((a[0],same))
    flips={};components=0
    for start in range(len(f)):
        if start in flips:continue
        components+=1;flips[start]=False;q=deque([start])
        while q:
            a=q.popleft()
            for b,same in adjacency[a]:
                expected=bool(flips[a]^same)
                if b in flips:
                    if flips[b]!=expected:raise ValueError('Nonorientable mesh')
                else:flips[b]=expected;q.append(b)
    f=f.copy()
    for i,flip in flips.items():
        if flip:f[i]=f[i,[0,2,1]]
    tri=v[f];norm=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0])
    areas=np.linalg.norm(norm,axis=1)/2
    if (areas<1e-7).any():raise ValueError('Degenerate triangle')
    volume=float(np.einsum('ij,ij->i',tri[:,0],np.cross(tri[:,1],tri[:,2])).sum()/6)
    if volume<0:
        f=f[:,[0,2,1]];norm=-norm;volume=-volume
    if volume<1e-5:raise ValueError('Nonpositive closed volume')
    if not np.isfinite(v).all() or not np.isfinite(norm).all():raise ValueError('Nonfinite geometry')
    vertex_normals=np.zeros_like(v)
    for i in range(3):np.add.at(vertex_normals,f[:,i],norm)
    lengths=np.linalg.norm(vertex_normals,axis=1)
    if (lengths<1e-8).any():raise ValueError('Undefined vertex normal')
    vertex_normals/=lengths[:,None]
    diagnostics={'vertex_count':len(v),'triangle_count':len(f),'edge_count':len(edges),'boundary_edges':0,'nonmanifold_edges':0,
                 'degenerate_triangles':0,'winding_consistent':True,'watertight':True,'connected_components':components,
                 'signed_volume_cm3':round(volume,4),'smallest_triangle_area_cm2':round(float(areas.min()),7),
                 'euler_characteristic':int(len(v)-len(edges)+len(f)),'self_intersection_test':'not performed; deterministic radial/shell construction, do not claim general self-intersection verification'}
    return f,vertex_normals,diagnostics

def vertical_axis_hits(v,f,xy):
    count=0
    for tri in v[f]:
        a,b,c=tri[:,:2];d0=b-a;d1=c-a;p=np.array(xy)-a
        den=d0[0]*d1[1]-d0[1]*d1[0]
        if abs(den)<1e-8:continue
        s=(p[0]*d1[1]-p[1]*d1[0])/den;t=(d0[0]*p[1]-d0[1]*p[0])/den
        if s>=-1e-8 and t>=-1e-8 and s+t<=1+1e-8:count+=1
    return count

def write_obj(path,v,f,norm):
    lines=['# DoughWorld V3 generated geometry. Units: centimeters. Import scale: 1.0.',
           '# Pivot: bottom center of physical bounds; +Z up; +X width; +Y depth.',
           '# UV0 is non-atlased dominant-face projection at 100 cm per UV unit.',
           'mtllib DoughWorld_Candidate.mtl', 'o '+path.stem]
    for p in v:lines.append('v %.7f %.7f %.7f'%tuple(p))
    for n in norm:lines.append('vn %.8f %.8f %.8f'%tuple(n))
    uv=[]
    for tri in v[f]:
        normal=np.cross(tri[1]-tri[0],tri[2]-tri[0]);dominant=int(np.argmax(abs(normal)))
        axes=[j for j in range(3) if j!=dominant]
        for point in tri:uv.append(point[axes]/100.)
    for t in uv:lines.append('vt %.7f %.7f'%tuple(t))
    lines+=['s 1','usemtl DoughWorld_Candidate']
    for i,tri in enumerate(f):lines.append('f '+' '.join(f'{int(idx)+1}/{3*i+j+1}/{int(idx)+1}' for j,idx in enumerate(tri)))
    path.write_text('\n'.join(lines)+'\n',encoding='ascii')

specs=[
 ('SM_DW_BreadBrick',round_brick,[240,140,110],'Rounded crust loaf with bevelled shoulders and asymmetric soft top.','convex_hull',['jidi_01','jidi_02','fajiao_01','fajiao_07','meijun_03','meijun_05']),
 ('SM_DW_MushroomCap_Pleated',mushroom_cap,[480,420,150],'Independent cap with domed crown, lobed lip and real radial underside pleats.','NoCollision',['fajiao_02','jiaomu_02','jiaomu_03','jiaomu_04','shendu_07']),
 ('SM_DW_MushroomStem',mushroom_stem,[90,90,320],'Tapered bent stalk with flared roots, separate from cap.','convex_decomposition_or_simple_capsule',['fajiao_02','jiaomu_02','jiaomu_03','jiaomu_04','shendu_07']),
 ('SM_DW_Bud',bud,[70,70,65],'Closed lobed fungal/dough bud.','NoCollision',['fajiao_01','fajiao_02','jiaomu_02','jiaomu_04','shendu_07']),
 ('SM_DW_PoreRing',lambda:annulus(),[600,500,300],'Hollow annular cut wall with thick rim and unobstructed center.','complex_as_simple_static_or_segmented_convex',['qianceng_03','qianceng_04','meijun_02','meijun_07']),
 ('SM_DW_PoreHalfRing',lambda:annulus(math.pi,32),[600,250,300],'Closed solid semicircular wall section; inner side remains concave/open. End caps close only wall thickness.','complex_as_simple_static_or_segmented_convex',['qianceng_06','fajiao_03','meijun_01','shendu_04']),
 ('SM_DW_PoreQuarterRing',lambda:annulus(math.pi/2,16),[300,250,300],'Quarter annular wall for modular exposed cuts or partial portal rims.','complex_as_simple_static_or_segmented_convex',['qianceng_03','meijun_02','meijun_07']),
 ('SM_DW_PoreInnerSleeve',lambda:annulus(thin=True),[500,400,600],'Deep thin hollow sleeve, no center cap. Used with separate top rim and optional dark bed.','complex_as_simple_static_or_segmented_convex',['qianceng_04','meijun_02','meijun_07']),
 ('SM_DW_GlutenColumn',gluten_column,[45,45,240],'Curved tapered gluten strand with polygonal grooves.','NoCollision',['qianceng_02','fajiao_01','jiaomu_07','meijun_04','shendu_07']),
 ('SM_DW_RoofTile',lambda:arched_roof(16,True),[240,180,60],'Curved bread roof tile with actual underside shell, not a plane.','NoCollision',['jidi_02','jidi_04','meijun_03']),
 ('SM_DW_ArcRoof',arched_roof,[1000,800,240],'Full arched roof shell; independent component suitable for CameraFade.','NoCollision',['jidi_02','jidi_04','qianceng_06','shendu_05']),
 ('SM_DW_BridgePlank',round_brick,[400,70,35],'Soft beveled plank/slab with slightly crowned top; repeat across bridge.','convex_hull',['meijun_06','jidi_03','jidi_04']),
 ('SM_DW_SupportPost',support_post,[60,60,500],'Tapered faceted support column with integrated foot/capital.','convex_hull',['jidi_02','jidi_03','jidi_04','meijun_06']),
 ('SM_DW_AltarDish',altar_dish,[800,700,80],'Stepped altar plinth with raised rim and a real recessed central bowl.','complex_as_simple_static_or_convex_segments',['shendu_02']),
]

result=[]
(OBJ/'DoughWorld_Candidate.mtl').write_text('newmtl DoughWorld_Candidate\nKa 0.15 0.12 0.07\nKd 0.74 0.57 0.32\nKs 0.05 0.05 0.05\nNs 10\nd 1\nillum 2\n',encoding='ascii')
for name,create,size,description,collision,modules in specs:
    v,f=create();v,xf=fit_bottom_pivot(v,size);f,n,diag=orient_and_validate(v,f)
    path=OBJ/(name+'.obj');write_obj(path,v,f,n)
    lo=v.min(axis=0);hi=v.max(axis=0)
    assert np.max(abs((hi-lo)-np.array(size)))<1e-6
    assert abs(lo[2])<1e-7 and abs(lo[0]+hi[0])<1e-7 and abs(lo[1]+hi[1])<1e-7
    entry={'key':name,'obj_path':str(path),'obj_relative_path':'OBJ/'+path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
           'units':'centimeters','import_uniform_scale':1.0,'pivot':'bottom_center_of_bounds','axis_convention':'+Z up; X width; Y depth',
           'bounds_min_cm':np.round(lo,6).tolist(),'bounds_max_cm':np.round(hi,6).tolist(),'dimensions_cm':size,
           'description':description,'recommended_collision':collision,'module_requirements':modules,'validation':diag,
           'normal_policy':'area-weighted smooth vertex normals, consistent outward winding',
           'uv_policy':'UV0 dominant face projection; no packed lightmap atlas; use candidate material or world aligned texture'}
    if 'Pore' in name:
        center=xf['original_origin_in_output_cm'][:2]
        hits=vertical_axis_hits(v,f,center)
        entry['arc_axis_xy_cm']=np.round(center,6).tolist()
        entry['validation']['hollow_axis_vertical_intersections']=hits
        entry['collision_warning']='A single auto-convex hull fills the opening. Use static complex collision or multiple convex wall segments; inspect capsule path.'
        assert hits==0,(name,hits)
    if name=='SM_DW_AltarDish':entry['central_bowl_floor_above_pivot_cm']=49.6
    if name=='SM_DW_ArcRoof':entry['center_underside_height_cm']=192
    result.append(entry)

# Minimal reusable assembly recipes. Root XY must stay at the original source anchor.
# These are candidate shapes/offsets, not new gameplay implementation.
recipes=[
 {'key':'fungus_medium','source_examples':['flora/yeast_buds_fajiao'],'root_xy_policy':'unchanged',
  'components':[{'mesh':'SM_DW_MushroomStem','bottom_offset_cm':[0,0,0],'target_dimensions_cm':[90,90,320],'collision':'BlockAll','fade':False},
                {'mesh':'SM_DW_MushroomCap_Pleated','bottom_offset_cm':[0,0,270],'target_dimensions_cm':[480,420,150],'collision':'NoCollision','fade':True},
                {'mesh':'SM_DW_Bud','bottom_offset_cm':[160,80,0],'target_dimensions_cm':[100,95,85],'collision':'NoCollision','fade':False}]},
 {'key':'fungus_large','source_examples':['regions/jiaomu'],'root_xy_policy':'source anchor plus additive decor offset',
  'components':[{'mesh':'SM_DW_MushroomStem','bottom_offset_cm':[0,0,0],'target_dimensions_cm':[200,200,700],'collision':'BlockAll','fade':False},
                {'mesh':'SM_DW_MushroomCap_Pleated','bottom_offset_cm':[0,0,600],'target_dimensions_cm':[1100,1000,300],'collision':'NoCollision','fade':True}]},
 {'key':'deep_pore','source_examples':['holes/hole_abyss'],'root_xy_policy':'unchanged',
  'components':[{'mesh':'SM_DW_PoreRing','bottom_offset_cm':[0,0,-200],'target_dimensions_cm':[1600,1400,300],'collision':'static_complex_or_segmented','fade':False},
                {'mesh':'SM_DW_PoreInnerSleeve','bottom_offset_cm':[0,0,-950],'target_dimensions_cm':[1120,910,850],'collision':'static_complex_or_segmented','fade':False}],
  'notes':'Terrain must have a matching real opening. Ring meshes do not remove Landscape underneath. Do not cap center with collision or single auto-convex hull.'},
 {'key':'base_arc_roof','source_examples':['buildings/base_hut'],'root_xy_policy':'unchanged',
  'components':[{'mesh':'SM_DW_ArcRoof','bottom_offset_cm':[0,0,280],'target_dimensions_cm':[1000,800,240],'collision':'NoCollision','fade':True}],
  'notes':'Walls, entry and floor are separate. Each roof owns candidate material instance; fading roof does not change wall collision.'},
 {'key':'altar_and_core','source_examples':['buildings/core_altar','monsters/boss_core'],'root_xy_policy':'both source XY unchanged',
  'components':[{'mesh':'SM_DW_AltarDish','bottom_offset_cm':[0,0,0],'target_dimensions_cm':[800,700,80],'collision':'static_complex_or_convex_segments','fade':False}],
  'boss_visual_min_bottom_z_cm':50,'notes':'Lift the Boss body visually from the recessed bowl floor; keep altar and Boss source roots coincident.'},
]
manifest={'schema_version':'1.0','status':'source_geometry_generated_offline_not_imported_or_runtime_verified',
          'generation_script':str(Path(__file__).resolve()),'meters_to_obj_cm':100.0,'obj_import_scale':1.0,
          'source_coordinates_modified':False,'asset_count':len(result),
          'intended_destination':'/Game/Maps/<candidate>/Geometry',
          'bounds_scaling':'actor_scale_axis = target_dimensions_cm_axis / dimensions_cm_axis; all bottom pivots allow local ground placement with no half-height offset',
          'mesh_count_quality_claim':'All are one connected, closed, consistently oriented manifold with no degenerate triangles; this does not prove UE collision import or runtime visual quality.',
          'material_policy':'No shared pack material modifications. Import meshes with candidate material; cap/roof slots require CameraFade hookup and independent components.',
          'assets':result,'assembly_recipes':recipes}
(ROOT/'GeometryManifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
(ROOT/'README.md').write_text('''# DoughWorld 候选几何源

14 个 OBJ 已按厘米输出；导入缩放为 1。全部以实际包围盒底部中心为 pivot，Z 向上。`GeometryManifest.json` 给出每个网格精确尺寸、校验与建议碰撞方式，以及多部件组装示例。

造型包括圆润面包砖、真实褶皱菌伞、独立菌柄、幼芽、中空孔环/半环/四分之一环/深内壁、弯曲面筋、弧顶瓦片、完整弧顶、圆角桥板、支撑柱和凹碗祭坛。可以按目标尺寸缩放，颜色通过候选材质指定。

孔环与内壁是真实中空几何，顶到底的中心轴没有三角面覆盖。**不得给整环生成一个凸包碰撞，否则会填死孔口。** 可使用静态复杂碰撞，或将碰撞分为多个壁段凸体。网格本身不删除下方 Landscape，必须另外处理地形可见性/开孔。

伞帽和屋顶单独导入/组装，标签贴在相应 StaticMeshComponent，柄/门口/危险边缘不得随之消失。屋顶为有厚度的弧形壳，非实心堵门块。

已完成：有限值、封闭边、二面共享、连通性、一致绕序、正体积、非退化三角、归一化法线、包围盒和孔中心穿透性检查。未执行通用自交检测；尚未导入 UE，也未验证导入碰撞、材质、镜头或性能。UV0 为每面主方向投影，适合平色/世界坐标材质，没有制作专用贴图或 lightmap atlas。
''',encoding='utf-8')
print(json.dumps({'asset_count':len(result),'total_vertices':sum(x['validation']['vertex_count'] for x in result),
                  'total_triangles':sum(x['validation']['triangle_count'] for x in result),
                  'all_watertight':all(x['validation']['watertight'] for x in result),
                  'degenerate_triangles':0,'manifest':str(ROOT/'GeometryManifest.json')},ensure_ascii=False))
