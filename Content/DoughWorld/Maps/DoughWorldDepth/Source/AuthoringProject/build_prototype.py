import unreal,json,sys,math,random
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
from scene_common import A,W,ROOT,depth,terrain,source,plan,h,ground,world,anchor,piece,point,records
rng=random.Random(91026)
CUBE='/Engine/BasicShapes/Cube';BREAD='SM_DW_BreadBrick';RING='SM_DW_PoreRing';SLEEVE='SM_DW_PoreInnerSleeve';POST='SM_DW_SupportPost';PLANK='SM_DW_BridgePlank'
# Reruns remove only objects created by this builder, inside the candidate level.
for a in A.get_all_level_actors():
    if 'DepthBuilt' in [str(t) for t in a.tags]:A.destroy_actor(a)
for ref,a in sc.byref.items():
    if not ref.startswith('regions/'):point(ref)
def pc(n,k,p,d,c='Dough',col=True,fade=False,yaw=0,mod='',src=''):return piece(n,k,p,d,c,col,fade,yaw,mod,src)
def solid(n,p,d,mod,src='',visible=False):
    a=pc(n,CUBE,p,d,'Crust',True,False,0,mod,src)
    if not visible:a.set_actor_hidden_in_game(True);a.get_component_by_class(unreal.StaticMeshComponent).set_visibility(False,True)
    return a
def strip(n,p,q,width,height,col,mod,src='',base=None,mesh='rock',fade=False):
    dx=q[0]-p[0];dy=q[1]-p[1];cx=(p[0]+q[0])/2;cy=(p[1]+q[1])/2;z=h(cx,cy) if base is None else base
    return pc(n,mesh,[cx,cy,z],[math.hypot(dx,dy),width,height],col,True,fade,math.degrees(math.atan2(dy,dx)),mod,src)
# Gate blockers and jambs. These are visual collision proxies; faction mechanics remain integration work.
for id,width,mod in [('mech_wall_1',5.5,'fajiao_07'),('mech_gap_sentry',1.5,'jiaomu_06')]:
    xyz=anchor('mechanisms/'+id);x,y,z=xyz;g=next(g for b in depth['boundaries'] for g in b['gaps'] if g['id']==id);tx,ty=g['tangent_xy'];yaw=math.degrees(math.atan2(ty,tx))
    for s in [-1,1]:
        pp=[x+s*tx*(width/2+2.2),y+s*ty*(width/2+2.2),z-.5];pc(id+'_Jamb'+str(s),'layered',pp,[4.5,5,6],'Yeast',True,False,yaw,mod,'mechanisms/'+id)
        # Exact rectangular collision fills porous rock gaps without changing the visible doorway.
        a=solid(id+'_JambCollision'+str(s),pp,[4.5,5,7],mod,'mechanisms/'+id);a.set_actor_rotation(unreal.Rotator(yaw=yaw),False)
    if id=='mech_wall_1':
        for row in range(3):
            for col in range(3):
                d=(col-1)*1.85;pc('Breakwall_Block_'+str(row)+str(col),BREAD,[x+tx*d,y+ty*d,z+row*1.2],[1.92,1.3,1.25],'Crust' if row==0 else 'Dough',True,False,yaw,mod,'mechanisms/'+id)
        a=solid('Breakwall_ClosedCollision',[x,y,z],[5.6,1.3,4],mod,'mechanisms/'+id);a.set_actor_rotation(unreal.Rotator(yaw=yaw),False)
    else:
        for side in [-1,1]:
            xx=x+tx*side*4.3;yy=y+ty*side*4.3
            pc('SentryBody'+str(side),'SM_DW_Bud',[xx,yy,z],[1,1,1.8],'Yeast',True,False,0,mod,'mechanisms/'+id);pc('SentryCap'+str(side),'SM_DW_MushroomCap_Pleated',[xx,yy,z+1.5],[1.6,1.5,.6],'Cap',False,False,0,mod,'mechanisms/'+id)
# Three short vertical falls linked by level upper shelves, then a real pool and shallow stream.
wf=depth['waterfall'];crest=[123.3,160];pool=[143.3,201.7];vx=pool[0]-crest[0];vy=pool[1]-crest[1];yaw=math.degrees(math.atan2(vy,vx));ux=wf['flow_direction_xy'][0];uy=wf['flow_direction_xy'][1]
for i,(start,end) in enumerate(zip(wf['fall_face_start_t'],wf['fall_face_end_t'])):
    sx=crest[0]+vx*start;sy=crest[1]+vy*start;ex=crest[0]+vx*end;ey=crest[1]+vy*end;top=wf['shelves'][i]['z_m'];bottom=wf['shelves'][i+1]['z_m']
    # Cliff face dimension X spans across flow; front of mesh kept behind actual liquid drop.
    pc('ButterFall_Cliff'+str(i),'cliff_half',[sx-ux*.9,sy-uy*.9,bottom-.6],[15,3.5,top-bottom+.6],'Dough',True,False,yaw+90,'fajiao_04','landmarks/butter_waterfall')
    pc('ButterFall_LiquidVolume'+str(i),BREAD,[(sx+ex)/2,(sy+ey)/2,bottom+.1],[8.5,math.dist((sx,sy),(ex,ey))+1,top-bottom+.25],'Butter',False,False,yaw+90,'fajiao_05','landmarks/butter_waterfall')
    pc('ButterFall_FlowMesh'+str(i),'waterfall',[ex+ux*.2,ey+uy*.2,bottom],[8.5,1.1,top-bottom+.3],'Butter',False,False,yaw+90,'fajiao_05','landmarks/butter_waterfall')
    prior=0 if i==0 else wf['fall_face_end_t'][i-1];ax=crest[0]+vx*prior;ay=crest[1]+vy*prior
    strip('ButterShelf'+str(i),(ax,ay),(sx,sy),8,.17,'Butter','fajiao_05','landmarks/butter_waterfall',top+.07,BREAD)
    for k in range(5):pc('ButterImpact_'+str(i)+'_'+str(k),'SM_DW_Bud',[ex+(k-2)*uy*1.4,ey-(k-2)*ux*1.4,bottom+.05],[1.8,1.5,.35],'Flour',False,False,0,'fajiao_05')
pc('ButterPool','SM_DW_Bud',[*pool,.50],[17,12,.23],'Butter',False,False,0,'fajiao_06','landmarks/butter_waterfall/pool_pos')
for j in range(10):
    y=207+j*2.55;z=.58-(y-201.7)/31.6*.45
    pc('ButterStream_'+str(j),BREAD,[143.3,y,z],[3.4,3,.10],'Butter',False,False,0,'fajiao_06','landmarks/butter_waterfall/stream_to')
    for side in [-1,1]:pc('StreamBank'+str(j)+'_'+str(side),'rock',[143.3+side*2.2,y,z-.2],[1.2,3,.55],'Crust',False,False,0,'fajiao_06')

sys.path.insert(0,str(P/'Planning/Geometry'));from generic_modules import build_generic
build_generic(module_filter={'jidi_02','jidi_03','jidi_04'})
unreal.EditorLevelLibrary.save_current_level();sc.save_report()
unreal.log('DEPTH_PROTOTYPE_READY')
