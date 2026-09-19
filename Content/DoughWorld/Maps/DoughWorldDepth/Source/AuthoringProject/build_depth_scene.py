import unreal,json,sys,math,random
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
from scene_common import A,W,ROOT,depth,terrain,source,plan,h,ground,world,anchor,piece,point,records
rng=random.Random(91026)
CUBE='/Engine/BasicShapes/Cube';BREAD='SM_DW_BreadBrick';RING='SM_DW_PoreRing';SLEEVE='SM_DW_PoreInnerSleeve';POST='SM_DW_SupportPost';PLANK='SM_DW_BridgePlank'
# Reruns remove only objects created by this builder, inside the candidate level.
for a in A.get_all_level_actors():
    if 'DepthBuilt' in [str(t) for t in a.tags]:A.destroy_actor(a)
sc.records.clear();sc.roots.clear()
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
# Local cut faces follow exposed terrain instead of surrounding every playable area with identical rocks.
for mod,segments,col in [('jidi_01',[(184,243,188,266),(214,287,239,290),(267,282,281,268)],'Dough'),('qianceng_01',[(275,364,288,349),(157,311,162,332)],'Flour'),('meijun_01',[(323,187,325,201),(414,234,425,222)],'Mold')]:
    for i,(x,y,xx,yy) in enumerate(segments):
        z=min(h(x,y),h(xx,yy));strip('CutFace_'+mod+str(i),(x,y),(xx,yy),2.2,3.0,col,mod,base=z-2.6,mesh='cliff_half');strip('CutLip_'+mod+str(i),(x,y),(xx,yy),2.7,.45,'Crust' if col!='Mold' else 'Core',mod,base=z-.2,mesh='shelf')
# Closed natural boundary circuits, exactly two original gate openings on ferment and one on yeast.
boundary_records=[]
for bd in depth['boundaries']:
    mod='fajiao_08' if bd['region']=='fajiao' else 'jiaomu_07';points=bd['polyline_m'];count=0
    for p,q in zip(points,points[1:]):
        length=math.dist(p,q);steps=max(1,math.ceil(length/5));u=[(q[0]-p[0])/length,(q[1]-p[1])/length]
        for i in range(steps):
            t0=i/steps;t1=(i+1)/steps;aa=[p[j]+(q[j]-p[j])*t0 for j in range(2)];bb=[p[j]+(q[j]-p[j])*t1 for j in range(2)];mid=[(aa[j]+bb[j])/2 for j in range(2)]
            # Exclude complete end segments meeting the exact original gate point. Jambs cover the transition.
            if any(min(math.dist(aa,g['xy']),math.dist(bb,g['xy']),math.dist(mid,g['xy']))<g['width_m']/2+.5 for g in bd['gaps']):continue
            side_samples=[h(mid[0]+v*u[1],mid[1]-v*u[0]) for v in [-5,0,5]];lo=min(side_samples)-2;hi=max(side_samples)+5
            yaw=math.degrees(math.atan2(u[1],u[0]));dims=[length/steps+.6,3.8,hi-lo]
            a=pc('Boundary_'+bd['region']+'_'+str(count),'cliff_half',[*mid,lo],dims,'Dough' if bd['region']=='fajiao' else 'Yeast',True,False,yaw,mod)
            collision=solid('BoundaryCollision_'+bd['region']+'_'+str(count),[*mid,lo],dims,mod);collision.set_actor_rotation(unreal.Rotator(yaw=yaw),False)
            if count%3==0:pc('BoundaryRoots_'+bd['region']+'_'+str(count),'roots',[*mid,hi-2],[4,3,2],'Fiber',False,False,yaw,mod)
            boundary_records.append({'actor':collision.get_path_name(),'region':bd['region'],'a':aa,'b':bb,'base':lo,'top':hi});count+=1
# Gate blockers and jambs. These are visual collision proxies; faction mechanics remain integration work.
for id,width,mod in [('mech_wall_1',5.5,'fajiao_07'),('mech_gap_sentry',1.5,'jiaomu_06')]:
    xyz=anchor('mechanisms/'+id);x,y,z=xyz;g=next(g for b in depth['boundaries'] for g in b['gaps'] if g['id']==id);tx,ty=g['tangent_xy'];yaw=math.degrees(math.atan2(ty,tx))
    for s in [-1,1]:
        pp=[x+s*tx*(width/2+2.2),y+s*ty*(width/2+2.2),z-.5];pc(id+'_Jamb'+str(s),BREAD,pp,[4.5,5,6],'Yeast',True,False,yaw,mod,'mechanisms/'+id)
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
    strip('ButterShelf'+str(i),(ax,ay),(sx,sy),8,.17,'Butter','fajiao_05','landmarks/butter_waterfall',top+.07,CUBE)
    for k in range(5):pc('ButterImpact_'+str(i)+'_'+str(k),'SM_DW_Bud',[ex+(k-2)*uy*1.4,ey-(k-2)*ux*1.4,bottom+.05],[1.8,1.5,.35],'Flour',False,False,0,'fajiao_05')
pc('ButterPool','SM_DW_Bud',[*pool,.50],[17,12,.23],'Butter',False,False,0,'fajiao_06','landmarks/butter_waterfall/pool_pos')
for j in range(10):
    y=207+j*2.55;z=.58-(y-201.7)/31.6*.45
    pc('ButterStream_'+str(j),CUBE,[143.3,y,z],[3.4,3,.10],'Butter',False,False,0,'fajiao_06','landmarks/butter_waterfall/stream_to')
    for side in [-1,1]:pc('StreamBank'+str(j)+'_'+str(side),'rock',[143.3+side*2.2,y,z-.2],[1.2,3,.55],'Crust',False,False,0,'fajiao_06')
# Actual Landscape visibility apertures, with hollow sleeves, permanent rims and lower collision floors.
for hole in depth['holes']:
    sid=hole['source_id'];x,y=hole['xy_m'];z=hole['rim_z_m'];r=hole['radius_m'];bottom=hole['inner_floor_z_m'];mod={'hole_small_1':'qianceng_06','hole_vent_1':'qianceng_04','hole_collapse_1':'meijun_02','hole_abyss':'meijun_07'}[sid];color='Mold' if mod.startswith('meijun') else 'Dough'
    pc(sid+'_HollowInnerWall',SLEEVE,[x,y,bottom],[r*2+.8,r*2+.8,z-bottom+.3],color,True,False,0,mod,'holes/'+sid)
    pc(sid+'_PermanentRim',RING,[x,y,z-.25],[r*2+1.7,r*2+1.7,.65],color,True,False,0,mod,'holes/'+sid)
    pc(sid+'_LowerFloor','SM_DW_AltarDish',[x,y,bottom-.2],[r*2,r*2,.3],'Shadow',True,False,0,mod,'holes/'+sid)
    if sid=='hole_abyss':
        # Tall visible perimeter crust encloses the rim: entering is explicit confirmation, never falling.
        pc('Abyss_SafetyRim',RING,[x,y,z+.2],[r*2+1.7,r*2+1.7,2.1],'Core',True,False,0,mod,'holes/'+sid)
    elif sid=='hole_vent_1':
        for i in range(5):pc('BreathingFold'+str(i),BREAD,[x+(i-2)*.85,y,z-.85],[.88,4,.3],'Dough',False,False,8*(i-2),mod,'holes/'+sid)
# Adjacent small pore cluster, separate from the original single shortcut source.
for i,(x,y,r) in enumerate([(164.3,295,3.2),(168.8,292,1.25)]):
    z=h(x,y);pc('SmallPore_Rim'+str(i),RING,[x,y,z-.1],[r*2,r*1.65,.55],'Dough',True,False,0,'qianceng_03');pc('SmallPore_Inner'+str(i),SLEEVE,[x,y,z-2.3],[r*1.7,r*1.4,2.4],'Crust',True,False,0,'qianceng_03');pc('SmallPore_Bed'+str(i),'SM_DW_AltarDish',[x,y,z-2.3],[r*1.5,r*1.2,.15],'Shadow',True,False,0,'qianceng_03')
# Side tunnel volume attached to the optional pore. An exit is not invented in source data.
x,y,z=anchor('holes/hole_small_1')
for side in [-1,1]:pc('TunnelSide'+str(side),BREAD,[x+side*1.6,y+4,z-2.1],[.5,8,2.4],'Dough',True,False,0,'qianceng_06','mechanisms/mech_hole_shortcut')
pc('TunnelWalkFloor',PLANK,[x,y+4,z-2.25],[3.3,8,.2],'Crust',True,False,0,'qianceng_06','mechanisms/mech_hole_shortcut');pc('TunnelRoof','SM_DW_ArcRoof',[x,y+4,z+.1],[4,8,1.2],'Dough',False,True,0,'qianceng_06','mechanisms/mech_hole_shortcut')
# Bridge, its bed 6m below the deck, and visibly supported abutments; the east detour remains open.
x,y,z=anchor('mechanisms/mech_plate_1');bridgez=min(h(x,y-2),h(x,y-18))+.10;bed=bridgez-6
pc('BridgeUnderpassFloor',CUBE,[x,y-10,bed-.35],[10.5,12.5,.35],'Core',True,False,0,'meijun_06','mechanisms/mech_plate_1')
for side in [-1,1]:pc('BridgeUnderpassWall'+str(side),BREAD,[x+side*5.1,y-10,bed],[.8,12,6],'Mold',True,False,0,'meijun_06')
for j in range(17):pc('BridgeDeck_'+str(j),PLANK,[x,y-2-j,bridgez],[4.2,1.06,.28],'Wood',True,False,0,'meijun_06','mechanisms/mech_plate_1')
for side in [-1,1]:
    pc('BridgeBeam'+str(side),POST,[x+side*1.7,y-10,bridgez-.55],[.45,17,.55],'Crust',True,False,0,'meijun_06')
    for end in [-5,5]:pc('BridgePier'+str(side)+str(end),POST,[x+side*1.7,y-10+end,bed],[.65,.65,bridgez-bed],'Wood',True,False,0,'meijun_06')
for dy in [-.5,-19.5]:pc('BridgeAbutment'+str(dy),PLANK,[x,y+dy,bridgez-.06],[5.5,3.2,.25],'Crust',True,False,0,'meijun_06')
# Actual underground chamber: continuous floor, edge trench, vertical walls and independent roof pieces.
cx,cy=436.7,83.3;floor=-60.0
pc('DeepFloor_ContinuousCollision',CUBE,[cx,cy,floor-.5],[100,90,.5],'Core',True,False,0,'shendu_02')
for side in [-1,1]:
    for k in range(10):
        xx=cx-45+k*10;yy=cy+side*47
        pc('DeepTrenchNS'+str(side)+'_'+str(k),CUBE,[xx,yy,-63.5],[10.1,6.2,.5],'Shadow',True,False,0,'shendu_06')
        pc('DeepWallNS'+str(side)+'_'+str(k),'cliff_half',[xx,cy+side*52,-63],[10.5,4,7],'Mold',True,False,0,'shendu_04')
        pc('DeepUpperNS'+str(side)+'_'+str(k),'layered',[xx,cy+side*52,-56],[10.5,4,11],'Mold',True,True,0,'shendu_04')
    for k in range(9):
        xx=cx+side*52;yy=cy-40+k*10
        pc('DeepWallEW'+str(side)+'_'+str(k),'cliff_half',[xx,yy,-63],[4,10.5,7],'Mold',True,False,0,'shendu_04')
        pc('DeepUpperEW'+str(side)+'_'+str(k),'layered',[xx,yy,-56],[4,10.5,11],'Mold',True,True,0,'shendu_04')
for i in range(9):
    for j in range(8):
        xx=cx-48+i*12;yy=cy-42+j*12
        pc('DeepCeiling_'+str(i)+'_'+str(j),BREAD,[xx,yy,-48],[12.2,12.2,2.8],'Mold',False,True,0,'shendu_05')
        if (i+j)%5==0:pc('DeepHangingLobe_'+str(i)+'_'+str(j),'SM_DW_GlutenColumn',[xx+3,yy,-52],[2.2,2.2,4.3],'Mold',False,True,0,'shendu_05')
# A recessed wall niche and small low lights establish depth while retaining a readable play floor.
for side in [-1,1]:pc('AltarNicheJamb'+str(side),BREAD,[cx+side*5,cy-44,-60],[2,3,7],'Mold',True,False,0,'shendu_04')
pc('AltarNicheLintel',BREAD,[cx,cy-44,-53],[12,3,2],'Mold',True,True,0,'shendu_04')
for i,(xx,yy) in enumerate([(cx-30,cy-27),(cx+28,cy-27),(cx,cy),(cx-25,cy+26),(cx+25,cy+25)]):
    light=A.spawn_actor_from_class(unreal.PointLight,world([xx,yy,-53]),unreal.Rotator());light.set_actor_label('Deep_Light'+str(i));light.set_folder_path('Depth/shendu/Lighting');light.set_editor_property('tags',[unreal.Name('DepthBuilt')]);lc=light.get_component_by_class(unreal.PointLightComponent);lc.set_intensity(38000);lc.set_attenuation_radius(3500);lc.set_light_color(unreal.LinearColor(.56,.78,.61,1));lc.set_cast_shadows(False)
landing=[cx-25,cy+26,-60]
pc('DeepLanding',PLANK,[landing[0],landing[1],-60.02],[10,9,.08],'Mold',True,False,0,'shendu_01')
pc('DeepLanding_BackRock','cliff_half',[landing[0]-7,landing[1]+4,-60],[7,3,8],'Mold',True,True,25,'shendu_01')
dest=A.spawn_actor_from_class(unreal.TargetPoint,world([landing[0],landing[1],-58.95]),unreal.Rotator());dest.set_actor_label('Depth_ConfirmedEntryDestination');dest.set_editor_property('tags',[unreal.Name('DepthBuilt')]);dest.set_folder_path('Depth/shendu/Entry')
portal=A.spawn_actor_from_class(unreal.load_class(None,'/Script/GDATtest.DepthAbyssPortalProxy'),world(anchor('holes/hole_abyss')),unreal.Rotator());portal.set_actor_label('Depth_Abyss_ExplicitConfirmation');portal.set_editor_property('tags',[unreal.Name('DepthBuilt')]);portal.set_folder_path('Depth/meijun/meijun_08');portal.set_editor_property('underground_destination_actor',dest);portal.set_editor_property('underground_destination',dest.get_actor_location());portal.set_editor_property('destination_configured',True);portal.set_editor_property('interaction_radius_cm',1350)
pc('AbyssSafeStandingLip',PLANK,[418.2,146,h(418.2,146)-.03],[5,5,.10],'Mold',True,False,0,'meijun_08','holes/hole_abyss')
# Generic authored modules are separate, source-anchored constructions.
sys.path.insert(0,str(P/'Planning/Geometry'));from generic_modules import build_generic
build_generic()
focus=A.spawn_actor_from_class(unreal.load_class(None,'/Script/GDATtest.DepthInteractionFocusActor'),unreal.Vector(),unreal.Rotator());focus.set_actor_label('Depth_ActualBuildingResourceVisibilityFocus');focus.set_folder_path('Depth/Integration');focus.set_editor_property('tags',[unreal.Name('DepthBuilt')]);focus.set_editor_property('protected_interaction_anchors',[a for ref,a in sc.roots.items() if ref.startswith(('buildings/','resources/'))])
# Route-only modules reference the actual imported Landscape; no floating square pads are generated.
land=next(a for a in A.get_all_level_actors() if isinstance(a,unreal.Landscape))
for mod in ['jidi_01','jidi_06','qianceng_01','qianceng_07','jiaomu_01','meijun_01','meijun_09']:
    records.append({'actor':land.get_path_name(),'label':land.get_actor_label(),'module':mod,'source_ref':'original_ground_layout','mesh':None,'geometry_type':'externally_imported_continuous_landscape','validation':'requires_runtime_navigation_and_visibility'})
unreal.EditorLevelLibrary.save_current_level();sc.save_report()
(P/'BoundaryManifest.json').write_text(json.dumps(boundary_records,indent=2),encoding='utf-8')
(P/'DepthOverrides.json').write_text(json.dumps({'source_xy_changes':0,'heightmap':str(P/'TerrainDesign/DoughWorldDepth_505.r16'),'deep_floor_z_m':-60,'deep_entry_additive_xyz_m':landing,'portal_confirmed_destination_cm':[dest.get_actor_location().x,dest.get_actor_location().y,dest.get_actor_location().z],'camera_change':False},indent=2),encoding='utf-8')
unreal.SystemLibrary.execute_console_command(W,'RebuildNavigation');unreal.log('DEPTH_SCENE_BUILT '+str(len(records)))
