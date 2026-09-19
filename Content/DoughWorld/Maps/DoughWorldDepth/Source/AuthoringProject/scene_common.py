import unreal,json,math,random
from pathlib import Path
P=Path(__file__).parent; ROOT='/Game/Maps/DoughWorldDepth'
A=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);W=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert W.get_path_name().startswith(ROOT+'/Levels/')
AT=unreal.AssetToolsHelpers.get_asset_tools();M=unreal.MaterialEditingLibrary
source=json.loads((P/'Handoff/Originals/10_document.json').read_text(encoding='utf-8'))
terrain=json.loads((P/'TerrainDesign/terrain_data.json').read_text(encoding='utf-8'));depth=json.loads((P/'TerrainDesign/depth_supplement.json').read_text(encoding='utf-8'));plan=json.loads((P/'Planning/ModulePlan.json').read_text(encoding='utf-8'))
byref={a['source_ref']:a for a in depth['anchors']};records=[];roots={};mesh_cache={};mat_cache={}
def h(x,y):
    u=max(0,min(503.9999,x/500*504));v=max(0,min(503.9999,y/500*504));i=int(u);j=int(v);tx=u-i;ty=v-j;H=terrain['heights_m']
    return (1-ty)*((1-tx)*H[j][i]+tx*H[j][i+1])+ty*((1-tx)*H[j+1][i]+tx*H[j+1][i+1])
def ground(x,y,region=None):return -60.0 if region=='shendu' else h(x,y)
def anchor(ref):
    a=byref[ref];return list(a['source_xy_m'])+[a['z_m']]
def world(xyz):return unreal.Vector((xyz[0]-250)*100,(xyz[1]-250)*100,xyz[2]*100)
def mesh(key):
    if key in mesh_cache:return mesh_cache[key]
    if key.startswith('/') :path=key
    elif key.startswith('SM_DW_'):path=ROOT+'/Meshes/'+key
    elif key in plan['assets']:path=plan['assets'][key]['package_path']
    else:raise ValueError(key)
    obj=unreal.load_asset(path);assert obj,key;mesh_cache[key]=obj;return obj
def material(name):
    if name not in mat_cache:mat_cache[name]=unreal.load_asset(ROOT+'/Materials/MI_'+name)
    assert mat_cache[name],name;return mat_cache[name]
def piece(name,mesh_key,xyz_m_bottom,dimensions_m,color='Dough',collision=True,fade=False,yaw=0,module='',source=''):
    sm=mesh(mesh_key);box=sm.get_bounding_box();mn=box.min;mx=box.max;sz=[mx.x-mn.x,mx.y-mn.y,mx.z-mn.z];assert min(sz)>.001,(mesh_key,sz)
    scale=[dimensions_m[i]*100/sz[i] for i in range(3)];cx=(mn.x+mx.x)*.5*scale[0];cy=(mn.y+mx.y)*.5*scale[1];ang=math.radians(yaw);offset=[cx*math.cos(ang)-cy*math.sin(ang),cx*math.sin(ang)+cy*math.cos(ang)]
    loc=world(xyz_m_bottom);loc.x-=offset[0];loc.y-=offset[1];loc.z-=mn.z*scale[2]
    cls=unreal.load_class(None,'/Script/GDATtest.DepthEnvironmentActor') if fade else unreal.StaticMeshActor
    a=A.spawn_actor_from_class(cls,loc,unreal.Rotator(roll=0,pitch=0,yaw=yaw));assert a,name;a.set_actor_label(name);a.set_folder_path('Depth/'+module.split('_')[0]+'/'+module);a.set_editor_property('tags',[unreal.Name('DepthBuilt'),unreal.Name('Module:'+module),unreal.Name('Source:'+source)])
    c=a.get_component_by_class(unreal.StaticMeshComponent)
    if fade:a.root_component.set_mobility(unreal.ComponentMobility.STATIC)
    c.set_mobility(unreal.ComponentMobility.STATIC);c.set_static_mesh(sm);a.set_actor_scale3d(unreal.Vector(*scale));c.set_collision_profile_name('BlockAll' if collision else 'NoCollision');c.set_editor_property('can_ever_affect_navigation',collision)
    if color:
        for slot in range(max(1,c.get_num_materials())):c.set_material(slot,material(color))
    if fade:
        c.set_editor_property('component_tags',[unreal.Name('CameraFade')])
        if not collision:c.set_editor_property('cast_shadow',False)
    records.append({'actor':a.get_path_name(),'label':name,'module':module,'source_ref':source,'mesh':sm.get_path_name(),'bottom_xyz_m':list(xyz_m_bottom),'dimensions_m':list(dimensions_m),'yaw':yaw,'material':color,'collision':str(c.get_collision_enabled()),'fade':fade})
    return a
def point(ref):
    xyz=anchor(ref);a=A.spawn_actor_from_class(unreal.TargetPoint,world(xyz),unreal.Rotator());a.set_actor_label('SOURCE__'+ref.replace('/','__'));a.set_folder_path('Depth/SourceAnchors');a.set_editor_property('tags',[unreal.Name('DepthBuilt'),unreal.Name('OriginalSource:'+ref)]);roots[ref]=a
    if ref.startswith(('buildings/','resources/')):a.set_actor_hidden_in_game(False)
    return a
def save_report():
    (P/'PlacementManifest.json').write_text(json.dumps({'schema':'actual_editor_placement_v1','level':W.get_path_name(),'source_anchors':{k:{'actor':v.get_path_name(),'xyz_m':anchor(k)} for k,v in roots.items()},'objects':records},ensure_ascii=False,indent=2),encoding='utf-8')
