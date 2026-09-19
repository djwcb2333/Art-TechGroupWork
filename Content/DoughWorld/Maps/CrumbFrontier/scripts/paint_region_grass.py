"""Run in Unreal Editor. Creates native foliage in the current ArtPass map only."""
import unreal, math, random, json, os
ROOT='/Game/Maps/CrumbFrontier/Foliage'
WORLD=unreal.EditorLevelLibrary.get_editor_world()
assert WORLD.get_path_name().startswith('/Game/Maps/CrumbFrontier/Levels/L_CrumbFrontier_ArtPass.'), WORLD.get_path_name()
ASSETS=unreal.AssetToolsHelpers.get_asset_tools()
MEL=unreal.MaterialEditingLibrary
REPORT=os.path.join(unreal.Paths.project_content_dir(),'Maps/CrumbFrontier/Documentation/RegionGrass_Report.json')
def asset(name,folder,cls,factory):
 path=ROOT+'/'+folder+'/'+name
 obj=unreal.load_asset(path)
 return obj if obj else ASSETS.create_asset(name,ROOT+'/'+folder,cls,factory)
def prop(obj,key,value):
 assert ('``'+key+'``') in obj.__class__.__doc__, (obj.__class__.__name__,key)
 obj.set_editor_property(key,value)
mat=asset('M_RegionGrass_NoRVT','Materials',unreal.Material,unreal.MaterialFactoryNew())
if not unreal.EditorAssetLibrary.get_metadata_tag(mat,'RegionGrassReady'):
 prop(mat,'two_sided',True)
 prop(mat,'used_with_instanced_static_meshes',True)
 tint=MEL.create_material_expression(mat,unreal.MaterialExpressionVectorParameter,-650,0)
 prop(tint,'parameter_name','GrassColor')
 prop(tint,'default_value',unreal.LinearColor(.24,.35,.1,1))
 rnd=MEL.create_material_expression(mat,unreal.MaterialExpressionPerInstanceRandom,-850,230)
 mult=MEL.create_material_expression(mat,unreal.MaterialExpressionMultiply,-650,230)
 prop(mult,'const_b',.35)
 MEL.connect_material_expressions(rnd,'',mult,'A')
 add=MEL.create_material_expression(mat,unreal.MaterialExpressionAdd,-450,230)
 prop(add,'const_b',.65)
 MEL.connect_material_expressions(mult,'',add,'A')
 color=MEL.create_material_expression(mat,unreal.MaterialExpressionMultiply,-180,0)
 MEL.connect_material_expressions(tint,'',color,'A')
 MEL.connect_material_expressions(add,'',color,'B')
 MEL.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
 rough=MEL.create_material_expression(mat,unreal.MaterialExpressionConstant,-180,300)
 prop(rough,'r',.9)
 MEL.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
 MEL.recompile_material(mat)
 unreal.EditorAssetLibrary.set_metadata_tag(mat,'RegionGrassReady','1')
unreal.EditorAssetLibrary.save_loaded_asset(mat)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
zones={}
for a in actors:
 label=a.get_actor_label()
 if label.startswith('Zone_'):
  i=int(label.split('_')[1]);p=a.get_actor_location();s=a.get_actor_scale3d()
  zones[i]=dict(name=label,cx=p.x,cy=p.y,rx=abs(s.x)*50,ry=abs(s.y)*50,z=p.z+abs(s.z)*50,rect=i==0)
assert len(zones)==6, zones
def inside(z,x,y,margin=0):
 dx=(x-z['cx'])/(z['rx']-margin);dy=(y-z['cy'])/(z['ry']-margin)
 return abs(dx)<=1 and abs(dy)<=1 if z['rect'] else dx*dx+dy*dy<=1
roads=[];blocks=[];pools=[]
for a in actors:
 n=a.get_actor_label()
 if n.startswith('Route_'):
  p=a.get_actor_location();s=a.get_actor_scale3d();r=a.get_actor_rotation();t=math.radians(r.yaw)
  roads.append((p.x,p.y,abs(s.x)*50*abs(math.cos(math.radians(r.pitch)))+70,abs(s.y)*50+90,math.cos(t),math.sin(t)))
 elif n.startswith('MoldPool_'):
  p=a.get_actor_location();s=a.get_actor_scale3d();pools.append((p.x,p.y,abs(s.x)*50+35,abs(s.y)*50+35))
 elif n.startswith(('POI_','Resource_','Village_','Dress_','Player_')):
  p=a.get_actor_location()
  if 'SporeTree' in n: blocks.append((p.x,p.y,100,100))
  elif any(k in n for k in ['WheatClump','MoldReeds','YeastBloom']):blocks.append((p.x,p.y,45,45))
  else:
   o,e=a.get_actor_bounds(False)
   blocks.append((o.x,o.y,e.x+65,e.y+65))
def clear(x,y):
 for cx,cy,hx,hy,c,s in roads:
  dx=x-cx;dy=y-cy
  if abs(dx*c+dy*s)<hx and abs(-dx*s+dy*c)<hy:return False
 for cx,cy,hx,hy in blocks:
  if abs(x-cx)<hx and abs(y-cy)<hy:return False
 for cx,cy,rx,ry in pools:
  if ((x-cx)/rx)**2+((y-cy)/ry)**2<1:return False
 return True
settings=[('Spawn',(.24,.34,.23),85),('WheatValley',(.55,.34,.055),65),('CrumbFrontier',(.32,.12,.43),65),('WheatVillage',(.65,.43,.085),80),('MoldSwamp',(.075,.22,.105),65),('YeastTown',(.41,.21,.52),80)]
mesh=unreal.load_asset('/Game/Maps/Stylized_Scene/Foliage/Grass_03')
assert mesh
report={'level':WORLD.get_path_name(),'usesRVT':False,'nativeFoliage':True,'regions':[],'assets':[]}
for i,(name,rgb,spacing) in enumerate(settings):
 mi=asset('MI_Grass_'+name,'Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
 MEL.set_material_instance_parent(mi,mat)
 # UE 5.8 implementation returns false even after setting the value; verify by readback later.
 MEL.set_material_instance_vector_parameter_value(mi,'GrassColor',unreal.LinearColor(*rgb,1))
 unreal.EditorAssetLibrary.save_loaded_asset(mi)
 ft=asset('FT_Grass_'+name,'Types',unreal.FoliageType_InstancedStaticMesh,unreal.FoliageType_InstancedStaticMeshFactory())
 for key,value in dict(mesh=mesh,override_materials=[mi],density=1000000/(spacing*spacing),radius=spacing*.38,scale_x=unreal.FloatInterval(.85,1.3),scale_y=unreal.FloatInterval(.85,1.3),scale_z=unreal.FloatInterval(.85,1.3),z_offset=unreal.FloatInterval(5.8283,8.9138),cull_distance=unreal.Int32Interval(14000,20000),cast_shadow=False,collision_with_world=False,enable_density_scaling=True).items():prop(ft,key,value)
 unreal.EditorAssetLibrary.save_loaded_asset(ft)
 unreal.InstancedFoliageActor.remove_all_instances(WORLD,ft)
 rng=random.Random(8300+i);z=zones[i];transforms=[];samples=[]
 x=z['cx']-z['rx']+spacing*.5
 while x<z['cx']+z['rx']:
  y=z['cy']-z['ry']+spacing*.5
  while y<z['cy']+z['ry']:
   px=x+rng.uniform(-spacing*.24,spacing*.24);py=y+rng.uniform(-spacing*.24,spacing*.24)
   nested=(i==1 and inside(zones[3],px,py)) or (i==2 and inside(zones[5],px,py))
   if inside(z,px,py,40) and not nested and clear(px,py):
    scale=rng.uniform(.85,1.3);yaw=rng.uniform(0,360)
    pz=z['z']+6.856781005859375*scale
    t=unreal.Transform()
    t.translation=unreal.Vector(px,py,pz)
    t.rotation=unreal.Rotator(0,yaw,0).quaternion()
    t.scale3d=unreal.Vector(scale,scale,scale)
    transforms.append(t)
    if len(samples)<5:samples.append([px,py,pz,scale])
   y+=spacing
  x+=spacing
 unreal.InstancedFoliageActor.add_instances(WORLD,ft,transforms)
 report['regions'].append(dict(region=z['name'],foliageType=ft.get_path_name(),count=len(transforms),groundZ=z['z'],linearColor=rgb,spacingCm=spacing,samples=samples))
 report['assets'] += [ft.get_path_name(),mi.get_path_name()]
 unreal.log('REGION_GRASS '+name+' '+str(len(transforms)))
report['total']=sum(r['count'] for r in report['regions'])
report['saved']=bool(unreal.EditorLevelLibrary.save_current_level())
os.makedirs(os.path.dirname(REPORT),exist_ok=True)
with open(REPORT,'w',encoding='utf-8') as f:json.dump(report,f,ensure_ascii=False,indent=2)
unreal.log('REGION_GRASS_DONE '+str(report['total'])+' saved='+str(report['saved']))

