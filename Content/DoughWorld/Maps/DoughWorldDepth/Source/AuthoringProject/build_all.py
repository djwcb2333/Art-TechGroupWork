from pathlib import Path
P=Path(__file__).parent
for script in ['finish_liquid_material.py','build_depth_scene.py','TerrainDesign/populate_low_foliage.py']:
    f=P/script
    exec(compile(f.read_text(encoding='utf-8'),str(f),'exec'),{'__file__':str(f),'__name__':'__main__'})
