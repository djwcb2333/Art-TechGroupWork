import unreal,json,math,shutil,gc
from pathlib import Path
P=Path(__file__).parent;ROOT='/Game/Maps/DoughWorldDepth';PACK='/Game/Maps/SoStylized/'
A=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);W=unreal.EditorLevelLibrary.get_editor_world();AT=unreal.AssetToolsHelpers.get_asset_tools();M=unreal.MaterialEditingLibrary
assert W.get_path_name().startswith(ROOT+'/Levels/'),W.get_path_name()
def prop(o,k,v):o.set_editor_property(k,v)
def asset(name,folder,cls,factory):
    path=ROOT+'/'+folder+'/'+name
    return unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else AT.create_asset(name,ROOT+'/'+folder,cls,factory)
def save(o):assert unreal.EditorAssetLibrary.save_loaded_asset(o,False)
def ex(mat,cls,**kwargs):
    n=M.create_material_expression(mat,cls)
    for k,v in kwargs.items():prop(n,k,v)
    return n
def lk(a,out,b,inp):
    names=list(M.get_material_expression_input_names(b))
    if inp=='Coordinates':inp='UVs'
    if inp=='Input' and inp not in names:inp=names[0]
    assert M.connect_material_expressions(a,out,b,inp),(b.get_class().get_name(),inp,names)
def import_tex(file,linear=False):
    task=unreal.AssetImportTask();prop(task,'filename',str(file));prop(task,'destination_path',ROOT+'/Terrain');prop(task,'automated',True);prop(task,'save',True);prop(task,'replace_existing',True);AT.import_asset_tasks([task])
    tex=unreal.load_asset(ROOT+'/Terrain/'+file.stem);assert tex,file
    if linear:
        prop(tex,'srgb',False);prop(tex,'compression_settings',unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP);prop(tex,'filter',unreal.TextureFilter.TF_NEAREST);prop(tex,'mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    prop(tex,'address_x',unreal.TextureAddress.TA_CLAMP);prop(tex,'address_y',unreal.TextureAddress.TA_CLAMP);save(tex);return tex
# Independent organic environment master; the graph actually consumes CameraFade.
base=asset('M_DepthOrganic_Fade','Materials',unreal.Material,unreal.MaterialFactoryNew());M.delete_all_material_expressions(base);prop(base,'blend_mode',unreal.BlendMode.BLEND_MASKED);prop(base,'two_sided',True)
tint=ex(base,unreal.MaterialExpressionVectorParameter,parameter_name='Tint',default_value=unreal.LinearColor(.55,.31,.11,1));M.connect_material_property(tint,'',unreal.MaterialProperty.MP_BASE_COLOR)
r=ex(base,unreal.MaterialExpressionScalarParameter,parameter_name='Roughness',default_value=.78);M.connect_material_property(r,'',unreal.MaterialProperty.MP_ROUGHNESS)
fade=ex(base,unreal.MaterialExpressionScalarParameter,parameter_name='CameraFade',default_value=1);alpha=ex(base,unreal.MaterialExpressionScalarParameter,parameter_name='OriginalOpacity',default_value=1);product=ex(base,unreal.MaterialExpressionMultiply);lk(fade,'',product,'A');lk(alpha,'',product,'B')
dither=ex(base,unreal.MaterialExpressionMaterialFunctionCall);dither.set_material_function(unreal.load_asset('/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA'));inputs=list(M.get_material_expression_input_names(dither));lk(fade,'',dither,inputs[0]);lk(dither,'',product,'A');lk(alpha,'',product,'B');M.connect_material_property(product,'',unreal.MaterialProperty.MP_OPACITY_MASK)
glow=ex(base,unreal.MaterialExpressionScalarParameter,parameter_name='Glow',default_value=0);em=ex(base,unreal.MaterialExpressionMultiply);lk(tint,'',em,'A');lk(glow,'',em,'B');M.connect_material_property(em,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR);M.recompile_material(base);save(base)
colors={'Dough':(.64,.40,.19),'Crust':(.27,.115,.035),'Flour':(.87,.76,.56),'Yeast':(.93,.51,.085),'Cap':(.79,.25,.045),'Mold':(.24,.17,.29),'Core':(.14,.09,.18),'Fiber':(.40,.43,.32),'Glow':(.18,.66,.43),'Wood':(.24,.115,.043),'Shadow':(.032,.018,.025),'Butter':(.92,.63,.14),'Water':(.09,.36,.31)}
for name,rgb in colors.items():
    mi=asset('MI_'+name,'Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());M.set_material_instance_parent(mi,base);M.set_material_instance_vector_parameter_value(mi,'Tint',unreal.LinearColor(*rgb,1));M.set_material_instance_scalar_parameter_value(mi,'Glow',.7 if name=='Glow' else 0);save(mi)

import sys
sys.path.insert(0,str(P))
import scene_common as sc
# Import an external visibility mask: white removes Landscape only inside real shafts and bridge underpass.
file=P/'TerrainDesign/T_LandscapeHoleMask.png';t=unreal.AssetImportTask();t.set_editor_property('filename',str(file));t.set_editor_property('destination_path',ROOT+'/Terrain');t.set_editor_property('automated',True);t.set_editor_property('save',True);AT.import_asset_tasks([t])
tex=unreal.load_asset(ROOT+'/Terrain/T_LandscapeHoleMask');assert tex
for k,v in {'srgb':False,'compression_settings':unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP,'filter':unreal.TextureFilter.TF_NEAREST,'mip_gen_settings':unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS}.items():tex.set_editor_property(k,v)
unreal.EditorAssetLibrary.save_loaded_asset(tex,False)
mat=AT.create_asset('M_HoleMaskImport',ROOT+'/Terrain',unreal.Material,unreal.MaterialFactoryNew()) if not unreal.EditorAssetLibrary.does_asset_exist(ROOT+'/Terrain/M_HoleMaskImport') else unreal.load_asset(ROOT+'/Terrain/M_HoleMaskImport');M.delete_all_material_expressions(mat);mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
n=M.create_material_expression(mat,unreal.MaterialExpressionTextureSample);n.set_editor_property('texture',tex);n.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR);M.connect_material_property(n,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR);M.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat,False)
rt=unreal.RenderingLibrary.create_render_target2d(W,505,505,unreal.TextureRenderTargetFormat.RTF_RGBA32F);unreal.RenderingLibrary.draw_material_to_render_target(W,rt,mat)
land=next(a for a in sc.A.get_all_level_actors() if isinstance(a,unreal.Landscape))
with unreal.ScopedEditorTransaction('Import external DoughWorld hole visibility'):
    ok=unreal.DepthLandscapeTools.import_visibility_mask(land,rt,0)
(P/'HoleImportResult.json').write_text(json.dumps({'success':ok,'api_methods':[x for x in dir(land) if 'layer' in x.lower()]},indent=2),encoding='utf-8')
unreal.EditorLevelLibrary.save_current_level();unreal.log('DEPTH_GEOMETRY_IMPORTED '+str(ok))
