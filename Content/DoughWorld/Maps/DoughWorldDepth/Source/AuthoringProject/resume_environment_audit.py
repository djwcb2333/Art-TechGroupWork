import unreal,json,sys,datetime
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
assert sc.W.get_path_name().startswith('/Game/Maps/DoughWorldDepth/Levels/L_DoughWorld_Depth.')
rows=[]
for a in sc.A.get_all_level_actors():
    if not isinstance(a,unreal.DepthEnvironmentActor):continue
    c=a.get_component_by_class(unreal.StaticMeshComponent);t=c.get_world_transform();at=a.get_actor_transform()
    rows.append(dict(label=a.get_actor_label(),actor=a.get_path_name(),attached=c.get_attach_parent()==a.root_component,root_mobility=str(a.root_component.mobility),mesh_mobility=str(c.mobility),position_error_cm=(t.translation-at.translation).length(),scale_error=(t.scale3d-at.scale3d).length(),rotation_error=abs(t.rotation.angular_distance(at.rotation)),mesh=c.static_mesh.get_path_name() if c.static_mesh else None))
bad=[r for r in rows if not r['attached'] or r['position_error_cm']>.01 or r['scale_error']>.01 or r['rotation_error']>.001]
(P/'ResumeEnvironmentAudit.json').write_text(json.dumps(dict(timestamp=datetime.datetime.now().isoformat(),level=sc.W.get_path_name(),count=len(rows),bad=bad,rows=rows),indent=2),encoding='utf-8')
unreal.log('DEPTH_RESUME_AUDIT '+str(len(rows))+' BAD '+str(len(bad)))
