import unreal,json,sys,math,datetime
from pathlib import Path
P=Path(__file__).parent;B=P.parent;sys.path.insert(0,str(B));import scene_common as sc
sc.records=[]
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
manifest=json.loads((B/'PlacementManifest.json').read_text(encoding='utf-8'));report=json.loads((P/'BuildReport.json').read_text(encoding='utf-8'))
rows={q['label']:q for q in manifest['objects']};actors=sc.A.get_all_level_actors();by={a.get_actor_label():a for a in actors}
probes=json.loads((P/'SupportProbes.json').read_text(encoding='utf-8'));changes=[]
def pose(label,z,ratio=1):
    r=rows[label];a=by[label];c=a.get_component_by_class(unreal.StaticMeshComponent);box=c.static_mesh.get_bounding_box();mn=box.min;mx=box.max
    before=r['bottom_xyz_m'][2];r['bottom_xyz_m'][2]=z;r['dimensions_m']=[s*ratio for s in r['dimensions_m']];d=r['dimensions_m'];scale=[d[i]*100/s for i,s in enumerate([mx.x-mn.x,mx.y-mn.y,mx.z-mn.z])];yaw=math.radians(r['yaw']);cx=(mn.x+mx.x)*.5*scale[0];cy=(mn.y+mx.y)*.5*scale[1]
    v=sc.world(r['bottom_xyz_m']);v.x-=cx*math.cos(yaw)-cy*math.sin(yaw);v.y-=cx*math.sin(yaw)+cy*math.cos(yaw);v.z-=mn.z*scale[2]
    a.set_actor_scale3d(unreal.Vector(*scale));a.set_actor_location(v,False,True);changes.append({'label':label,'previous_bottom_z_m':before,'final_bottom_z_m':z,'scale_ratio':ratio})
for i in range(4):pose('FV__RackFlour'+str(i),probes[i]['hit_z']+.003)
for i in range(3):pose('FV__RackBreadBasket'+str(i),probes[4+i]['hit_z']+.003);pose('FV__StorageBox'+str(i),6.39,.88/.94)
pose('FV__HutBread',probes[8]['hit_z']+.003)
assert not any('CartLoad' in k for k in by)
info={q['path'].rsplit('/',1)[-1]:q for q in json.loads((P/'ImportAudit.json').read_text(encoding='utf-8'))['meshes']}
for i,(x,y,name,width) in enumerate([(228.5,262.5,'SM_PROP_sack_01',.8)]):
    # Probe the exact cart floor at every load position before placing it.
    hit=unreal.SystemLibrary.line_trace_single(sc.W,sc.world([x,y,10]),sc.world([x,y,6]),unreal.TraceTypeQuery.ECC_CAMERA,True,[a for a in actors if a!=by['FV__SupplyCart']],unreal.DrawDebugTrace.NONE)
    assert hit,(x,y)
    z=hit.to_tuple()[4].z/100+.005;q=info[name];size=[(q['bounds_max_cm'][j]-q['bounds_min_cm'][j])/100 for j in range(3)];dims=[s*width/size[0] for s in size]
    a=sc.piece('FV__CartLoad'+str(i),sc.ROOT+'/Village/Meshes/'+name,[x,y,z],dims,None,False,False,10+i*25,'jidi_04','buildings/storage_repair');a.set_folder_path('Depth/Village/jidi_04');a.tags=list(a.tags)+[unreal.Name('VillagePass')]
    r=sc.records[-1];r.update({'art_pass':'FantasticVillage_20260913','source_asset':q['path'],'materials':[a.static_mesh_component.get_material(j).get_path_name() for j in range(a.static_mesh_component.get_num_materials())]})
manifest['objects']+=sc.records;rows={q['label']:q for q in manifest['objects']};report['placements']=[rows[q['label']] for q in report['placements']]+sc.records;report['support_refinements']=changes;report['final_timestamp']=datetime.datetime.now().isoformat();manifest['village_pass']['new_placement_count']=len(report['placements'])
unreal.EditorLevelLibrary.save_current_level()
(B/'PlacementManifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8');(P/'BuildReport.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('VILLAGE_SUPPORT_REFINED '+str(len(changes)))
