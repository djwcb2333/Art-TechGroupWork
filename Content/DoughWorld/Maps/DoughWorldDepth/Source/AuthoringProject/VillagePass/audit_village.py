import unreal,json,datetime,traceback
from pathlib import Path
P=Path(__file__).parent
root='/Game/Fantastic_Village_Pack'
out={'timestamp':datetime.datetime.now().isoformat(),'root':root,'assets':[],'meshes':[],'materials':[],'missing_dependencies':[],'errors':[]}
try:
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    out['level']=world.get_path_name()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    out['scene_actor_count']=len(actors)
    out['source_anchors']={a.get_actor_label():[a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z] for a in actors if any(str(t).startswith('OriginalSource:') for t in a.tags)}
    ar=unreal.AssetRegistryHelpers.get_asset_registry();assets=ar.get_assets_by_path(root,True)
    opts=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True)
    for data in assets:
        path=str(data.package_name);cls=str(data.asset_class_path.asset_name)
        out['assets'].append({'path':path,'class':cls})
        for dep in ar.get_dependencies(data.package_name,opts):
            dep=str(dep)
            if dep.startswith('/Game/') and not unreal.EditorAssetLibrary.does_asset_exist(dep):out['missing_dependencies'].append({'asset':path,'missing':dep})
        if cls=='StaticMesh':
            mesh=data.get_asset();box=mesh.get_bounding_box()
            slots=[{'name':str(s.material_slot_name),'material':s.material_interface.get_path_name() if s.material_interface else None} for s in mesh.static_materials]
            body=mesh.get_editor_property('body_setup')
            out['meshes'].append({'path':path,'bounds_min_cm':[box.min.x,box.min.y,box.min.z],'bounds_max_cm':[box.max.x,box.max.y,box.max.z],'slots':slots,'collision_trace_flag':str(body.get_editor_property('collision_trace_flag')) if body else None})
        elif cls in ('MaterialInstanceConstant','Material'):
            mat=data.get_asset();row={'path':path,'class':cls}
            if cls=='MaterialInstanceConstant':
                parent=mat.get_editor_property('parent');row['parent']=parent.get_path_name() if parent else None
                row['texture_overrides']=[{'name':str(q.parameter_info.name),'value':q.parameter_value.get_path_name() if q.parameter_value else None} for q in mat.get_editor_property('texture_parameter_values')]
                row['vector_overrides']=[{'name':str(q.parameter_info.name),'value':str(q.parameter_value)} for q in mat.get_editor_property('vector_parameter_values')]
            out['materials'].append(row)
    out['asset_count']=len(assets)
except:out['errors'].append(traceback.format_exc())
(P/'ImportAudit.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('VILLAGE_IMPORT_AUDIT_DONE '+str(len(out['meshes']))+' ERRORS '+str(out['errors']))
