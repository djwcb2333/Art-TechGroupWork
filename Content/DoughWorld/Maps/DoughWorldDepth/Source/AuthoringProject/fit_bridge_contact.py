import unreal,json,sys
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
manifest=json.loads((P/'PlacementManifest.json').read_text(encoding='utf-8'));actors={a.get_path_name():a for a in sc.A.get_all_level_actors()};report=[]
with unreal.ScopedEditorTransaction('Fit bridge retaining backs beneath external terrain'):
    for row in manifest['objects']:
        if not row['label'].startswith(('BridgeContact_Side','BridgeContact_End')):continue
        a=actors[row['actor']];x,y,bottom=row['bottom_xyz_m'];dx,dy,dz=row['dimensions_m']
        # Keep each flat backing below the lowest adjoining soil, avoiding exposed flat strips.
        heights=[sc.h(x+dx*(ix/8-.5),y+dy*(iy/8-.5)) for ix in range(9) for iy in range(9)]
        top=min(heights)-.025;new_height=min(dz,top-bottom)
        assert new_height>3
        loc=a.get_actor_location();loc.z=(bottom+new_height*.5)*100
        a.set_actor_scale3d(unreal.Vector(dx,dy,new_height));a.set_actor_location(loc,False,True)
        row['dimensions_m'][2]=new_height;report.append({'label':row['label'],'old_height':dz,'height':new_height,'top_m':top})
unreal.EditorLevelLibrary.save_current_level()
(P/'PlacementManifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
(P/'BridgeContactFit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

