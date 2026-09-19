"""Compose candidate modules through the root-owned scene_common API.

Importing this file creates nothing. Call build_generic() once after sc.records is
initialized. All coordinates passed to piece() are meters at the mesh bottom.
Original source roots stay at their source XY; component offsets are additive.
"""
from pathlib import Path
import sys
import math
from collections import Counter

ROOT = Path(__file__).resolve().parent.parent.parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
import scene_common as sc

BREAD='SM_DW_BreadBrick'
CAP='SM_DW_MushroomCap_Pleated'
STEM='SM_DW_MushroomStem'
BUD='SM_DW_Bud'
FIBER='SM_DW_GlutenColumn'
RING='SM_DW_PoreRing'
QUARTER='SM_DW_PoreQuarterRing'
ROOF='SM_DW_ArcRoof'
TILE='SM_DW_RoofTile'
PLANK='SM_DW_BridgePlank'
POST='SM_DW_SupportPost'
ALTAR='SM_DW_AltarDish'

_keys=set()
_counts=Counter()
_sources=Counter()
_character_sources=set()
_resource_sources=set()
_module_filter=None

def _name(module, source, suffix):
    return ('DWG__'+module+'__'+source.replace('/','__')+'__'+suffix).replace(' ','_')

def emit(module, source, suffix, mesh, offset, dims, color='Dough', collision=False,
         fade=False, yaw=0, terrain=False, region=None):
    """offset[2] is a bottom offset, never a mesh-center offset."""
    if _module_filter is not None and module not in _module_filter:
        return None
    name=_name(module,source,suffix)
    if name in _keys:
        raise ValueError('Duplicate generic piece key: '+name)
    _keys.add(name)
    a=sc.anchor(source)
    xyz=[a[0]+offset[0],a[1]+offset[1],a[2]+offset[2]]
    if terrain:
        xyz[2]=sc.ground(xyz[0],xyz[1],region)+offset[2]
    assert len(dims)==3 and min(dims)>0,(name,dims)
    actor=sc.piece(name,mesh,xyz,list(dims),color=color,collision=collision,
                   fade=fade,yaw=yaw,module=module,source=source)
    _counts[module]+=1
    _sources[source]+=1
    return actor

def wall(module,source,key,offset,dims,color='Dough',upper_fade=True):
    """Separate persistent low wall from upper decorative occluder."""
    x,y,z=offset;w,d,h=dims
    low=min(1.05,h)
    emit(module,source,key+'_lower',BREAD,(x,y,z),(w,d,low),color,True)
    if h>low:
        emit(module,source,key+'_upper',BREAD,(x,y,z+low),(w,d,h-low),color,True,upper_fade)

def bench(module,source,key,offset=(0,0,0),width=2.8,depth=1.35,height=1.1):
    x,y,z=offset
    # Bench is an interaction-volume proxy: no single solid tabletop collision
    # covering the source point. The four separated posts retain collision.
    emit(module,source,key+'_tabletop',PLANK,(x,y,z+height-.16),(width,depth,.16),'Wood',False)
    for i,(dx,dy) in enumerate([(-1,-1),(-1,1),(1,-1),(1,1)]):
        emit(module,source,key+'_leg_'+str(i),POST,
             (x+dx*(width/2-.24),y+dy*(depth/2-.2),z),(.15,.15,height-.12),'Wood',True)
    emit(module,source,key+'_lower_shelf',PLANK,(x,y,z+.27),(width-.38,depth-.2,.1),'Crust',False)
    return height

def mushroom(module,source,key,offset,stem_dims,cap_dims,cap_bottom,color='Cap',
             terrain=True,region=None,small=False):
    x,y,z=offset
    emit(module,source,key+'_stem',STEM,(x,y,z),stem_dims,'Dough',not small,
         terrain=terrain,region=region)
    emit(module,source,key+'_independent_cap',CAP,(x,y,z+cap_bottom),cap_dims,color,
         False,not small,terrain=terrain,region=region)

def build_base():
    m='jidi_02';s='buildings/base_hut'
    emit(m,s,'floor',BREAD,(0,0,0),(9,7,.15),'Dough',True)
    wall(m,s,'west',(-4.25,0,.15),(.45,6.7,2.8),'Crust')
    wall(m,s,'east',(4.25,0,.15),(.45,6.7,2.8),'Crust')
    wall(m,s,'back',(0,-3.25,.15),(8.5,.45,2.8),'Dough')
    wall(m,s,'front_left',(-2.85,3.25,.15),(2.8,.45,2.8),'Dough')
    wall(m,s,'front_right',(2.85,3.25,.15),(2.8,.45,2.8),'Dough')
    for i,x in enumerate([-1.4,1.4]):
        emit(m,s,'door_post_'+str(i),POST,(x,3.25,.15),(.25,.38,2.85),'Wood',True)
    emit(m,s,'door_header',PLANK,(0,3.25,2.75),(3.05,.5,.3),'Wood',True)
    emit(m,s,'independent_arch_roof',ROOF,(0,0,2.95),(10,8,2.4),'Crust',False,True)
    emit(m,s,'entry_threshold',BREAD,(0,3.7,0),(2.45,1.9,.13),'Dough',True)

    m='jidi_03';s='buildings/workbench'
    bench(m,s,'workbench')
    emit(m,s,'mixing_bowl',ALTAR,(-.65,0,1.1),(.72,.66,.13),'Crust')
    emit(m,s,'flour_bud',BUD,(-.65,0,1.18),(.4,.35,.12),'Flour')
    emit(m,s,'rolling_pin',POST,(.65,0,1.11),(.13,.6,.13),'Wood')

    m='jidi_04';s='buildings/storage_repair'
    for i,(x,y) in enumerate([(-1.9,-.6),(-1.9,.6),(1.9,-.6),(1.9,.6)]):
        emit(m,s,'rack_post_'+str(i),POST,(x,y,0),(.16,.16,2.6),'Wood',True)
    for i,z in enumerate([.12,1.04,1.96]):
        emit(m,s,'rack_shelf_'+str(i),PLANK,(0,0,z),(4.1,1.5,.14),'Wood',True)
    for i,(x,z) in enumerate([(-1.23,.26),(0,1.18),(1.23,.26)]):
        # Actual open-sided crate construction: bottom, four sides and separate lid.
        emit(m,s,'crate_'+str(i)+'_bottom',BREAD,(x,0,z),(1.02,1.0,.12),'Crust',True)
        for j,(dx,dy,w,d) in enumerate([(-.47,0,.12,1),( .47,0,.12,1),(0,-.44,.82,.12),(0,.44,.82,.12)]):
            emit(m,s,'crate_'+str(i)+'_wall_'+str(j),BREAD,(x+dx,dy,z+.12),(w,d,.55),'Wood',True)
        emit(m,s,'crate_'+str(i)+'_independent_lid',PLANK,(x,0,z+.69),(1.05,1.03,.12),'Crust',True)
    emit(m,s,'rack_separate_canopy',ROOF,(0,0,2.66),(4.8,2.6,.7),'Crust',False,True)
    bench(m,s,'repair_table',(3.5,0,0),2.4,1.2,1.05)
    emit(m,s,'repair_tray',ALTAR,(3.5,0,1.05),(.8,.6,.1),'Crust')

    m='jidi_05';s='regions/jidi'
    for i,(x,y) in enumerate([(-12,-24),(25,9),(20,-23),(-6,33)]):
        emit(m,s,'border_grass_'+str(i),'grass',(x,y,0),(2.2,1.7,.26),None,
             terrain=True,region='jidi')
        emit(m,s,'border_flower_'+str(i),'desert_flower',(x+1.4,y-.8,0),(1.1,1,.36),None,
             terrain=True,region='jidi')
        emit(m,s,'border_bush_'+str(i),'bush',(x-1.3,y+.5,0),(1.2,1.1,.58),None,
             terrain=True,region='jidi')

def build_shallow():
    m='qianceng_02'
    for s,count in [('flora/gluten_whiskers_1',8),('flora/gluten_whiskers_2',4)]:
        for i in range(count):
            a=math.tau*i/count;r=.4+.19*(i%3)
            x=math.cos(a)*r;y=math.sin(a)*r
            emit(m,s,'strand_'+str(i),FIBER,(x,y,0),(.18+.035*(i%2),.2,1.4+.21*(i%3)),
                 'Fiber',False,yaw=math.degrees(a),terrain=True,region='qianceng')
        for i,(x,y) in enumerate([(1.9,1),(-1.7,-.8),(1.1,-1.7)]):
            emit(m,s,'edge_grass_'+str(i),'grass',(x,y,0),(1.15,1,.35),None,
                 terrain=True,region='qianceng')

    m='qianceng_05';s='demo_route/3'
    # The common scaler requires nonzero mesh thickness. Use shaped solid liquid
    # surface instead of zero-height pack WaterPlane, retaining the Water material.
    emit(m,s,'water_source_surface',BREAD,(0,0,.015),(8.5,6.5,.10),'Water',False)
    for i,(x,y,w,d) in enumerate([(-4.4,0,1.8,5.5),(4.4,0,1.8,5.5),(0,-3.5,7,1.6),(0,3.5,7,1.6)]):
        emit(m,s,'wet_shore_'+str(i),BREAD,(x,y,-.06),(w,d,.2),'Crust',False,
             terrain=True,region='qianceng')
    for i,(x,y) in enumerate([(-4,2.5),(4,-2.3),(-3.2,-3.1)]):
        emit(m,s,'shore_reeds_'+str(i),'weed',(x,y,0),(1.6,1.2,.75),None,
             terrain=True,region='qianceng')
        emit(m,s,'shore_pebbles_'+str(i),'clump',(x+1,y+.8,0),(1.6,1.1,.4),'Dough',
             terrain=True,region='qianceng')

def build_ferment():
    m='fajiao_01';s='regions/fajiao'
    for i,(x,y,w,d,h) in enumerate([(-37,-18,10,8,3.1),(-35,-5,7,6,2.3),(-42,12,5.5,4.5,1.8)]):
        emit(m,s,'inflated_dough_'+str(i),BREAD,(x,y,-.4),(w,d,h),'Dough',True,
             yaw=-18+23*i,terrain=True,region='fajiao')
        emit(m,s,'small_companion_'+str(i),BUD,(x+w*.45,y+1,-.1),(2.3,2,1.25),'Yeast',False,
             terrain=True,region='fajiao')
        emit(m,s,'sparse_edge_'+str(i),'grass',(x-w*.5,y-2,0),(2,1.6,.32),None,
             terrain=True,region='fajiao')

    m='fajiao_02';s='flora/yeast_buds_fajiao'
    mushroom(m,s,'main',(0,0,0),(.9,.9,3.2),(4.8,4.2,1.5),2.7,'Yeast',region='fajiao')
    mushroom(m,s,'young',(3.4,1.3,0),(.35,.35,1.2),(1.8,1.55,.7),1.,'Cap',region='fajiao',small=True)
    mushroom(m,s,'companion',(-3.7,-2.4,0),(.55,.55,2),(3.1,2.7,.95),1.7,'Yeast',region='fajiao')
    for i,(x,y) in enumerate([(-1.6,2),(2.7,-2.1),(-4.2,.6)]):
        emit(m,s,'bud_'+str(i),BUD,(x,y,0),(.75,.7,.5),'Yeast',terrain=True,region='fajiao')

    m='fajiao_03';s='regions/fajiao'
    # Decorative recess collars only; major cut-wall/terrain is root-owned.
    for i,(x,y,w,h) in enumerate([(-31,24,2.8,1.25),(-28,25.5,1.8,.9),(-32,28.5,1.5,.75)]):
        emit(m,s,'porous_cut_collar_'+str(i),RING,(x,y,-.7),(w,w*.8,h),'Crust',False,
             terrain=True,region='fajiao')
        emit(m,s,'pore_shadow_recess_'+str(i),BREAD,(x,y,-.72),(w*.58,w*.46,.07),'Shadow',False,
             terrain=True,region='fajiao')
    emit(m,s,'broken_cut_lip',QUARTER,(-34,25,-1),(3.5,2.4,2),'Dough',False,
         terrain=True,region='fajiao')

def build_yeast():
    s='regions/jiaomu';m='jiaomu_02'
    for key,xy,st,cp,cb in [
        ('dominant',(-18,-16),(2,2,7),(11,10,3),6),
        ('companion',(12,-10),(1.3,1.3,4.8),(7,6.3,2.2),4.1),
        ('southern',(-4,18),(1.6,1.6,5.5),(8.5,7.5,2.5),4.7),
    ]:
        mushroom(m,s,key,(*xy,0),st,cp,cb,'Cap',region='jiaomu')
        emit(m,s,key+'_roots','roots',(xy[0],xy[1],0),(4.8,4,.6),'Fiber',False,
             terrain=True,region='jiaomu')
        mushroom(m,s,key+'_young',(xy[0]-5,xy[1]+3,0),(.4,.4,1.55),(2.2,1.9,.8),1.25,
                 'Yeast',region='jiaomu',small=True)

    # Separate offset specimen provides its own component references for jiaomu_03;
    # it does not duplicate any jiaomu_02 root or count a label as geometry.
    m='jiaomu_03'
    mushroom(m,s,'independent_parts',(22,-31,0),(1.4,1.4,5.5),(8,7,2.3),4.8,
             'Cap',region='jiaomu')
    emit(m,s,'independent_root_flare','roots',(22,-31,0),(3.3,2.7,.8),'Fiber',False,
         terrain=True,region='jiaomu')

    m='jiaomu_04';s='resources/special_yeast_strain'
    for i,(x,y,zscale) in enumerate([(-6,-5,1),(-4.1,-7,.75),(-8,-3.5,.58)]):
        mushroom(m,s,'young_'+str(i),(x,y,0),(.3*zscale,.3*zscale,1.2*zscale),
                 (1.65*zscale,1.5*zscale,.7*zscale),1.0*zscale,'Yeast',region='jiaomu',small=True)
        emit(m,s,'tiny_bud_'+str(i),BUD,(x+1,y+.5,0),(.45,.4,.35),'Yeast',terrain=True,region='jiaomu')

    m='jiaomu_05'
    emit(m,s,'low_background_ledge','shelf',(-4,-2,-.1),(3.5,2.8,.55),'Dough',False,
         terrain=True,region='jiaomu')
    mushroom(m,s,'background_fungus',(-5.5,1.5,0),(.45,.45,1.8),(2.6,2.3,1),1.5,
             'Cap',region='jiaomu')
    emit(m,s,'sparse_resource_foreground','grass',(3.2,2.7,0),(1.8,1.4,.25),None,
         terrain=True,region='jiaomu')

def build_corruption():
    m='meijun_03';s='buildings/ruined_settlement'
    emit(m,s,'ruin_floor',BREAD,(0,0,-.1),(11,9,.3),'Crust',True)
    wall(m,s,'west_fragment',(-4.7,-1,.2),(.7,5.8,3.1),'Mold')
    wall(m,s,'north_fragment',(-1.7,-4,.2),(6.8,.7,2.8),'Mold')
    wall(m,s,'east_low_remnant',(4.7,-2,.2),(.7,4,1.2),'Crust',False)
    for i,x in enumerate([-1.55,1.55]):
        emit(m,s,'ruined_doorpost_'+str(i),POST,(x,4,.2),(.45,.5,2.7),'Crust',True)
    emit(m,s,'ruined_doorheader',PLANK,(0,4,2.72),(3.6,.65,.5),'Crust',True)
    emit(m,s,'broken_roof_half',TILE,(-2.8,-1.7,3.15),(4.6,4,.9),'Mold',False,True,yaw=8)
    for i,(x,y,w) in enumerate([(4,3,2.5),(6,-3,3),(-6,2,2)]):
        emit(m,s,'off_floor_rubble_'+str(i),'clump',(x,y,0),(w,w*.7,.8),'Crust',True,
             terrain=True,region='meijun')

    m='meijun_04'
    for s in ['flora/mycelium_meijun_1','flora/mycelium_meijun_2']:
        emit(m,s,'main_network','roots',(0,0,0),(5.7,4.6,.7),'Mold',False,
             terrain=True,region='meijun')
        emit(m,s,'second_network','roots',(3.1,1.7,0),(3.3,2.9,.38),'Fiber',False,yaw=63,
             terrain=True,region='meijun')
        for i,(x,y) in enumerate([(-2,1),(1,-2),(3,-1)]):
            emit(m,s,'fungal_nodule_'+str(i),BUD,(x,y,0),(.55,.5,.28),'Mold',False,
                 terrain=True,region='meijun')

    m='meijun_05';s='mechanisms/mech_plate_1'
    emit(m,s,'plate_base',BREAD,(0,0,0),(2.65,2.65,.18),'Crust',True)
    emit(m,s,'plate_moving_top_proxy',PLANK,(0,0,.18),(2.25,2.25,.12),'Wood',True)
    emit(m,s,'dry_block_proxy',BREAD,(-3.2,0,0),(1.8,1.8,1.8),'Crust',True)
    for i,x in enumerate([-.55,0,.55]):
        emit(m,s,'block_crust_ridge_'+str(i),BREAD,(-3.2+x,-.82,.85),(.15,.12,.6),'Dough')
    emit(m,s,'plate_indicator',BUD,(0,.75,.3),(.28,.28,.12),'Yeast')

def character(source,region,kind='mold',module=None,boss=False):
    """Stationary character-volume proxy, no AI, inventory or combat claims."""
    if source in _character_sources:raise ValueError('Character source repeated: '+source)
    _character_sources.add(source)
    module=module or region+'_CharacterProxies'
    if boss:
        # Altar dish has central floor at +0.496 m; source root remains unchanged.
        base=.53
        for i,x in enumerate([-1.05,1.05]):
            emit(module,source,'proxy_core_leg_'+str(i),STEM,(x,0,base),(.65,.7,1.05),'Mold',True)
        emit(module,source,'proxy_core_torso',BUD,(0,0,base+.8),(4.2,3.8,3.15),'Core',True)
        emit(module,source,'proxy_core_head',BUD,(0,-.4,base+3.35),(2,1.8,1.2),'Mold',True)
        for i,(x,y,z) in enumerate([(-1.7,0,1.7),(1.65,.1,1.9),(-.9,1.4,2.1),(1,1.3,2.4)]):
            emit(module,source,'proxy_core_shell_lobe_'+str(i),BUD,(x,y,base+z),(1.4,1.15,1.5),'Mold')
        for i,(x,z) in enumerate([(-.6,2.05),(.55,2.5),(0,3.75)]):
            emit(module,source,'proxy_core_glow_'+str(i),BUD,(x,-1.68,base+z),(.45,.45,.4),'Glow')
        emit(module,source,'proxy_core_tendrils','roots',(0,0,base),(5.7,5,.5),'Mold')
        return
    color={'dough':'Dough','yeast':'Yeast','mold':'Mold','guard':'Core','ambush':'Mold','spore':'Core'}[kind]
    guard=kind=='guard'
    scale=1.3 if guard else (1.15 if kind=='spore' else 1.)
    for i,x in enumerate([-.23,.23]):
        emit(module,source,'proxy_leg_'+str(i),STEM,(x*scale,0,0),(.24*scale,.3*scale,.5*scale),color,True)
    emit(module,source,'proxy_torso',BUD,(0,0,.33*scale),(.95*scale,.78*scale,1.1*scale),color,True)
    emit(module,source,'proxy_head',BUD,(0,-.03,1.30*scale),(.70*scale,.65*scale,.64*scale),color,True)
    for i,x in enumerate([-.17,.17]):
        emit(module,source,'proxy_eye_'+str(i),BUD,(x*scale,-.30*scale,1.58*scale),(.10*scale,.085*scale,.10*scale),'Shadow')
    if kind=='yeast':
        emit(module,source,'proxy_yeast_crown',CAP,(0,0,1.7*scale),(1.05*scale,.94*scale,.35*scale),'Yeast')
        for i,x in enumerate([-.25,0,.25]):
            emit(module,source,'proxy_yeast_bud_'+str(i),BUD,(x,0,1.97*scale),(.18,.18,.24),'Dough')
    elif kind=='dough':
        for i,x in enumerate([-.25,0,.25]):
            emit(module,source,'proxy_gluten_hair_'+str(i),FIBER,(x,.06,1.80),(.10,.10,.46),'Fiber')
    elif kind=='guard':
        emit(module,source,'proxy_guard_shoulders',BREAD,(0,0,1.12*scale),(1.35*scale,.60*scale,.4*scale),'Mold')
        emit(module,source,'proxy_guard_crest',BUD,(0,0,1.90*scale),(.6,.4,.5),'Core')
    elif kind=='ambush':
        emit(module,source,'proxy_ambush_tendrils','roots',(0,0,0),(2.7,2.3,.4),'Mold')
        emit(module,source,'proxy_spore_bud',BUD,(.4,.12,1.05),(.4,.4,.5),'Core')
    else:
        for i,(x,y,z) in enumerate([(-.4,.2,1.2),(.35,.15,1.48),(.15,-.1,1.98)]):
            emit(module,source,'proxy_spore_bud_'+str(i),BUD,(x*scale,y*scale,z*scale),(.24*scale,.24*scale,.3*scale),'Core')

def build_deep():
    m='shendu_02';s='buildings/core_altar'
    # Root agent owns the large boss floor, surrounding recess and cave geometry.
    emit(m,s,'stepped_recessed_altar',ALTAR,(0,0,0),(8,7,.8),'Crust',True)
    for i,(x,y) in enumerate([(-3.1,-1.7),(3.1,-1.7),(-3.1,1.7),(3.1,1.7)]):
        emit(m,s,'altar_rim_nodule_'+str(i),BUD,(x,y,.52),(.35,.35,.28),'Glow')
    character('monsters/boss_core','shendu','mold','shendu_03',True)
    for monster in ['mold_guard_shendu_1','mold_guard_shendu_2']:
        character('monsters/'+monster,'shendu','guard','shendu_03')

    m='shendu_07';s='flora/mycelium_shendu'
    emit(m,s,'deep_main_mycelium','roots',(0,0,0),(6.7,5,.65),'Mold',False,
         terrain=True,region='shendu')
    emit(m,s,'deep_fine_mycelium','roots',(3.7,2.2,0),(3.6,3,.30),'Fiber',False,yaw=68,
         terrain=True,region='shendu')
    for i,(x,y,h) in enumerate([(-3,1,1.7),(3.5,-2,1.1),(4.3,3,2.2)]):
        mushroom(m,s,'deep_glowing_fungus_'+str(i),(x,y,0),(.3,.3,h),(h*1.1,h,.65),h-.18,
                 'Glow',region='shendu',small=h<1.3)
        emit(m,s,'deep_glowing_bud_'+str(i),BUD,(x+.9,y+.5,0),(.35,.35,.45),'Glow',False,
             terrain=True,region='shendu')

def build_source_characters_and_resources():
    for monster in sc.source['monsters']:
        source='monsters/'+monster['id']
        if source in _character_sources:continue
        ident=monster['id']
        if ident.startswith('dough'):kind='dough'
        elif ident.startswith('yeast'):kind='yeast'
        elif 'guard' in ident:kind='guard'
        elif 'thread' in ident:kind='ambush'
        elif 'spore' in ident:kind='spore'
        else:kind='mold'
        character(source,monster['region'],kind)

    for resource in sc.source['resources']:
        s='resources/'+resource['id'];m=resource['region']+'_ResourceProxies'
        if s in _resource_sources:raise ValueError('Resource source repeated: '+s)
        _resource_sources.add(s)
        # Original resource is the composition root, not moved onto a scenic shelf.
        if resource['id']=='flour_demo':
            emit(m,s,'proxy_flour_tray',ALTAR,(0,0,0),(1.25,1.1,.16),'Crust')
            emit(m,s,'proxy_flour_heap',BUD,(0,0,.11),(.85,.75,.5),'Flour')
            emit(m,s,'proxy_flour_scoop',PLANK,(.5,.15,.14),(.48,.22,.08),'Wood')
        else:
            color={'special_yeast_strain':'Yeast','corrupt_spore_core':'Core','active_spore':'Glow'}[resource['id']]
            emit(m,s,'proxy_resource_cup',ALTAR,(0,0,0),(1.2,1.05,.2),'Dough')
            for i,(x,y,z) in enumerate([(0,0,.13),(-.30,.12,.13),(.30,.15,.13)]):
                emit(m,s,'proxy_resource_spore_'+str(i),BUD,(x,y,z),(.43,.4,.65 if i==0 else .42),color)
    expected_monsters={'monsters/'+x['id'] for x in sc.source['monsters']}
    expected_resources={'resources/'+x['id'] for x in sc.source['resources']}
    assert _character_sources==expected_monsters,(_character_sources,expected_monsters)
    assert _resource_sources==expected_resources,(_resource_sources,expected_resources)

def build_generic(module_filter=None):
    """Build only delegated generic modules; no terrain, portals, bridges or major walls."""
    global _module_filter
    _module_filter=None if module_filter is None else set(module_filter)
    _keys.clear();_counts.clear();_sources.clear();_character_sources.clear();_resource_sources.clear()
    build_base()
    build_shallow()
    build_ferment()
    build_yeast()
    build_corruption()
    build_deep()
    build_source_characters_and_resources()
    required={'jidi_02','jidi_03','jidi_04','jidi_05','qianceng_02','qianceng_05',
              'fajiao_01','fajiao_02','fajiao_03','jiaomu_02','jiaomu_03','jiaomu_04',
              'jiaomu_05','meijun_03','meijun_04','meijun_05','shendu_02','shendu_03','shendu_07'}
    expected=required if _module_filter is None else required.intersection(_module_filter)
    assert expected.issubset(_counts),expected-set(_counts)
    return {'piece_count':sum(_counts.values()),'modules':dict(_counts),
            'source_piece_counts':dict(_sources),'monster_source_count':sum(s in _sources for s in _character_sources),
            'resource_source_count':sum(s in _sources for s in _resource_sources),
            'module_filter':sorted(_module_filter) if _module_filter is not None else None,
            'gameplay_status':'stationary visual/collision proxies only; preserve original source semantics and future status',
            'source_anchor_roots_moved':False}
