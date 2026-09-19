"""Exclude only this map's raw source/docs from UE's automatic asset importer."""
from pathlib import Path
import hashlib,json
P=Path(__file__).parent
target=Path('E:/Unreal Project/GDATtest/Saved/Config/WindowsEditor/EditorPerProjectUserSettings.ini')
data=target.read_bytes();encoding='utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else ('utf-8-sig' if data.startswith(b'\xef\xbb\xbf') else 'utf-8')
text=data.decode(encoding)
old='AutoReimportDirectorySettings=(SourceDirectory="/Game/",MountPoint="",Wildcards=((Wildcard="Localization/*")))'
new='AutoReimportDirectorySettings=(SourceDirectory="/Game/",MountPoint="",Wildcards=((Wildcard="Localization/*"),(Wildcard="Maps/DoughWorldDepth/Source/*",bInclude=False),(Wildcard="Maps/DoughWorldDepth/Documentation/*",bInclude=False)))'
assert text.count(old)==1 or text.count(new)==1,'Project importer settings changed; inspect before patching'
backup=P/'Backups/EditorPerProjectUserSettings_BeforeSourceExclusions.ini'
if not backup.exists():backup.write_bytes(data)
if old in text:target.write_bytes(text.replace(old,new).encode(encoding))
(P/'SourceImportExclusions.json').write_text(json.dumps({'target':str(target),'backup':str(backup),'excluded':['Maps/DoughWorldDepth/Source/*','Maps/DoughWorldDepth/Documentation/*'],'global_auto_import_preserved':True,'before_sha256':hashlib.sha256(data).hexdigest(),'after_sha256':hashlib.sha256(target.read_bytes()).hexdigest()},indent=2),encoding='utf-8')
