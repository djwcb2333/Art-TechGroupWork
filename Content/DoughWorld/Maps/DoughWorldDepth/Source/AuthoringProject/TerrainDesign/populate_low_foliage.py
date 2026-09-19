"""Run manually in the candidate Unreal level, after terrain and modules are built.

This file is delivered unexecuted. It alters only this script's owned foliage
types under /Game/Maps/DoughWorldDepth/Foliage and never edits pack materials.
"""
import collections
import datetime
import hashlib
import json
import math
import random
from pathlib import Path

import unreal

P = Path(__file__).resolve().parent
ROOT = '/Game/Maps/DoughWorldDepth'
FROOT = ROOT + '/Foliage'
OWNER = 'DoughWorldDepth_LowFoliage_20260910_v1'
OWNER_KEY = 'DepthLowFoliageOwner'
SEED = 9102026
REPORT_PATH = P / 'actual_low_foliage_report.json'
T = json.loads((P/'terrain_data.json').read_text(encoding='utf-8'))
S = json.loads((P/'depth_supplement.json').read_text(encoding='utf-8'))
source_path = P.parent/'Handoff'/'Originals'/'10_document.json'
if not source_path.exists():
    source_path = P/'10_document.json'
D = json.loads(source_path.read_text(encoding='utf-8-sig'))
H = T['heights_m']
N = int(T.get('n', T['resolution']))
SIZE = float(T['size_m'])
SPACING = SIZE/(N-1)
W = unreal.EditorLevelLibrary.get_editor_world()
A = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
assert W and W.get_path_name().startswith(ROOT+'/Levels/'), W.get_path_name() if W else 'No editor world'
assert 'PIE_' not in W.get_path_name(), 'Stop PIE before placing editor foliage.'

report = {
    'status':'preparing', 'world':W.get_path_name(), 'owner':OWNER,
    'random_seed':SEED, 'terrain_sha256':hashlib.sha256((P/'terrain_data.json').read_bytes()).hexdigest(),
    'native_foliage':True, 'uses_rvt':False, 'cleanup_scope':FROOT+'/Types/FT_Low_* with matching ownership metadata only',
    'regions':[], 'scene_footprint_exclusions':[], 'scene_bounds_ignored':[],
    'warnings':[], 'assets':[], 'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'exclusion_rules': {'path_half_width_m':4.0,'facility_square_m':14,'source_building_min_buffer_m':1.0,'mechanism_radius_m':7,'resource_radius_m':3.6,'monster_radius_m':4.5,'hole_radius_plus_m':4.0,'boundary_buffer_m':6.0,'maximum_slope_degrees':18.0,'waterfall_and_stream_excluded':True,'underground_excluded':True},
}

def write_report():
    REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

def height(x,y):
    u=max(0,min(N-1-1e-8,x/SIZE*(N-1))); v=max(0,min(N-1-1e-8,y/SIZE*(N-1)))
    i,j=int(u),int(v);a,b=u-i,v-j
    return (H[j][i]*(1-a)+H[j][i+1]*a)*(1-b)+(H[j+1][i]*(1-a)+H[j+1][i+1]*a)*b

def slope(x,y):
    dx=(height(x+.5,y)-height(x-.5,y))
    dy=(height(x,y+.5)-height(x,y-.5))
    return math.degrees(math.atan(math.hypot(dx,dy)))

def segdist(x,y,a,b):
    vx,vy=b[0]-a[0],b[1]-a[1];den=vx*vx+vy*vy
    if den<1e-12:return math.hypot(x-a[0],y-a[1])
    t=max(0,min(1,((x-a[0])*vx+(y-a[1])*vy)/den))
    return math.hypot(x-a[0]-t*vx,y-a[1]-t*vy)

def inside(x,y,pts):
    result=False
    for a,b in zip(pts,pts[1:]+pts[:1]):
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:result=not result
    return result

POLYGONS=T['regions']
REGION_ORDER=['jidi','jiaomu','fajiao','meijun','qianceng']
def region_at(x,y):
    for k in REGION_ORDER:
        if inside(x,y,POLYGONS[k]):return k
    return None

ROADS=[(a,b) for line in T['routes'] for a,b in zip(line,line[1:])]
BORDERS=[(a,b) for boundary in S['boundaries'] for a,b in zip(boundary['polyline_m'],boundary['polyline_m'][1:])]
FACILITIES=[q['pos'] for q in D['buildings'] if q.get('region')!='shendu']
PIN_RADII=[]
for group,radius in [('mechanisms',7),('resources',3.6),('monsters',4.5)]:
    for q in D[group]:
        if q.get('region')!='shendu':PIN_RADII.append((q['pos'][0],q['pos'][1],radius))
for q in S['holes']:PIN_RADII.append((q['xy_m'][0],q['xy_m'][1],q['radius_m']+4))
for q in D['demo_route']:PIN_RADII.append((q['pos'][0],q['pos'][1],5.0 if q['type']=='combat' else 3.5))
LANDMARK=D['landmarks'][0]
fall_a,fall_b,stream_end=LANDMARK['crest_pos'],LANDMARK['pool_pos'],LANDMARK['stream_to']
deep=next(q for q in D['regions'] if q['id']=='shendu')

# Read actual already placed mesh actors. Bounds are measured from the candidate
# world at execution time; a planning list is never treated as placement proof.
# Canopies above head height do not reserve an entire bare patch below them.
BLOCKS=[]
for actor in A.get_all_level_actors():
    if isinstance(actor,unreal.InstancedFoliageActor):continue
    comps=actor.get_components_by_class(unreal.StaticMeshComponent)
    if not comps:continue
    origin,extent=actor.get_actor_bounds(False)
    x,y=origin.x/100+250,origin.y/100+250
    if not (0<=x<=500 and 0<=y<=500):continue
    base=origin.z-extent.z;top=origin.z+extent.z
    ground=height(x,y)*100
    if top<ground-75 or base>ground+160:
        report['scene_bounds_ignored'].append({'actor':actor.get_path_name(),'reason':'underground_or_overhead','bottom_z_cm':base,'top_z_cm':top})
        continue
    # Terrain-like world-scale helpers and water planes are handled by explicit
    # road/water/hole masks, not a single blanket AABB across the whole region.
    if extent.x>6500 or extent.y>6500:
        report['scene_bounds_ignored'].append({'actor':actor.get_path_name(),'reason':'wide_helper_bounds_use_explicit_masks','extent_xy_cm':[extent.x,extent.y]})
        continue
    if extent.x<3 and extent.y<3:continue
    margin=.5
    box=(x,y,extent.x/100+margin,extent.y/100+margin)
    BLOCKS.append(box)
    report['scene_footprint_exclusions'].append({'actor':actor.get_path_name(),'label':actor.get_actor_label(),'center_xy_m':[x,y],'half_size_m':[box[2],box[3]],'ground_intersection':True})

# Spatial bins keep clearance checks local even with hundreds of modules.
BLOCK_GRID=collections.defaultdict(list)
CELL=12.0
for index,(x,y,hx,hy) in enumerate(BLOCKS):
    for ix in range(math.floor((x-hx)/CELL),math.floor((x+hx)/CELL)+1):
        for iy in range(math.floor((y-hy)/CELL),math.floor((y+hy)/CELL)+1):BLOCK_GRID[(ix,iy)].append(index)

REJECTIONS=collections.Counter()
def clear(x,y,key,record=True):
    reason=None
    if not (3<x<497 and 3<y<497) or region_at(x,y)!=key:reason='outside_region'
    elif ((x-deep['center'][0])/deep['radius'][0])**2+((y-deep['center'][1])/deep['radius'][1])**2<1:reason='deep_surface_background'
    elif any(segdist(x,y,a,b)<4 for a,b in ROADS):reason='walkway'
    elif any(abs(x-p[0])<8 and abs(y-p[1])<8 for p in FACILITIES):reason='building_clearance'
    elif any(math.hypot(x-px,y-py)<r for px,py,r in PIN_RADII):reason='original_anchor_buffer'
    elif segdist(x,y,fall_a,fall_b)<16 or segdist(x,y,fall_b,stream_end)<9:reason='watercourse'
    elif any(segdist(x,y,a,b)<6 for a,b in BORDERS):reason='boundary_cliff'
    elif slope(x,y)>18:reason='steep_ground'
    else:
        for idx in BLOCK_GRID.get((math.floor(x/CELL),math.floor(y/CELL)),[]):
            bx,by,hx,hy=BLOCKS[idx]
            if abs(x-bx)<hx and abs(y-by)<hy:reason='actual_scene_mesh';break
    if reason:
        if record:REJECTIONS[reason]+=1
        return False
    return True

SETTINGS={
 'qianceng':{'count':2600,'color':(.40,.32,.12),'patches':40,'radius':[6,15]},
 'jidi':{'count':350,'color':(.40,.39,.19),'patches':8,'radius':[3,7]},
 'fajiao':{'count':1150,'color':(.59,.35,.075),'patches':20,'radius':[5,12]},
 'jiaomu':{'count':1050,'color':(.34,.43,.12),'patches':18,'radius':[6,13]},
 'meijun':{'count':1450,'color':(.20,.22,.13),'patches':24,'radius':[6,15]},
}

# Build the entire deterministic placement proposal before removing any prior
# owned instances, so an asset/mask/region problem cannot clear good foliage.
placements={}; OCCUPIED=collections.defaultdict(list); MIN_DISTANCE=.63
def spaced(x,y):
    ix,iy=math.floor(x/MIN_DISTANCE),math.floor(y/MIN_DISTANCE)
    for i in range(ix-1,ix+2):
        for j in range(iy-1,iy+2):
            if any((x-px)**2+(y-py)**2<MIN_DISTANCE**2 for px,py in OCCUPIED.get((i,j),[])):return False
    OCCUPIED[(ix,iy)].append((x,y));return True

for ridx,(key,cfg) in enumerate(SETTINGS.items()):
    rng=random.Random(SEED+ridx*1009); poly=POLYGONS[key]
    xlo,xhi=min(p[0] for p in poly),max(p[0] for p in poly)
    ylo,yhi=min(p[1] for p in poly),max(p[1] for p in poly)
    centers=[]
    for attempt in range(cfg['patches']*400):
        x,y=rng.uniform(xlo,xhi),rng.uniform(ylo,yhi)
        if clear(x,y,key) and all(math.hypot(x-a,y-b)>5 for a,b,_,_ in centers):
            centers.append((x,y,rng.uniform(*cfg['radius']),rng.uniform(0,math.tau)))
            if len(centers)>=cfg['patches']:break
    chosen=[]
    for attempt in range(cfg['count']*100):
        if not centers:break
        cx,cy,r,ang=centers[rng.randrange(len(centers))]
        localx=rng.gauss(0,r*.43);localy=rng.gauss(0,r*.30)
        if (localx/r)**2+(localy/(r*.75))**2>1.2:continue
        x=cx+localx*math.cos(ang)-localy*math.sin(ang)
        y=cy+localx*math.sin(ang)+localy*math.cos(ang)
        if not clear(x,y,key) or not spaced(x,y):continue
        # Ground-level grasses predominate; weed and bush accents stay below
        # the player silhouette and never replace a tree or fungal landmark.
        kind_roll=rng.random()
        kind='Grass1' if kind_roll<.48 else ('Grass2' if kind_roll<.88 else ('Weed' if kind_roll<.985 else 'Bush'))
        if key=='jidi' and kind=='Bush':kind='Grass2'
        wanted_height=rng.uniform(.18,.38) if kind.startswith('Grass') else rng.uniform(.22,.48)
        chosen.append({'xy_m':[x,y],'ground_z_m':height(x,y),'kind':kind,'height_m':wanted_height,'yaw':rng.uniform(0,360),'slope_deg':slope(x,y)})
        if len(chosen)>=cfg['count']:break
    placements[key]=chosen
    report['regions'].append({'id':key,'target':cfg['count'],'planned':len(chosen),'patch_centers_m':[list(q) for q in centers],'linear_color':cfg['color']})
    if len(chosen)<cfg['count']:report['warnings'].append(key+': actual scene clearances reduce planned instances from '+str(cfg['count'])+' to '+str(len(chosen)))
report['rejected_candidates']=dict(REJECTIONS)
report['planned_total']=sum(map(len,placements.values()))
if not (5000<=report['planned_total']<=8000):
    report['status']='aborted_before_asset_or_instance_changes'
    report['warnings'].append('Actual module exclusions leave insufficient space for 5000 safe instances. No existing foliage was removed.')
    write_report()
    raise RuntimeError('Safe foliage proposal outside 5000-8000 range; inspect actual_low_foliage_report.json')

MESH_PATHS={
 'Grass1':'/Game/Maps/SoStylized/Environment/Foliage/SM_Grass1',
 'Grass2':'/Game/Maps/SoStylized/Environment/Foliage/SM_Grass2',
 'Weed':'/Game/Maps/SoStylized/Environment/Foliage/SM_DesertWeed01',
 'Bush':'/Game/Maps/SoStylized/Environment/Foliage/SM_DesertBush01',
}
meshes={k:unreal.load_asset(v) for k,v in MESH_PATHS.items()}
for k,obj in meshes.items():assert obj, MESH_PATHS[k]
SOURCE_MATERIAL='/Game/Maps/DoughWorldV2/Foliage/M_RegionalGrass_NoRVT'
source_material=unreal.load_asset(SOURCE_MATERIAL)
assert source_material, SOURCE_MATERIAL
for folder in ['Materials','Types']:EAL.make_directory(FROOT+'/'+folder)

def owned_existing(path):
    obj=unreal.load_asset(path)
    if obj:assert EAL.get_metadata_tag(obj,OWNER_KEY)==OWNER, 'Asset already exists with another owner: '+path
    return obj

def tag_save(obj):
    EAL.set_metadata_tag(obj,OWNER_KEY,OWNER)
    assert EAL.save_loaded_asset(obj,False), obj.get_path_name()
    report['assets'].append(obj.get_path_name())

mat_path=FROOT+'/Materials/M_LowGrass_NoRVT'
material=owned_existing(mat_path)
if not material:
    material=EAL.duplicate_asset(SOURCE_MATERIAL,mat_path)
    assert material, 'Failed to duplicate source NoRVT material'
    # Copy only. Shader graph and the pack's original materials are untouched.
    tag_save(material)
report['material_source']=SOURCE_MATERIAL
report['material_copy']=material.get_path_name()

type_map={}; mi_paths=set(); actual_by_region=collections.Counter(); transforms_by_type={}
for key,cfg in SETTINGS.items():
    mi_path=FROOT+'/Materials/MI_LowGrass_'+key
    mi=owned_existing(mi_path)
    if not mi:mi=AT.create_asset('MI_LowGrass_'+key,FROOT+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi,material)
    MEL.set_material_instance_vector_parameter_value(mi,'GrassColor',unreal.LinearColor(*cfg['color'],1.0))
    readback=MEL.get_material_instance_vector_parameter_value(mi,'GrassColor')
    assert max(abs(a-b) for a,b in zip((readback.r,readback.g,readback.b),cfg['color']))<1e-5, ('Color readback failed',key)
    tag_save(mi);mi_paths.add(mi.get_path_name())
    for kind,m in meshes.items():
        values=[q for q in placements[key] if q['kind']==kind]
        name='FT_Low_'+kind+'_'+key;path=FROOT+'/Types/'+name
        ft=owned_existing(path)
        if not ft:ft=AT.create_asset(name,FROOT+'/Types',unreal.FoliageType_InstancedStaticMesh,unreal.FoliageType_InstancedStaticMeshFactory())
        assert ft, path
        slots=max(1,len(m.get_editor_property('static_materials')))
        props={'mesh':m,'override_materials':[mi]*slots,'cast_shadow':False,'collision_with_world':False,'cull_distance':unreal.Int32Interval(8500,14000),'radius':32.0,'density':55.0,'enable_density_scaling':True}
        for prop,value in props.items():ft.set_editor_property(prop,value)
        # Foliage's placement-collision setting does not by itself disable
        # runtime collision. Store NoCollision on the foliage body explicitly.
        body=ft.get_editor_property('body_instance')
        body.set_editor_property('collision_enabled',unreal.CollisionEnabled.NO_COLLISION)
        body.set_editor_property('collision_profile_name',unreal.Name('NoCollision'))
        ft.set_editor_property('body_instance',body)
        tag_save(ft)
        b=m.get_bounding_box();mesh_height=max(1,b.max.z-b.min.z)
        ts=[]
        for q in values:
            scale=q['height_m']*100/mesh_height
            # A tiny burial offsets any tessellation interpolation difference.
            z=q['ground_z_m']*100-b.min.z*scale-1.0
            t=unreal.Transform()
            t.translation=unreal.Vector((q['xy_m'][0]-250)*100,(q['xy_m'][1]-250)*100,z)
            t.rotation=unreal.Rotator(roll=0,pitch=0,yaw=q['yaw']).quaternion()
            t.scale3d=unreal.Vector(scale,scale,scale)
            ts.append(t)
        type_map[path]=ft;transforms_by_type[path]=ts

# Only the exact owned type set is cleared. Unrelated grass, old V2 foliage,
# and the user's own candidates remain untouched, even in this same world.
for path,ft in type_map.items():
    assert path.startswith(FROOT+'/Types/FT_Low_') and EAL.get_metadata_tag(ft,OWNER_KEY)==OWNER
    unreal.InstancedFoliageActor.remove_all_instances(W,ft)
for path,transforms in transforms_by_type.items():
    if transforms:unreal.InstancedFoliageActor.add_instances(W,type_map[path],transforms)

# Actual component enumeration is the placement evidence, not planned counts.
actual_components=[];bad_tilt=0;bad_clearance=0;actual_total=0;actual_by_type=collections.Counter()
for actor in A.get_all_level_actors():
    if not isinstance(actor,unreal.InstancedFoliageActor):continue
    for c in actor.get_components_by_class(unreal.FoliageInstancedStaticMeshComponent):
        mi=c.get_material(0)
        if not mi or mi.get_path_name() not in mi_paths:continue
        key=mi.get_name().replace('MI_LowGrass_','')
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        count=c.get_instance_count();actual_total+=count;actual_by_region[key]+=count
        actual_mesh=c.get_editor_property('static_mesh')
        kind=next(k for k,m in meshes.items() if m.get_path_name()==actual_mesh.get_path_name())
        actual_by_type[FROOT+'/Types/FT_Low_'+kind+'_'+key]+=count
        samples=[]
        for index in range(count):
            tr=c.get_instance_transform(index,True);rot=tr.rotation.rotator();v=tr.translation
            if abs(rot.pitch)>.01 or abs(rot.roll)>.01:bad_tilt+=1
            x,y=v.x/100+250,v.y/100+250
            if not clear(x,y,key,record=False):bad_clearance+=1
            if len(samples)<4:samples.append({'xyz_cm':[v.x,v.y,v.z],'yaw_deg':rot.yaw,'scale':tr.scale3d.x})
        actual_components.append({'component':c.get_path_name(),'mesh':c.get_editor_property('static_mesh').get_path_name(),'material':mi.get_path_name(),'region':key,'actual_instances':count,'collision':str(c.get_collision_enabled()),'samples':samples})
report['actual_total']=actual_total
report['actual_by_region']=dict(actual_by_region)
report['actual_components']=actual_components
report['bad_tilt_instances']=bad_tilt
report['bad_clearance_instances']=bad_clearance
report['actual_type_counts']=dict(actual_by_type)
report['planned_type_counts']={p:len(v) for p,v in transforms_by_type.items()}
report['all_actual_type_counts_match']=all(actual_by_type.get(p,0)==len(v) for p,v in transforms_by_type.items())
report['total_actual_matches_plan']=actual_total==report['planned_total']
report['collision_policy']='NoCollision on owned types and actual owned foliage components'
report['status']='placed_pending_validation'
write_report()
assert actual_total==report['planned_total'] and report['all_actual_type_counts_match'] and bad_tilt==0 and bad_clearance==0, (actual_total,report['planned_total'],bad_tilt,bad_clearance)
report['saved']=bool(unreal.EditorLevelLibrary.save_current_level())
report['status']='saved_actual_native_foliage' if report['saved'] else 'placed_but_save_failed'
report['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
write_report()
assert report['saved'],'Candidate foliage placed but level save failed; see report.'
unreal.log('DOUGH_DEPTH_LOW_FOLIAGE_DONE '+str(actual_total)+' seed='+str(SEED))
