import unreal,json,sys
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
out={}
land=next(a for a in sc.A.get_all_level_actors() if isinstance(a,unreal.Landscape))
out['nav_gathering_before']=str(land.get_editor_property('navigation_geometry_gathering_mode'))
land.set_editor_property('navigation_geometry_gathering_mode',unreal.NavDataGatheringMode.INSTANT)
for c in land.get_components_by_class(unreal.LandscapeHeightfieldCollisionComponent):
    c.set_editor_property('can_ever_affect_navigation',False);c.set_editor_property('can_ever_affect_navigation',True)
out['lights']=[]
for a in sc.A.get_all_level_actors():
    if isinstance(a,unreal.PointLight) and a.get_actor_label().startswith('Deep_Light'):
        c=a.get_component_by_class(unreal.PointLightComponent);before=c.get_editor_property('intensity');c.set_intensity(2800);out['lights'].append({'actor':a.get_path_name(),'before':before,'after':2800})
    if isinstance(a,unreal.RecastNavMesh):
        for k in ['default_max_search_nodes','heuristic_scale','ledge_slope_filter_mode']:
            try:out[k]=str(a.get_editor_property(k))
            except:pass
unreal.SystemLibrary.execute_console_command(sc.W,'RebuildNavigation');unreal.EditorLevelLibrary.save_current_level()
(P/'FinalPolish.json').write_text(json.dumps(out,indent=2),encoding='utf-8');unreal.log('DEPTH_FINAL_POLISH_DONE')
