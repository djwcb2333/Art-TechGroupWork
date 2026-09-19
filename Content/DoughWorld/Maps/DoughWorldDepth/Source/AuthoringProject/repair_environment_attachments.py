import unreal,json,sys
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
assert sc.W.get_path_name()=='/Game/Maps/DoughWorldDepth/Levels/L_DoughWorld_Depth.L_DoughWorld_Depth'
bad={d['actor'] for d in json.loads((P/'GhostTransform.json').read_text(encoding='utf-8'))}
manifest={o['actor']:o for o in json.loads((P/'PlacementManifest.json').read_text(encoding='utf-8'))['objects']}
assert len(bad)==47 and bad.issubset(manifest)
out=[]
for a in sc.A.get_all_level_actors():
    if a.get_path_name() not in bad:continue
    assert isinstance(a,unreal.DepthEnvironmentActor) and unreal.Name('DepthBuilt') in a.tags
    root=a.root_component;c=a.get_component_by_class(unreal.StaticMeshComponent)
    assert c.get_attach_parent() is None and c.get_world_location().length()<.01
    assert c.get_editor_property('static_mesh').get_path_name()==manifest[a.get_path_name()]['mesh']
    assert not c.is_simulating_physics(),'Unexpected physics state; no mutation allowed'
    d={'actor':a.get_path_name(),'label':a.get_actor_label(),'was_detached':c.get_attach_parent()!=root,'was_simulating_physics':c.is_simulating_physics(),'root_mobility_before':str(root.mobility),'mesh_mobility_before':str(c.mobility)}
    root.set_mobility(c.mobility)
    ok=c.attach_to_component(root,'None',unreal.AttachmentRule.SNAP_TO_TARGET,unreal.AttachmentRule.SNAP_TO_TARGET,unreal.AttachmentRule.SNAP_TO_TARGET,False)
    c.set_relative_transform(unreal.Transform(),False,True)
    c.set_editor_property('can_ever_affect_navigation',c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION)
    d['attached_after']=c.get_attach_parent()==root;d['transform_error_cm']=(c.get_world_location()-a.get_actor_location()).length();assert d['attached_after'] and d['transform_error_cm']<.01,d
    out.append(d)
unreal.SystemLibrary.execute_console_command(sc.W,'RebuildNavigation')
(P/'EnvironmentAttachmentRepair.json').write_text(json.dumps(out,indent=2),encoding='utf-8');unreal.log('DEPTH_ATTACHMENTS_REPAIRED '+str(len(out)))
