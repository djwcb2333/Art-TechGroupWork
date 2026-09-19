import unreal,sys
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
M=sc.M;AT=sc.AT;ROOT=sc.ROOT
path=ROOT+'/Materials/M_ButterFlow_NoRVT';mat=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else AT.create_asset('M_ButterFlow_NoRVT',ROOT+'/Materials',unreal.Material,unreal.MaterialFactoryNew());M.delete_all_material_expressions(mat);mat.set_editor_property('two_sided',True)
def e(cls,**kw):
    n=M.create_material_expression(mat,cls)
    for k,v in kw.items():n.set_editor_property(k,v)
    return n
def link(a,out,b,inp):assert M.connect_material_expressions(a,out,b,inp)
uv=e(unreal.MaterialExpressionTextureCoordinate,u_tiling=2,v_tiling=3);pan=e(unreal.MaterialExpressionPanner,speed_x=.025,speed_y=.12);link(uv,'',pan,'Coordinate')
tex=e(unreal.MaterialExpressionTextureSample,texture=unreal.load_asset('/Game/Maps/SoStylized/Environment/Water/Textures/T_RiverFlowLines'));link(pan,'',tex,'UVs')
mul=e(unreal.MaterialExpressionMultiply,const_b=.24);link(tex,'R',mul,'A');add=e(unreal.MaterialExpressionAdd,const_b=.82);link(mul,'',add,'A');tint=e(unreal.MaterialExpressionVectorParameter,parameter_name='Tint',default_value=unreal.LinearColor(.92,.63,.14,1));color=e(unreal.MaterialExpressionMultiply);link(tint,'',color,'A');link(add,'',color,'B');M.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
normal=e(unreal.MaterialExpressionTextureSample,texture=unreal.load_asset('/Game/Maps/SoStylized/Environment/Water/Textures/T_Water1_N'),sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL);link(pan,'',normal,'UVs');M.connect_material_property(normal,'RGB',unreal.MaterialProperty.MP_NORMAL)
rough=e(unreal.MaterialExpressionConstant,r=.3);M.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS);M.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat,False)
mi=sc.material('Butter');M.set_material_instance_parent(mi,mat);unreal.EditorAssetLibrary.save_loaded_asset(mi,False)
unreal.log('DEPTH_LIQUID_FLOW_READY')
