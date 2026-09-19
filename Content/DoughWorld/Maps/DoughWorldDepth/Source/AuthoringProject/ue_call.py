import mcp_local as m, json, sys, pathlib
m.request('initialize',{'protocolVersion':'2025-06-18','capabilities':{},'clientInfo':{'name':'DoughWorldLocalValidation','version':'1.0'}})
m.request('notifications/initialized',ident=None)
if sys.argv[1]=='script':
    params={'toolset_name':'SlateInspectorToolset.SlateInspectorToolset','tool_name':'Type','arguments':{'ref':sys.argv[3] if len(sys.argv)>3 else 'tb3','text':'py "'+str(pathlib.Path(sys.argv[2]).resolve()).replace('\\','/')+'"','submit':True}}
else:
    params={'toolset_name':sys.argv[1],'tool_name':sys.argv[2],'arguments':json.loads(pathlib.Path(sys.argv[3]).read_text(encoding='utf-8-sig')) if len(sys.argv)>3 else {}}
r=m.request('tools/call',{'name':'call_tool','arguments':params},2)
for c in r.get('result',{}).get('content',[]):
    if c.get('type')=='text': print(c['text'])
if r.get('error'): print(json.dumps(r))
