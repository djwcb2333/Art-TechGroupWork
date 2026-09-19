import sys
import time,json,unreal
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P/'CameraCode'))
import validate_camera_fade_pie as test
state={'start':time.monotonic(),'last':None,'stable':0}
def stable_tick(dt):
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    p=unreal.GameplayStatics.get_player_character(w,0) if w else None
    if p:
        pos=p.get_actor_location();mv=p.get_component_by_class(unreal.CharacterMovementComponent)
        stable=mv.is_moving_on_ground() and state['last'] is not None and (pos-state['last']).length()<.001
        state['stable']=state['stable']+dt if stable else 0.;state['last']=pos
        if state['stable']>=2.0:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            test.run('/Game/Maps/DoughWorldDepth/Materials/MI_Dough',str(P/'CameraFadeRuntime.json'))
            return
    if time.monotonic()-state['start']>25:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        (P/'CameraFadeRuntime.json').write_text(json.dumps({'passed':False,'error':'Player never reached stable ground before camera comparison'}),encoding='utf-8')
state['handle']=unreal.register_slate_post_tick_callback(stable_tick)
