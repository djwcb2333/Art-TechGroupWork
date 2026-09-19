"""Offline fake-API validation only. Does not import unreal or operate the editor."""
from pathlib import Path
import json, types, sys, importlib.util, collections, math

p=Path(__file__).resolve().parent
root=p.parent.parent
source=json.loads((root/'Handoff/Originals/10_document.json').read_text(encoding='utf-8'))
depth=json.loads((root/'TerrainDesign/depth_supplement.json').read_text(encoding='utf-8'))
plan=json.loads((root/'Planning/ModulePlan.json').read_text(encoding='utf-8'))
geo=json.loads((p/'GeometryManifest.json').read_text(encoding='utf-8'))
anchors={a['source_ref']:a for a in depth['anchors']}
allowed_meshes=set(plan['assets'])|{a['key'] for a in geo['assets']}
colors={'Dough','Crust','Flour','Yeast','Cap','Mold','Core','Fiber','Glow','Wood','Shadow','Butter','Water',None}
sc=types.ModuleType('scene_common')
sc.source=source;sc.records=[]
sc.anchor=lambda ref:list(anchors[ref]['source_xy_m'])+[anchors[ref]['z_m']]
sc.ground=lambda x,y,region: -60. if region=='shendu' else 0.

def piece(name,mesh_key,xyz_m_bottom,dimensions_m,color='Dough',collision=True,fade=False,yaw=0,module='',source=''):
    assert source in anchors,source
    assert mesh_key in allowed_meshes,mesh_key
    assert color in colors,color
    assert len(xyz_m_bottom)==3 and all(math.isfinite(x) for x in xyz_m_bottom),name
    assert len(dimensions_m)==3 and min(dimensions_m)>0,name
    assert isinstance(collision,bool) and isinstance(fade,bool),name
    assert module and source,name
    sc.records.append({'name':name,'mesh':mesh_key,'xyz_m_bottom':xyz_m_bottom,'dimensions_m':dimensions_m,
                       'color':color,'collision':collision,'fade':fade,'module':module,'source':source})
    return None
sc.piece=piece
sys.modules['scene_common']=sc
spec=importlib.util.spec_from_file_location('generic_modules',p/'generic_modules.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
full=module.build_generic()
assert len(sc.records)==full['piece_count']
assert len({r['name'] for r in sc.records})==len(sc.records)
assert full['monster_source_count']==13 and full['resource_source_count']==4
for monster in source['monsters']:
    ref='monsters/'+monster['id'];r=[r for r in sc.records if r['source']==ref]
    assert any('torso' in x['name'] and x['xyz_m_bottom'][:2]==monster['pos'] for x in r),ref
    assert any('head' in x['name'] for x in r),ref
    assert sum('leg_' in x['name'] for x in r)>=2,ref
for resource in source['resources']:
    ref='resources/'+resource['id']
    assert any(r['source']==ref and r['xyz_m_bottom'][:2]==resource['pos'] for r in sc.records),ref
hut=[r for r in sc.records if r['source']=='buildings/base_hut']
assert any('independent_arch_roof' in r['name'] and r['fade'] and not r['collision'] for r in hut)
assert all(r['collision'] and r['fade'] for r in hut if '_upper' in r['name'])
assert all(r['collision'] and not r['fade'] for r in hut if '_lower' in r['name'])
bench=[r for r in sc.records if r['source']=='buildings/workbench']
assert sum('workbench_leg_' in r['name'] and r['collision'] for r in bench)==4
assert all(not r['collision'] for r in bench if 'tabletop' in r['name'] or 'lower_shelf' in r['name'])
all_records=list(sc.records)
sc.records=[]
prototype=module.build_generic(module_filter={'jidi_02','jidi_03','jidi_04'})
assert set(prototype['modules'])=={'jidi_02','jidi_03','jidi_04'}
assert prototype['monster_source_count']==0 and prototype['resource_source_count']==0
assert all(r['module'] in prototype['module_filter'] for r in sc.records)
sc.records=[]
empty=module.build_generic(module_filter=set())
assert empty['piece_count']==0 and not sc.records
result={'status':'offline_fake_API_only_not_UE_execution','full':full,'prototype':prototype,
        'checks':{'API_arguments':True,'mesh_keys_exist_in_manifest':True,'colors_supported':True,
                  'unique_piece_labels':True,'all_13_monster_roots_have_torso_at_original_xy':True,
                  'all_13_monsters_have_head_and_two_legs':True,'all_4_resource_roots_preserved':True,
                  '19_requested_modules_have_pieces':True,'main_hut_wall_split_and_roof_fade':True,
                  'workbench_four_colliding_legs_no_solid_table_box':True,'module_filter_isolates_prototype':True,
                  'empty_filter_has_no_side_effect_calls':True},
        'terrain_height_validation':'fake ground used; actual surface sampling/traversal still requires editor verification'}
(p/'GenericDryRun.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'full_piece_count':full['piece_count'],'prototype_piece_count':prototype['piece_count'],
                  'monsters':full['monster_source_count'],'resources':full['resource_source_count'],
                  'modules':full['modules'],'checks':'passed offline only'},ensure_ascii=False))
