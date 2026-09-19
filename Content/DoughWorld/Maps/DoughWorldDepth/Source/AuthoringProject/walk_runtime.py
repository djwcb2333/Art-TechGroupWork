import unreal,json,sys,time,traceback,math
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
targets=[('Collect1_Flour',200,236),('Return1_Hut',221.7,249.5),('Collect2_Flour',200,236),('Return2_Workbench',221.7,269),('Collect3_Flour',200,236),('Return3_Storage',196.7,260.2)]
ctx={'i':0,'phase':0,'next':0,'busy':False,'legs':[],'samples':[],'start':time.monotonic()}
def save(error=None):
    unreal.unregister_slate_post_tick_callback(ctx['handle']);(P/'WalkRuntime.json').write_text(json.dumps({'error':error,'scope':'actual locomotion between original flour proxy and three base facilities; collection inventory not implemented','legs':ctx['legs']},indent=2),encoding='utf-8');unreal.log('DEPTH_WALK_DONE '+str(error))
def tick(dt):
    if ctx['busy']:return
    ctx['busy']=True
    try:
        now=time.monotonic()
        if now<ctx['next']:return
        w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();p=unreal.GameplayStatics.get_player_character(w,0) if w else None
        if not p:return
        pc=unreal.GameplayStatics.get_player_controller(w,0);move=p.get_component_by_class(unreal.CharacterMovementComponent)
        if ctx['i']>=len(targets):save();return
        name,x,y=targets[ctx['i']]
        if ctx['phase']==0:
            if ctx['i']==0:
                pc.stop_movement();move.stop_movement_immediately();p.set_actor_location(sc.world([221.7,249.5,sc.h(221.7,249.5)+1.1]),False,True);ctx['phase']=1;ctx['next']=now+1;return
            ctx['phase']=1
        if ctx['phase']==1:
            goal=unreal.NavigationSystemV1.project_point_to_navigation(w,sc.world([x,y,sc.h(x,y)+.8]),None,None,unreal.Vector(150,150,200));ctx['goal']=goal;unreal.AIHelperLibrary.simple_move_to_location(pc,goal);ctx['leg_start']=now;ctx['samples']=[];ctx['phase']=2;ctx['next']=now+.4;return
        pos=p.get_actor_location();goal=ctx['goal'];distance=math.hypot(pos.x-goal.x,pos.y-goal.y);arm=p.get_component_by_class(unreal.SpringArmComponent);cam=unreal.GameplayStatics.get_player_camera_manager(w,0)
        ctx['samples'].append({'xyz_cm':[pos.x,pos.y,pos.z],'grounded':move.is_moving_on_ground(),'fov':cam.get_fov_angle(),'arm':arm.target_arm_length})
        if distance<120 or now-ctx['leg_start']>32:
            ctx['legs'].append({'name':name,'reached':distance<120,'remaining_cm':distance,'seconds':now-ctx['leg_start'],'samples':ctx['samples']});pc.stop_movement();ctx['i']+=1;ctx['phase']=0;ctx['next']=now+.4
        else:ctx['next']=now+.4
    except:save(traceback.format_exc())
    finally:ctx['busy']=False
ctx['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.log('DEPTH_WALK_ARMED')
