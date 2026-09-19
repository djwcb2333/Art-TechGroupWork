import unreal,json,sys,math,datetime
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P.parent));import scene_common as sc
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
manifest=json.loads((P.parent/'PlacementManifest.json').read_text(encoding='utf-8'));report=json.loads((P/'BuildReport.json').read_text(encoding='utf-8'))
actors=sc.A.get_all_level_actors();by={a.get_actor_label():a for a in actors};rows={q['label']:q for q in manifest['objects'] if q['label'].startswith('FV__')}
out={'timestamp':datetime.datetime.now().isoformat(),'actor_count':len(actors),'village_count':len(rows),'errors':[],'transforms':[],'hidden_proxy_checks':[],'source_anchor_max_delta_cm':0.0}
original=json.loads((P/'ImportAudit.json').read_text(encoding='utf-8'))['source_anchors']
for label,position in original.items():
    v=by[label].get_actor_location();delta=(v-unreal.Vector(*position)).length();out['source_anchor_max_delta_cm']=max(out['source_anchor_max_delta_cm'],delta)
for label,r in rows.items():
    a=by[label];c=a.get_component_by_class(unreal.StaticMeshComponent);b=c.static_mesh.get_bounding_box();s=a.get_actor_scale3d();yaw=math.radians(r['yaw']);cx=(b.min.x+b.max.x)*.5*s.x;cy=(b.min.y+b.max.y)*.5*s.y;v=a.get_actor_location();actual=[(v.x+cx*math.cos(yaw)-cy*math.sin(yaw))/100+250,(v.y+cx*math.sin(yaw)+cy*math.cos(yaw))/100+250,(v.z+b.min.z*s.z)/100]
    delta=math.dist(actual,r['bottom_xyz_m']);scale_expected=[r['dimensions_m'][i]*100/q for i,q in enumerate([b.max.x-b.min.x,b.max.y-b.min.y,b.max.z-b.min.z])]
    scale_delta=math.dist([s.x,s.y,s.z],scale_expected)
    out['transforms'].append({'label':label,'bottom_error_m':delta,'scale_error':scale_delta})
    if delta>.001 or scale_delta>.001:out['errors'].append('Transform mismatch '+label)
    if c.static_mesh.get_path_name()!=r['mesh']:out['errors'].append('Mesh mismatch '+label)
    if str(c.get_collision_enabled())!=r['collision']:out['errors'].append('Collision mismatch '+label)
for r in report['replaced_visuals']:
    c=by[r['label']].get_component_by_class(unreal.StaticMeshComponent);hidden=not c.is_visible() and c.get_editor_property('hidden_in_game');blocked=c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION
    ok=hidden and blocked==r['retained_collision'];out['hidden_proxy_checks'].append({'label':r['label'],'passed':ok})
    if not ok:out['errors'].append('Proxy state mismatch '+r['label'])
out['passed']=not out['errors'] and out['village_count']==60 and len(original)==50 and out['source_anchor_max_delta_cm']<.01
(P/'VillageVerification.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8');unreal.log('VILLAGE_VERIFIED '+str(out['passed']))
