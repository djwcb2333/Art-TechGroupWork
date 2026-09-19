import unreal,json,sys
from pathlib import Path
P=Path(__file__).parent;sys.path.insert(0,str(P));import scene_common as sc
AT=sc.AT;M=sc.M;ROOT=sc.ROOT;W=sc.W
report=[]
g=json.loads((P/'Planning/Geometry/GeometryManifest.json').read_text(encoding='utf-8'))
for a in g['assets']:
    path=ROOT+'/Meshes/'+a['key']
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        t=unreal.AssetImportTask();t.set_editor_property('filename',a['obj_path']);t.set_editor_property('destination_path',ROOT+'/Meshes');t.set_editor_property('destination_name',a['key']);t.set_editor_property('automated',True);t.set_editor_property('save',True)
        opts=unreal.FbxImportUI();opts.set_editor_property('import_mesh',True);opts.set_editor_property('import_as_skeletal',False);opts.set_editor_property('import_materials',False);opts.set_editor_property('import_textures',False);opts.set_editor_property('automated_import_should_detect_type',False);opts.set_editor_property('mesh_type_to_import',unreal.FBXImportType.FBXIT_STATIC_MESH)
        opts.static_mesh_import_data.set_editor_property('combine_meshes',True);opts.static_mesh_import_data.set_editor_property('auto_generate_collision',False);opts.static_mesh_import_data.set_editor_property('import_uniform_scale',1.0);t.set_editor_property('options',opts);t.set_editor_property('factory',unreal.FbxFactory());AT.import_asset_tasks([t])
    sm=unreal.load_asset(path);assert isinstance(sm,unreal.StaticMesh),path
    box=sm.get_bounding_box();sz=[box.max.x-box.min.x,box.max.y-box.min.y,box.max.z-box.min.z]
    body=sm.get_editor_property('body_setup');assert body,path
    body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    sm.set_material(0,sc.material('Dough'));unreal.EditorAssetLibrary.save_loaded_asset(sm,False)
    report.append({'asset':path,'expected_cm':a['dimensions_cm'],'actual_cm':sz,'collision':str(body.get_editor_property('collision_trace_flag'))})
(P/'GeometryImportResult.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
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
