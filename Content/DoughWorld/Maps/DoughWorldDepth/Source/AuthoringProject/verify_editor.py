import unreal,json,sys,math,hashlib
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
W=sc.W;A=sc.A;actors=A.get_all_level_actors();land=next(a for a in actors if isinstance(a,unreal.Landscape));others=[a for a in actors if a!=land]
out={'map':W.get_path_name(),'actors':len(actors),'source_positions':[],'terrain':[],'holes':[],'navigation':[],'warnings':[]}
def ray(x,y,ignored=None,top=120,bottom=-120):
    hit=unreal.SystemLibrary.line_trace_single(W,sc.world([x,y,top]),sc.world([x,y,bottom]),unreal.TraceTypeQuery.ECC_CAMERA,True,ignored or [],unreal.DrawDebugTrace.NONE)
    if not hit:return None
    t=hit.to_tuple();return {'z_m':t[4].z/100,'position_cm':[t[4].x,t[4].y,t[4].z],'tuple':str(t)}
for a in actors:
    tags=[str(t) for t in a.tags]
    for tag in tags:
        if tag.startswith('OriginalSource:'):
            ref=tag.split(':',1)[1];expected=sc.anchor(ref);actual=a.get_actor_location();out['source_positions'].append({'source_ref':ref,'xy_error_m':math.hypot(actual.x/100+250-expected[0],actual.y/100+250-expected[1]),'expected_z_m':expected[2],'actual_z_m':actual.z/100,'actor':a.get_path_name()})
for q in sc.source['demo_route']+ [{'pos':r['center'],'name_cn':r['id']} for r in sc.source['regions']]+[{'pos':[123.3,160],'name_cn':'crest'},{'pos':[143.3,201.7],'name_cn':'pool'}]:
    x,y=q['pos'];r=ray(x,y,others);out['terrain'].append({'name':q['name_cn'],'expected_z_m':sc.h(x,y),'actual_z_m':r['z_m'] if r else None,'error_m':abs(r['z_m']-sc.h(x,y)) if r else None})
for q in sc.depth['holes']:
    x,y=q['xy_m'];r=ray(x,y,others);out['holes'].append({'id':q['source_id'],'landscape_has_no_center_collision':r is None,'all_geometry_hit':ray(x,y)})
out['bridge_underpass_center_landscape']=ray(393.3,170,others)
out['underground_floor_samples']=[{'xy':p,'hit':ray(*p,top=-55,bottom=-90)} for p in [(396.7,48.3),(436.7,103.3),(466.7,60),(411.7,109.3),(430,130),(430,34)]]
out['nav_building']=unreal.NavigationSystemV1.is_navigation_being_built(W)
def nav(name,aa,bb,expect=True):
    def project(p):return unreal.NavigationSystemV1.project_point_to_navigation(W,sc.world([p[0],p[1],p[2] if len(p)>2 else sc.h(p[0],p[1])+.8]),None,None,unreal.Vector(350,350,400))
    s=project(aa);e=project(bb);path=unreal.NavigationSystemV1.find_path_to_location_synchronously(W,s,e) if s and e else None
    valid=bool(path and path.is_valid() and not path.is_partial());out['navigation'].append({'name':name,'complete':valid,'expected_complete':expect,'projected_start_cm':[s.x,s.y,s.z] if s else None,'projected_end_cm':[e.x,e.y,e.z] if e else None,'points':[[p.x,p.y,p.z] for p in path.path_points] if path else []})
for aa,bb in zip(sc.source['demo_route'],sc.source['demo_route'][1:]):nav('Demo_'+str(aa['order'])+'_'+str(bb['order']),aa['pos'],bb['pos'])
for name,aa,bb in [('BaseToHut',[221.7,249.5],[221.7,243.3]),('BaseToWorkbench',[221.7,249.5],[221.7,269]),('BaseToStorage',[221.7,249.5],[196.7,260.2]),('BaseToWall',[216.7,253.3],[149,233.3]),('MoldDetour',[360,220],[418.2,146]),('BridgeCross',[393.3,181],[393.3,159]),('DeepEntryToAltar',[411.7,109.3,-59],[436.7,91,-59]),('DeepGuards',[403.3,64,-59],[466.7,64,-59])]:nav(name,aa,bb)
nav('ClosedBreakwall',[149,233.3],[137,233.1],False)
for name,aa,bb in [('FermentEastBypass',[145,200],[123,200]),('FermentWestBypass',[27,202],[66,202]),('YeastNorthBypass',[70,15],[70,45])]:nav(name,aa,bb,False)
present={a.get_path_name() for a in actors};manifest=json.loads((P/'PlacementManifest.json').read_text(encoding='utf-8'));out['manifest_missing_actors']=[o['actor'] for o in manifest['objects'] if o['actor'] not in present]
out['source_original_sha256']=hashlib.sha256((P/'Handoff/Originals/10_document.json').read_bytes()).hexdigest()
(P/'EditorVerification.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8');unreal.log('DEPTH_EDITOR_VERIFIED')
