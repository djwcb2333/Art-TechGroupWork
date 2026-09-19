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
# Only the duplicate candidate level is cleared. All original assets and maps remain available.
removed=[]
for a in A.get_all_level_actors():
    if isinstance(a,(unreal.StaticMeshActor,unreal.InstancedFoliageActor,unreal.TargetPoint)):
        removed.append(a.get_actor_label());A.destroy_actor(a)
land=next(a for a in A.get_all_level_actors() if isinstance(a,unreal.Landscape))
tex=import_tex(P/'TerrainDesign/HeightRG.png',True)
mat=asset('M_HeightImport','Terrain',unreal.Material,unreal.MaterialFactoryNew());M.delete_all_material_expressions(mat);prop(mat,'shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
n=ex(mat,unreal.MaterialExpressionTextureSample,texture=tex,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR);M.connect_material_property(n,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR);M.recompile_material(mat);save(mat)
rt=unreal.RenderingLibrary.create_render_target2d(W,505,505,unreal.TextureRenderTargetFormat.RTF_RGBA32F);unreal.RenderingLibrary.draw_material_to_render_target(W,rt,mat)
assert land.landscape_import_heightmap_from_render_target(rt,True,0)
land.set_actor_label('Depth_External505_ContinuousLandscape');land.set_folder_path('00_Terrain');land.set_actor_location(unreal.Vector(-25000,-25000,0),False,True);land.set_actor_scale3d(unreal.Vector(50000/504,50000/504,100))
# Candidate ground shader: original source palette, SoStylized sand microdetail, no RVT.
region_tex=import_tex(P/'TerrainDesign/T_DepthRegionColor.png')
ground=asset('M_DepthGround_NoRVT','Terrain',unreal.Material,unreal.MaterialFactoryNew());M.delete_all_material_expressions(ground);prop(ground,'blend_mode',unreal.BlendMode.BLEND_MASKED)
wp=ex(ground,unreal.MaterialExpressionWorldPosition);xy=ex(ground,unreal.MaterialExpressionComponentMask,r=True,g=True);lk(wp,'',xy,'Input');add=ex(ground,unreal.MaterialExpressionAdd,const_b=25000);lk(xy,'',add,'A');div=ex(ground,unreal.MaterialExpressionDivide,const_b=50000);lk(add,'',div,'A');reg=ex(ground,unreal.MaterialExpressionTextureSample,texture=region_tex);lk(div,'',reg,'Coordinates')
uv=ex(ground,unreal.MaterialExpressionDivide,const_b=500);lk(xy,'',uv,'A');sand=ex(ground,unreal.MaterialExpressionTextureSample,texture=unreal.load_asset(PACK+'Environment/Landscape/Textures/T_DesertSand_BC'));lk(uv,'',sand,'Coordinates');gray=ex(ground,unreal.MaterialExpressionDesaturation);lk(sand,'RGB',gray,'Input');mul=ex(ground,unreal.MaterialExpressionMultiply,const_b=.38);lk(gray,'',mul,'A');level=ex(ground,unreal.MaterialExpressionAdd,const_b=.70);lk(mul,'',level,'A');col=ex(ground,unreal.MaterialExpressionMultiply);lk(reg,'RGB',col,'A');lk(level,'',col,'B');M.connect_material_property(col,'',unreal.MaterialProperty.MP_BASE_COLOR)
normal=ex(ground,unreal.MaterialExpressionTextureSample,texture=unreal.load_asset(PACK+'Environment/Landscape/Textures/T_DesertSand_N'),sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL);lk(uv,'',normal,'Coordinates');M.connect_material_property(normal,'RGB',unreal.MaterialProperty.MP_NORMAL)
rough=ex(ground,unreal.MaterialExpressionConstant,r=.87);M.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS);vis=ex(ground,unreal.MaterialExpressionLandscapeVisibilityMask);M.connect_material_property(vis,'',unreal.MaterialProperty.MP_OPACITY_MASK);M.recompile_material(ground);save(ground);prop(land,'landscape_material',ground);prop(land,'landscape_hole_material',ground)
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
# Preserve the established fixed camera and input; IMC_Default only maps click/touch.
for a in A.get_all_level_actors():
    if isinstance(a,unreal.PlayerStart):a.set_actor_location(unreal.Vector(-1670,12330,230),False,True);a.set_actor_rotation(unreal.Rotator(yaw=-90),False)
    if isinstance(a,unreal.NavMeshBoundsVolume):a.set_actor_location(unreal.Vector(0,0,-1500),False,True);a.set_actor_scale3d(unreal.Vector(255,255,95))
    if isinstance(a,unreal.DirectionalLight):a.set_actor_rotation(unreal.Rotator(pitch=-48,yaw=-25),False)
unreal.EditorLevelLibrary.save_current_level()
dst=Path(unreal.Paths.project_content_dir())/'Maps/DoughWorldDepth/Source';dst.mkdir(parents=True,exist_ok=True)
for f in ['DoughWorldDepth_505.r16','DoughWorldDepth_Height16.png','depth_supplement.json','terrain_validation.json']:shutil.copy2(P/'TerrainDesign'/f,dst/f)
shutil.copy2(P/'Handoff/Originals/10_document.json',dst/'OriginalMap.json')
(P/'PreparationResult.json').write_text(json.dumps({'removed_candidate_actors':len(removed),'heightmap_import_success':True,'landscape':land.get_path_name(),'camera_parameters_mutated':False,'materials':list(colors)},indent=2),encoding='utf-8')
unreal.log('DEPTH_PREPARED')
