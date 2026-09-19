import unreal,json,sys,datetime
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
manifest=json.loads((P/'PlacementManifest.json').read_text(encoding='utf-8'))
actors=sc.A.get_all_level_actors();labels={a.get_actor_label():a for a in actors}
report={'timestamp':datetime.datetime.now().isoformat(),'ceiling_settings':[],'added':[]}
with unreal.ScopedEditorTransaction('Finish bridge terrain contact and underground roof readability'):
    for a in actors:
        if a.get_actor_label().startswith(('DeepCeiling_','DeepHangingLobe_')):
            assert isinstance(a,unreal.DepthEnvironmentActor) and 'DepthBuilt' in [str(t) for t in a.tags]
            f=a.get_component_by_class(unreal.CameraOccluderFadeComponent)
            report['ceiling_settings'].append({'label':a.get_actor_label(),'before_padding_cm':f.bounds_padding_cm,'before_visibility':f.occluded_visibility})
            f.set_editor_property('bounds_padding_cm',650.0);f.set_editor_property('occluded_visibility',.10)
    x,y=393.3,170.;deck=min(sc.h(x,178),sc.h(x,162))+.10;bed=deck-6
    additions=[('BridgeContact_Subfloor',[x,y,bed-.50],[14,16,.15])]
    for side in [-1,1]:
        additions.append(('BridgeContact_Side'+str(side),[x+side*6,y,bed-.35],[2.5,16,6.55]))
        additions.append(('BridgeContact_End'+str(side),[x,y+side*7.3,bed-.35],[14,2.5,6.10]))
    sc.records=[]
    for label,pos,dims in additions:
        if label in labels:continue
        a=sc.piece(label,'/Engine/BasicShapes/Cube',pos,dims,'Mold',True,False,0,'meijun_06','mechanisms/mech_plate_1')
        row=sc.records[-1];row.update(region='meijun',category_cn='桥洞接土地层',source_anchor_ref='mechanisms/mech_plate_1',source_association_kind='original_source_reference',runtime_acceptance_status='final_verification_pending')
        manifest['objects'].append(row);report['added'].append(row)
unreal.EditorLevelLibrary.save_current_level()
(P/'PlacementManifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
(P/'SpatialPolish.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('DEPTH_SPATIAL_POLISH_SAVED '+str(len(report['added'])))
