import unreal,time,json,sys,traceback
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
C=P/'Captures';ctx={'phase':0,'next':0,'busy':False,'fades':[],'before':[],'stable_seconds':0.,'last_camera':None,'start':time.monotonic()}
def finish(error=None):
    for f in ctx['fades']:
        if not f.is_active():f.activate(True)
    unreal.unregister_slate_post_tick_callback(ctx['handle'])
    before=ctx.get('snapshot_before');after=ctx.get('snapshot_after')
    delta=sum((a-b)**2 for a,b in zip(before['location'],after['location']))**.5 if before and after else None
    (P/'SceneFadeComparison.json').write_text(json.dumps({'error':error,'before':before,'after':after,'camera_position_delta_cm':delta,'same_collision':ctx.get('collision_before')==ctx.get('collision_after'),'captures':['19_RoofOpaque.png','20_RoofFaded.png'],'camera_lag_settled_before_first_capture':ctx['stable_seconds']>=1.5},indent=2),encoding='utf-8');unreal.log('DEPTH_SCENE_FADE_COMPARISON_DONE')
def snapshot(p,w):
    cam=unreal.GameplayStatics.get_player_camera_manager(w,0);pos=cam.get_camera_location();r=cam.get_camera_rotation()
    return {'location':[pos.x,pos.y,pos.z],'rotation':[r.pitch,r.yaw,r.roll],'fov':cam.get_fov_angle(),'arm':p.get_component_by_class(unreal.SpringArmComponent).target_arm_length}
def tick(dt):
    if ctx['busy'] or time.monotonic()<ctx['next']:return
    ctx['busy']=True
    try:
        now=time.monotonic();w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();p=unreal.GameplayStatics.get_player_character(w,0)
        if ctx['phase']==0:
            unreal.GameplayStatics.get_player_controller(w,0).stop_movement();p.get_component_by_class(unreal.CharacterMovementComponent).stop_movement_immediately();p.set_actor_location(sc.world([221.7,243.3,sc.h(221.7,243.3)+1.2]),False,True)
            actors=[a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.DepthEnvironmentActor) if 'buildings__base_hut' in a.get_actor_label()]
            ctx['meshes']=[a.get_component_by_class(unreal.StaticMeshComponent) for a in actors];ctx['fades']=[a.get_component_by_class(unreal.CameraOccluderFadeComponent) for a in actors]
            for f in ctx['fades']:f.deactivate()
            ctx['phase']=1;ctx['next']=now+3;return
        if ctx['phase']==1:
            assert now-ctx['start']<70,'Camera lag did not settle for comparison'
            current=snapshot(p,w)['location'];last=ctx['last_camera']
            stable=last is not None and sum((a-b)**2 for a,b in zip(current,last))<.0004
            ctx['stable_seconds']=ctx['stable_seconds']+dt if stable else 0.;ctx['last_camera']=current
            if ctx['stable_seconds']<1.5:return
            ctx['snapshot_before']=snapshot(p,w);ctx['collision_before']=[str(c.get_collision_enabled()) for c in ctx['meshes']]
            unreal.AutomationLibrary.take_high_res_screenshot(1280,720,str(C/'19_RoofOpaque.png'))
            ctx['phase']=2;ctx['next']=now+1.3;return
        if ctx['phase']==2:
            for f in ctx['fades']:f.activate(True)
            ctx['phase']=3;ctx['next']=now+2;return
        ctx['snapshot_after']=snapshot(p,w);ctx['collision_after']=[str(c.get_collision_enabled()) for c in ctx['meshes']]
        assert sum((a-b)**2 for a,b in zip(ctx['snapshot_before']['location'],ctx['snapshot_after']['location']))<.01,'Camera moved after stabilization'
        unreal.AutomationLibrary.take_high_res_screenshot(1280,720,str(C/'20_RoofFaded.png'));finish()
    except:finish(traceback.format_exc())
    finally:ctx['busy']=False
ctx['handle']=unreal.register_slate_post_tick_callback(tick)
