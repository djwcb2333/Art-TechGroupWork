"""Local dressing pass on the saved Depth map; source anchors and source pack stay intact."""
import unreal,json,sys,math,datetime,traceback
from pathlib import Path
P=Path(__file__).parent; B=P.parent;sys.path.insert(0,str(B));import scene_common as sc
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
audit=json.loads((P/'ImportAudit.json').read_text(encoding='utf-8'))
assert not audit['errors'] and not audit['missing_dependencies']
manifest=json.loads((P/'PlacementManifest_BeforeVillage.json').read_text(encoding='utf-8'))
byname={a.get_actor_label():a for a in sc.A.get_all_level_actors()}
info={q['path'].rsplit('/',1)[-1]:q for q in audit['meshes']}
rows={q['label']:q for q in manifest['objects']}
out={'timestamp':datetime.datetime.now().isoformat(),'level':sc.W.get_path_name(),'new_assets':[],'replaced_visuals':[],'placements':[],'errors':[]}
ROOT=sc.ROOT+'/Village';cache={};created=[]
def copy_mesh(name):
    if name in cache:return cache[name]
    source=info[name]['path'];target=ROOT+'/Meshes/'+name
    obj=unreal.load_asset(target) if unreal.EditorAssetLibrary.does_asset_exist(target) else unreal.EditorAssetLibrary.duplicate_asset(source,target)
    assert obj,name
    # Exact static geometry collision avoids filling table-leg and shelf openings.
    obj.get_editor_property('body_setup').set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    unreal.EditorAssetLibrary.save_loaded_asset(obj)
    out['new_assets'].append({'source':source,'destination':target,'materials':[s.material_interface.get_path_name() for s in obj.static_materials]})
    cache[name]=obj;return obj
def prop(label,name,x,y,z=None,width=None,scale=1,yaw=0,module='jidi_02',source='',collision=False,dimensions=None):
    sm=copy_mesh(name);q=info[name];size=[(q['bounds_max_cm'][i]-q['bounds_min_cm'][i])/100 for i in range(3)]
    if width:scale=width/size[0]
    dims=dimensions or [v*scale for v in size]
    a=sc.piece('FV__'+label,sm.get_path_name(),[x,y,sc.h(x,y)-.025 if z is None else z],dims,None,collision,False,yaw,module,source)
    a.set_folder_path('Depth/Village/'+module);a.tags=list(a.tags)+[unreal.Name('VillagePass')]
    r=sc.records[-1];r.update({'art_pass':'FantasticVillage_20260913','source_asset':q['path'],'materials':[a.static_mesh_component.get_material(i).get_path_name() for i in range(a.static_mesh_component.get_num_materials())]})
    out['placements'].append(r);created.append(a);return a,r
def retire(label,keep_collision=False):
    a=byname[label];c=a.get_component_by_class(unreal.StaticMeshComponent)
    assert not any(str(t).startswith('OriginalSource:') for t in a.tags)
    c.set_visibility(False,True);c.set_hidden_in_game(True,True);c.set_editor_property('cast_shadow',False)
    if not keep_collision:c.set_collision_profile_name('NoCollision');c.set_editor_property('can_ever_affect_navigation',False)
    a.set_folder_path('Depth/Village/ReplacedProxyGeometry')
    rows[label].update({'visible':False,'collision':str(c.get_collision_enabled()),'replacement_pass':'FantasticVillage_20260913','retained_collision_proxy':keep_collision})
    out['replaced_visuals'].append({'label':label,'actor':a.get_path_name(),'retained_collision':keep_collision})
def shift(label,dz):
    a=byname[label];v=a.get_actor_location();v.z+=dz*100;a.set_actor_location(v,False,True);rows[label]['bottom_xyz_m'][2]+=dz
try:
    assert not any('VillagePass' in [str(t) for t in a.tags] for a in byname.values()),'Village pass already exists; inspect before rerunning.'
    # Workbench: preserve its source point, replace the six primitive table parts.
    wb='DWG__jidi_03__buildings__workbench__'
    for suffix in ['workbench_tabletop','workbench_lower_shelf']+['workbench_leg_'+str(i) for i in range(4)]+['mixing_bowl']:retire(wb+suffix)
    _,table=prop('Workbench','SM_PROP_table_02',221.7,266.7,6.40625,width=2.8,module='jidi_03',source='buildings/workbench',collision=True)
    top=table['bottom_xyz_m'][2]+table['dimensions_m'][2]-.035
    prop('MixingBowl','SM_PROP_bowl_wood_01',221.05,266.7,top,width=.60,module='jidi_03',source='buildings/workbench')
    shift(wb+'flour_bud',top+.105-rows[wb+'flour_bud']['bottom_xyz_m'][2])
    shift(wb+'rolling_pin',top+.012-rows[wb+'rolling_pin']['bottom_xyz_m'][2])
    prop('BreadBoard','SM_PROP_plate_wood',221.85,266.75,top,width=.48,module='jidi_03')
    prop('FreshBread','SM_PROP_food_bread_02',221.85,266.75,top+.045,width=.32,module='jidi_03')
    prop('BreadRoll','SM_PROP_food_bread_05',222.65,266.4,top,width=.25,yaw=40,module='jidi_03')
    prop('WoodenSpoon','SM_PROP_spoon_01',221.5,266.3,top+.03,scale=1.15,yaw=55,module='jidi_03')
    for i,(x,y) in enumerate([(223.65,266.3),(223.65,267.15),(224.35,266.65)]):prop('WorkbenchFlourBag'+str(i),'SM_PROP_sack_0'+str(i+1),x,y,width=.8,yaw=25*i,module='jidi_03')
    prop('WorkbenchStool','SM_PROP_stool_01',220.1,266.6,scale=1.45,yaw=70,module='jidi_03',collision=True)
    prop('WaterBucket','SM_PROP_bucket_01',222.6,265.55,scale=1.2,module='jidi_03')
    # Open rack plus low boxes; all source anchors and separate canopy survive.
    st='DWG__jidi_04__buildings__storage_repair__'
    for label in rows:
        if label.startswith(st) and any(label[len(st):].startswith(k) for k in ['rack_post_','rack_shelf_','crate_','repair_table_','repair_tray']):retire(label)
    prop('StorageRack','SM_PROP_market_shelf_03',196.7,256.7,6.36625,width=4.1,module='jidi_04',source='buildings/storage_repair',collision=True)
    for i,x in enumerate([195.45,196.7,197.95]):
        prop('StorageBox'+str(i),'SM_PROP_box_02',x,256.7,6.43,width=.94,module='jidi_04',collision=True)
        # Source lid is intentionally leaning, not a horizontal closed lid.
        if i!=1:prop('StorageLeaningLid'+str(i),'SM_PROP_box_lid_02',x,255.95,6.43,width=.94,module='jidi_04')
    for i,(x,y) in enumerate([(195.7,256.7),(196.65,256.7),(197.55,256.7)]):prop('RackBreadBasket'+str(i),'SM_PROP_basket_01',x,y,7.49,width=.63,module='jidi_04')
    for i,x in enumerate([195.3,196.25,197.25,198.1]):prop('RackFlour'+str(i),'SM_PROP_sack_0'+str(i%3+1),x,256.8,8.11,width=.61,yaw=i*30,module='jidi_04')
    _,rt=prop('RepairTable','SM_PROP_table_01',200.2,256.7,6.36625,width=2.3,module='jidi_04',collision=True)
    rtop=rt['bottom_xyz_m'][2]+rt['dimensions_m'][2]-.035
    prop('RepairPlanks','SM_PROP_planks_03',200.2,256.7,rtop,width=1.3,yaw=8,module='jidi_04')
    prop('RepairWheel','SM_PROP_cart_wheel_small',201.45,256.1,scale=1.5,yaw=90,module='jidi_04')
    prop('StorageBarrel','SM_PROP_barrel_01',193.6,256.5,scale=1.15,module='jidi_04',collision=True)
    prop('StorageBagA','SM_PROP_sack_05',193.55,257.8,scale=1.5,module='jidi_04')
    prop('StorageBagB','SM_PROP_sack_06',194.15,258.2,scale=1.3,yaw=35,module='jidi_04')
    # Low perimeter furniture leaves the central hut door and room clear.
    prop('HutBackBench','SM_PROP_bench_01',221.7,240.95,6.52115,width=2.8,module='jidi_02',collision=True)
    prop('HutSideTable','SM_PROP_table_03',224.7,241.5,6.52115,width=1.45,module='jidi_02',collision=True)
    prop('HutBread','SM_PROP_food_bread_08',224.7,241.5,7.354,scale=1.3,module='jidi_02')
    prop('HutBasket','SM_PROP_basket_01',218.55,241.05,6.52115,width=.8,module='jidi_02')
    prop('HutJug','SM_PROP_bucket_01',218.55,242.0,6.52115,scale=1.2,module='jidi_02')
    prop('HutFrontBench','SM_PROP_bench_01',225.1,247.7,width=2.25,yaw=0,module='jidi_02',collision=True)
    prop('HutFrontBasket','SM_PROP_basket_01',218.65,247.8,width=.8,module='jidi_02')
    # Transport group north/east of the base work area, outside tested route.
    cart,cr=prop('SupplyCart','SM_PROP_cart_01',228.5,262.5,scale=.88,yaw=-18,module='jidi_04',collision=True)
    prop('CartSideBarrel','SM_PROP_barrel_01',230.4,262.5,scale=1.05,module='jidi_04',collision=True)
    prop('CartBagA','SM_PROP_sack_01',230.0,263.7,width=.85,module='jidi_04')
    prop('CartBagB','SM_PROP_sack_03',230.6,263.8,width=.72,yaw=65,module='jidi_04')
    # Ruins: broken structure remains dough; abandoned wooden goods cluster at edges.
    for i,(x,y,angle) in enumerate([(330.0,164.4,35),(330.8,165.6,-30),(336.7,163.9,80),(337.2,168.2,12)]):
        prop('RuinPlanks'+str(i),'SM_PROP_planks_0'+str(i%2+1),x,y,-4.09 if x<337 else None,width=2.0+i*.13,yaw=angle,module='meijun_03')
    prop('RuinShelf','SM_PROP_market_shelf_02',332.6,163.8,-4.09975,width=2.6,module='meijun_03',collision=True)
    prop('RuinOpenBox','SM_PROP_box_02',336.9,166.5,-4.09975,width=1.05,yaw=28,module='meijun_03',collision=True)
    prop('RuinLid','SM_PROP_box_lid_02',336.4,165.65,-4.09975,width=1.05,yaw=28,module='meijun_03')
    prop('RuinBarrel','SM_PROP_barrel_01',330.0,167.8,-4.09975,scale=1.1,module='meijun_03',collision=True)
    prop('RuinWheel','SM_PROP_cart_wheel_small',329.25,166.7,-4.09975,scale=1.8,yaw=10,module='meijun_03')
    prop('RuinBasket','SM_PROP_basket_01',334.0,163.7,-4.09975,width=.8,module='meijun_03')
    prop('RuinCart','SM_PROP_cart_03',340.8,168.2,scale=.85,yaw=115,module='meijun_03',collision=True)
    # Replace flat proxy bridge visuals; original continuous collision deck retained.
    for i in range(17):retire('BridgeDeck_'+str(i),True)
    for i,y in enumerate([164.32,170,175.68]):
        prop('BridgeWoodDeck'+str(i),'SM_PROP_walkway_wood',393.3,y,-3.89109,dimensions=[4.2,5.7,.31],module='meijun_06')
    for i,(x,y) in enumerate([(390.1,179.7),(396.5,160.1)]):
        prop('BridgeSupplyBarrel'+str(i),'SM_PROP_barrel_01',x,y,scale=1.05,module='meijun_06',collision=True)
        prop('BridgeSupplyBag'+str(i),'SM_PROP_sack_05',x+.75,y,scale=1.5,module='meijun_06')
    manifest['objects']+=out['placements'];manifest['village_pass']={'timestamp':out['timestamp'],'asset_root':ROOT,'source_pack_retained':'/Game/Fantastic_Village_Pack','replaced_visual_count':len(out['replaced_visuals']),'new_placement_count':len(out['placements'])}
    (B/'PlacementManifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.EditorLevelLibrary.save_current_level()
    out['saved']=True
except:out['errors'].append(traceback.format_exc())
(P/'BuildReport.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('VILLAGE_BUILD_DONE '+str(len(out['placements']))+' ERRORS '+str(out['errors']))
