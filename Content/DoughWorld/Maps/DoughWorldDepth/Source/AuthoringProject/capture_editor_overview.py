import mcp_local as m,json,base64
from pathlib import Path
m.request('initialize',{'protocolVersion':'2025-06-18','capabilities':{},'clientInfo':{'name':'DoughWorldOverview','version':'1.0'}});m.request('notifications/initialized',ident=None)
r=m.request('tools/call',{'name':'call_tool','arguments':{'toolset_name':'EditorToolset.EditorAppToolset','tool_name':'CaptureViewport','arguments':{'captureTransform':{'location':{'x':0,'y':16800,'z':36000},'rotation':{'pitch':-65,'yaw':-90,'roll':0}},'annotations':{'gridSpacing':0,'gridExtent':0,'gridHeight':0,'maxLabelDistance':0,'classFilter':{'refPath':'/Script/Engine.Actor'},'maxLabels':0},'bShowUI':False}}},2)
v=json.loads(r['result']['content'][0]['text'])['returnValue']
Path(__file__).with_name('Captures').joinpath('21_EditorOverview.png').write_bytes(base64.b64decode(v['image']['data']))
v.pop('image');Path(__file__).with_name('EditorOverview.json').write_text(json.dumps(v,indent=2),encoding='utf-8')
