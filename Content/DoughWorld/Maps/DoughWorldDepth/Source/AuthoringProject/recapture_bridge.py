from pathlib import Path
p=Path(__file__).parent
exec(compile((p/'full_runtime.py').read_text(encoding='utf-8').replace("FullRuntime.json","BridgeRuntime.json"),str(p/'full_runtime.py'),'exec'))
targets=[q for q in targets if q[0] in ('11_BridgeDeck','12_BridgeUnder')]
