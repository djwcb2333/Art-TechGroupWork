import unreal,json,sys,time,traceback,math
from pathlib import Path
P=Path(__file__).parent.parent;sys.path.insert(0,str(P));import scene_common as sc
C=P/'VillagePass/Captures';C.mkdir(exist_ok=True)
bridgez=min(sc.h(393.3,178),sc.h(393.3,162))+.10
targets=[('01_BaseEntrance',221.7,249.5,None),('02_BaseInterior',221.7,243.3,None),('03_Workbench',221.7,269.0,None),('04_Storage',196.7,260.2,None),('05_SupplyCart',228.5,266.3,None),('06_Ruins',333.3,169,None),('07_BridgeDeck',393.3,170,bridgez+1.1),('08_BridgeUnder',393.3,170,bridgez-6+1.1)]
ctx={'phase':0,'index':0,'next':0,'samples':[],'start':time.monotonic(),'busy':False}
def done(error=None):
    unreal.unregister_slate_post_tick_callback(ctx['handle']);(P/'VillagePass/Runtime.json').write_text(json.dumps({'error':error,'samples':ctx['samples']},indent=2),encoding='utf-8');unreal.log('DEPTH_FULL_RUNTIME_DONE '+str(error))
def tick(dt):
    if ctx['busy']:return
    ctx['busy']=True
    try:
        now=time.monotonic()
        if now<ctx['next']:return
        w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();pawn=unreal.GameplayStatics.get_player_character(w,0) if w else None
        if not pawn:
            if now-ctx['start']>120:done('No PIE player')
            return
        if ctx['index']>=len(targets):done();return
        name,x,y,z=targets[ctx['index']];pc=unreal.GameplayStatics.get_player_controller(w,0);move=pawn.get_component_by_class(unreal.CharacterMovementComponent)
        if ctx['phase']==0:
            pc.stop_movement();move.stop_movement_immediately();pawn.set_actor_location(sc.world([x,y,z if z is not None else sc.h(x,y)+1.12]),False,True);ctx['phase']=1;ctx['next']=now+3;return
        cam=unreal.GameplayStatics.get_player_camera_manager(w,0);arm=pawn.get_component_by_class(unreal.SpringArmComponent);pos=pawn.get_actor_location();fades=[]
        for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.DepthEnvironmentActor):
            for s in a.get_component_by_class(unreal.CameraOccluderFadeComponent).get_fade_status():
                if s.current_fade<.99:fades.append({'actor':a.get_name(),'fade':s.current_fade,'collision':str(s.mesh.get_collision_enabled())})
        filename=str(C/(name+'.png'));unreal.AutomationLibrary.take_high_res_screenshot(1280,720,filename)
        focus=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.DepthInteractionFocusActor)[0]
        actual_focus=focus.get_editor_property('current_focus')
        ctx['samples'].append({'capture':filename,'player_xyz_m':[pos.x/100+250,pos.y/100+250,pos.z/100],'grounded':move.is_moving_on_ground(),'camera_rotation':[cam.get_camera_rotation().pitch,cam.get_camera_rotation().yaw,cam.get_camera_rotation().roll],'arm_length':arm.target_arm_length,'fov':cam.get_fov_angle(),'fading_components':fades,'interaction_focus':actual_focus.get_name() if actual_focus else None})
        ctx['index']+=1;ctx['phase']=0;ctx['next']=now+1.3
    except:done(traceback.format_exc())
    finally:ctx['busy']=False
ctx['handle']=unreal.register_slate_post_tick_callback(tick)
unreal.log('DEPTH_FULL_RUNTIME_ARMED')
