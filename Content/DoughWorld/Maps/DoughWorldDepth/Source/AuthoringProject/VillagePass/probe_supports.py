import unreal,json,sys
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P.parent));import scene_common as sc
actors=sc.A.get_all_level_actors();by={a.get_actor_label():a for a in actors};out=[]
for target,x,y,top,bottom in [('FV__StorageRack',195.3,256.8,10,6),('FV__StorageRack',196.25,256.8,10,6),('FV__StorageRack',197.25,256.8,10,6),('FV__StorageRack',198.1,256.8,10,6),('FV__StorageRack',195.7,256.7,8,6),('FV__StorageRack',196.65,256.7,8,6),('FV__StorageRack',197.55,256.7,8,6),('FV__SupplyCart',228.5,262.5,10,6),('FV__HutSideTable',224.7,241.5,9,6)]:
    hit=unreal.SystemLibrary.line_trace_single(sc.W,sc.world([x,y,top]),sc.world([x,y,bottom]),unreal.TraceTypeQuery.ECC_CAMERA,True,[a for a in actors if a!=by[target]],unreal.DrawDebugTrace.NONE)
    out.append({'target':target,'xy':[x,y],'start_z':top,'hit_z':hit.to_tuple()[4].z/100 if hit else None})
(P/'SupportProbes.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
unreal.log('VILLAGE_SUPPORT_PROBES_DONE')
