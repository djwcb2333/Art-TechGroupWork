"""Serial local MCP test driver; each UE callback completes before the next starts."""
import json,time,re,sys,traceback
from pathlib import Path
import mcp_local as m
P=Path(__file__).parent
def connect():
    m.request('initialize',{'protocolVersion':'2025-06-18','capabilities':{},'clientInfo':{'name':'DoughWorldFinalValidation','version':'1.0'}})
    m.request('notifications/initialized',ident=None)
def call(toolset,name,args=None):
    r=m.request('tools/call',{'name':'call_tool','arguments':{'toolset_name':toolset,'tool_name':name,'arguments':args or {}}},2)
    if r.get('error') or r.get('result',{}).get('isError'):raise RuntimeError(str(r))
    text=r['result']['content'][0]['text'];o=json.loads(text)
    if 'returnValue' not in o:raise RuntimeError(text)
    return o['returnValue']
def script(name):
    print('START '+name,flush=True)
    assert call('SlateInspectorToolset.SlateInspectorToolset','Type',{'ref':ref,'text':'py "'+str((P/name).resolve()).replace('\\','/')+'"','submit':True})
def wait_file(name,previous,timeout=180):
    path=P/name;start=time.monotonic()
    while time.monotonic()-start<timeout:
        if path.exists() and path.stat().st_mtime_ns>previous:
            try:return json.loads(path.read_text(encoding='utf-8-sig'))
            except (ValueError,PermissionError):pass
        time.sleep(1)
    raise RuntimeError('Timed out: '+name)
connect();app='EditorToolset.EditorAppToolset'
assert not call(app,'IsPIERunning'), 'Finish current PIE before final run'
snapshot=call('SlateInspectorToolset.SlateInspectorToolset','Snapshot',{'ref':'','maxDepth':40})
assert 'L_DoughWorld_Depth' in snapshot
ref=re.search(r'textbox (?:\[focused\] )?\[pos=[^\n]+\[ref=(tb\d+)\]',snapshot).group(1)
sequence=[];pie=False
try:
    tests=[('full_runtime.py','FullRuntime.json'),('actions_lifecycle_runtime.py','ActionsLifecycleRuntime.json'),('run_fade_validation.py','CameraFadeRuntime.json'),('fade_scene_comparison.py','SceneFadeComparison.json'),('portal_runtime.py','PortalRuntime.json'),('verify_holes_game_collision.py','HoleGameCollision_PIE.json'),('walk_runtime.py','WalkRuntime.json')]
    if len(sys.argv)>1:
        tests=tests[2:] if sys.argv[1]=='remaining' else ([('roll_collision_runtime.py','RollCollisionRuntime.json'),('recapture_bridge.py','BridgeRuntime.json')] if sys.argv[1]=='finishing' else ([tests[3]] if sys.argv[1]=='comparison' else [tests[1]]))
        sequence=json.loads((P/'FinalValidationSequence.json').read_text(encoding='utf-8'))
        call(app,'StartPIE',{'options':{'bSimulate':False,'playMode':'PlayMode_InViewPort','warmupSeconds':2.0}});pie=True
    for i,(src,dst) in enumerate(tests):
        prior=(P/dst).stat().st_mtime_ns if (P/dst).exists() else 0
        script(src)
        if i==0 and not pie:call(app,'StartPIE',{'options':{'bSimulate':False,'playMode':'PlayMode_InViewPort','warmupSeconds':.5}});pie=True
        result=wait_file(dst,prior)
        row={'script':src,'result_file':dst,'modified_utc_epoch':(P/dst).stat().st_mtime,'error':result.get('error'),'passed':result.get('passed',result.get('all_passed'))}
        sequence=[q for q in sequence if q['script']!=src];sequence.append(row);(P/'FinalValidationSequence.json').write_text(json.dumps(sequence,indent=2),encoding='utf-8')
        print('DONE '+json.dumps(row),flush=True)
        if result.get('error') or result.get('passed') is False or result.get('all_passed') is False:raise RuntimeError('Validation failed: '+dst)
    print('ALL_FINAL_TEST_SCRIPTS_COMPLETED',flush=True)
finally:
    if pie:call(app,'StopPIE')

