import unreal,json,sys,datetime,hashlib
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert sc.W.get_path_name()=='/Game/Maps/DoughWorldDepth/Levels/L_DoughWorld_Depth.L_DoughWorld_Depth'
actors=sc.A.get_all_level_actors();by_path={a.get_path_name():a for a in actors}
manifest=json.loads((P/'PlacementManifest.json').read_text(encoding='utf-8'))
out={'timestamp':datetime.datetime.now().isoformat(),'world':sc.W.get_path_name(),'actor_count':len(actors),'placement_records':len(manifest['objects']),'missing_actors':[],'mesh_mismatch':[],'material_problems':[],'environment_problems':[],'foliage':[],'roof_settings':[]}
for row in manifest['objects']:
    a=by_path.get(row['actor'])
    if not a:out['missing_actors'].append(row['label']);continue
    if not row.get('mesh'):continue
    c=a.get_component_by_class(unreal.StaticMeshComponent)
    if not c or not c.static_mesh or c.static_mesh.get_path_name()!=row['mesh']:out['mesh_mismatch'].append(row['label']);continue
    for slot in range(c.get_num_materials()):
        mat=c.get_material(slot)
        if not mat or isinstance(mat,unreal.MaterialInstanceDynamic):out['material_problems'].append({'actor':row['label'],'slot':slot,'material':str(mat)})
for a in actors:
    if isinstance(a,unreal.DepthEnvironmentActor):
        c=a.get_component_by_class(unreal.StaticMeshComponent)
        if c.get_attach_parent()!=a.root_component or (c.get_world_location()-a.get_actor_location()).length()>.01:out['environment_problems'].append(a.get_actor_label())
        if a.get_actor_label().startswith(('DeepCeiling_','DeepHangingLobe_')):
            f=a.get_component_by_class(unreal.CameraOccluderFadeComponent);out['roof_settings'].append({'label':a.get_actor_label(),'padding_cm':f.bounds_padding_cm,'visibility':f.occluded_visibility})
    if isinstance(a,unreal.InstancedFoliageActor):
        for c in a.get_components_by_class(unreal.FoliageInstancedStaticMeshComponent):
            mat=c.get_material(0)
            if mat and mat.get_path_name().startswith('/Game/Maps/DoughWorldDepth/Foliage/'):
                out['foliage'].append({'component':c.get_path_name(),'count':c.get_instance_count(),'collision':str(c.get_collision_enabled()),'affects_navigation':c.get_editor_property('can_ever_affect_navigation'),'material':mat.get_path_name()})
out['foliage_total']=sum(q['count'] for q in out['foliage'])
out['focus_anchors']=[q.get_actor_label() for a in actors if isinstance(a,unreal.DepthInteractionFocusActor) for q in a.get_editor_property('protected_interaction_anchors') if q]
out['noninteractive_ruin_excluded']=not any('ruined_settlement' in q for q in out['focus_anchors'])
out['navigation_building']=unreal.NavigationSystemV1.is_navigation_being_built(sc.W)
out['saved_current_level']=unreal.EditorLevelLibrary.save_current_level()
out['passed']=all(not out[k] for k in ['missing_actors','mesh_mismatch','material_problems','environment_problems']) and out['foliage_total']==6600 and not out['navigation_building'] and bool(out['saved_current_level']) and out['noninteractive_ruin_excluded']
(P/'FinalPersistence.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('DEPTH_FINAL_PERSISTENCE '+str(out['passed']))
