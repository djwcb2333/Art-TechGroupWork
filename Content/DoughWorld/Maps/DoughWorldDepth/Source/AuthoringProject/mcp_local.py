"""Small synchronous client for the project's local Unreal MCP server."""
import json, sys, urllib.request, pathlib
URL = 'http://127.0.0.1:8000/mcp'
session = None
def request(method, params=None, ident=1):
    global session
    payload={'jsonrpc':'2.0','method':method}
    if ident is not None: payload['id']=ident
    if params is not None: payload['params']=params
    headers={'Content-Type':'application/json','Accept':'application/json, text/event-stream'}
    if session: headers['Mcp-Session-Id']=session
    req=urllib.request.Request(URL,json.dumps(payload).encode(),headers)
    with urllib.request.urlopen(req,timeout=90) as res:
        session=res.headers.get('Mcp-Session-Id',session)
        raw=res.read().decode()
    if not raw.strip(): return None
    if raw.lstrip().startswith('{'): return json.loads(raw)
    for line in raw.splitlines():
        if line.startswith('data: '):
            item=json.loads(line[6:])
            if item.get('id')==ident: return item
    return {'raw':raw}
if __name__=='__main__':
    request('initialize',{'protocolVersion':'2025-06-18','capabilities':{},'clientInfo':{'name':'DoughWorldLocalValidation','version':'1.0'}})
    request('notifications/initialized',ident=None)
    if len(sys.argv)>1:
        cmd=json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
        result=request(cmd['method'],cmd.get('params'),2)
    else: result=request('tools/list',{},2)
    if len(sys.argv)>2: pathlib.Path(sys.argv[2]).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    else: print(json.dumps(result,ensure_ascii=False))
