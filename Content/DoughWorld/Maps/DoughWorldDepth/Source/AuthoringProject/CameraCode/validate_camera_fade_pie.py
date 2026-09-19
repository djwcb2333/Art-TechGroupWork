"""PIE-only numerical validation. Does not save maps or move the player/camera.

After a real Editor build/restart and after entering PIE, execute this file, then:
    run('/Game/Maps/DoughWorldV3/Materials/M_CameraFade_Test', 'E:/.../CameraFadeRuntime.json')
The supplied material must have a functional CameraFade scalar connected to Opacity Mask.
An idle, stable player/camera is required. Status values verify controller behavior;
screenshots at CameraFade=1 and 0.2 remain necessary to verify actual rendering.
"""
import json
import math
import pathlib
import time
import traceback
import unreal


def _xyz(value):
    return [float(value.x), float(value.y), float(value.z)]


def _rot(value):
    return [float(value.pitch), float(value.yaw), float(value.roll)]


def _snapshot(component):
    s = component.get_view_snapshot()
    return {
        'valid': bool(s.get_editor_property('valid')),
        'location': _xyz(s.location), 'rotation': _rot(s.rotation),
        'fov': float(s.fov), 'ortho_width': float(s.ortho_width),
        'aspect_ratio': float(s.aspect_ratio),
        'orthographic': bool(s.get_editor_property('orthographic')),
        'distance_to_pawn': float(s.distance_to_pawn),
    }


def _slot(component):
    result = component.get_fade_status()
    assert len(result) == 1, 'Expected exactly one supported slot on test cube.'
    s = result[0]
    return {
        'fade': float(s.current_fade), 'baseline': float(s.original_fade),
        'strength': float(s.fade_strength),
        'owns': bool(s.get_editor_property('owns_current_material')),
        'occluded': bool(s.get_editor_property('occluded_last_scan')),
        'mid': s.dynamic_material.get_path_name(),
    }


def run(material_path, report_path):
    """Launch six asynchronous checks and write JSON, including exceptions and cleanup."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    assert world, 'Enter PIE before running the validation.'
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    camera = unreal.GameplayStatics.get_player_camera_manager(world, 0)
    assert pawn and camera, 'A possessed local player and actual camera are required.'
    material = unreal.load_asset(material_path)
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    assert material and cube, 'Missing test material or engine cube.'

    actors, records = [], []
    ctx = {'index': 0, 'next': time.monotonic() + 1.2, 'finished': False}

    def spawn(location, with_cube=True, baseline=1.0):
        actor = unreal.DepthEnvironmentActor.spawn_transient_validation_actor(
            world, unreal.Transform(location=location))
        assert actor, 'Native PIE fixture factory returned null.'
        actors.append(actor)
        mesh = actor.get_editor_property('environment_mesh')
        fade = actor.get_editor_property('camera_fade_component')
        # Only transient test actors get collision disabled; the production fade code never does this.
        mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        if with_cube:
            mesh.set_static_mesh(cube)
            mesh.set_relative_scale3d(unreal.Vector(0.65, 0.65, 0.65))
            original = unreal.MaterialLibrary.create_dynamic_material_instance(world, material)
            original.set_scalar_parameter_value('CameraFade', baseline)
            mesh.set_material(0, original)
            fade.refresh_fade_meshes()
        return actor, mesh, fade

    def finish(error=None):
        if ctx['finished']:
            return
        ctx['finished'] = True
        unreal.unregister_slate_post_tick_callback(ctx['handle'])
        cleanup_errors = []
        for actor in reversed(actors):
            try:
                actor.destroy_actor()
            except Exception as exc:
                cleanup_errors.append(str(exc))
        report = {
            'passed': error is None and not cleanup_errors,
            'material': material_path,
            'checks': records, 'error': error, 'cleanup_errors': cleanup_errors,
            'visual_opacity_verified_by_this_script': False,
            'collision_unchanged_verified_by_this_script': False,
        }
        output = pathlib.Path(report_path)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('Camera fade validation: ' + str(output) + ' passed=' + str(report['passed']))

    def setup():
        c = camera.get_camera_location()
        p = pawn.get_actor_location()
        right = unreal.MathLibrary.get_right_vector(camera.get_camera_rotation())
        ctx.update(c=c, p=p, right=right)
        # Two independent layers on the real camera-player segment, plus a same-mesh clear sibling.
        ctx['a'] = spawn(c + (p - c) * 0.45, baseline=0.7)
        ctx['b'] = spawn(c + (p - c) * 0.66)
        ctx['clear'] = spawn(c + (p - c) * 0.45 + right * 700)
        ctx['camera_before'] = _snapshot(ctx['a'][2])
        for key in ('a', 'b', 'clear'):
            ctx[key][2].evaluate_occlusion_now()

    def layers_and_independence():
        a, b, clear = (_slot(ctx[key][2]) for key in ('a', 'b', 'clear'))
        assert a['occluded'] and b['occluded'], 'Both overlapping layers must be detected.'
        assert abs(a['fade'] - 0.14) < 0.015 and abs(b['fade'] - 0.2) < 0.015
        assert not clear['occluded'] and abs(clear['fade'] - 1.0) < 0.005
        assert len({a['mid'], b['mid'], clear['mid']}) == 3, 'Each component needs its own MID.'
        records.append({'name': 'multilayer_independent_mid_original_baseline', 'passed': True, 'a': a, 'b': b, 'clear': clear})
        for key in ('a', 'b'):
            actor, _, fade = ctx[key]
            actor.set_actor_location(actor.get_actor_location() + ctx['right'] * 700, False, True)
            fade.evaluate_occlusion_now()

    def restoration_and_tag():
        a, b = _slot(ctx['a'][2]), _slot(ctx['b'][2])
        assert abs(a['fade'] - 0.7) < 0.005 and abs(b['fade'] - 1.0) < 0.005
        records.append({'name': 'leave_occlusion_restores_original_parameter', 'passed': True, 'a': a, 'b': b})
        _, mesh, fade = ctx['clear']
        old_original = fade.get_fade_status()[0].original_material
        mesh.set_editor_property('component_tags', [])
        fade.refresh_fade_meshes()
        assert fade.get_managed_mesh_count() == 0 and mesh.get_material(0) == old_original
        records.append({'name': 'untagged_component_not_managed', 'passed': True})
        # Explicit interaction point is separate from the player and has no mesh/collision.
        target_location = ctx['p'] + ctx['right'] * 450
        ctx['target'] = spawn(target_location, with_cube=False)
        ctx['extra'] = spawn(ctx['c'] + (target_location - ctx['c']) * 0.76)
        ef = ctx['extra'][2]
        ef.set_editor_property('protect_player', False)
        ef.set_editor_property('additional_target_max_distance_cm', 0.0)
        assert ef.evaluate_occlusion_now() == 0, 'No player protection or extra target is enabled yet.'
        ef.add_protected_target(ctx['target'][0], unreal.Vector(), 0.0, 0.0)
        assert ef.evaluate_occlusion_now() == 1, 'Registered actual target should be detected.'

    def extra_target():
        state = _slot(ctx['extra'][2])
        assert state['occluded'] and abs(state['fade'] - 0.2) < 0.015
        records.append({'name': 'additional_interaction_actor_target', 'passed': True, 'status': state})
        ctx['extra'][2].clear_protected_targets()
        ctx['extra'][2].evaluate_occlusion_now()

    def external_replacement():
        actor, mesh, fade = ctx['extra']
        state = _slot(fade)
        assert abs(state['fade'] - 1.0) < 0.005
        foreign = unreal.MaterialLibrary.create_dynamic_material_instance(world, material)
        foreign.set_scalar_parameter_value('CameraFade', 0.63)
        mesh.set_material(0, foreign)
        assert not _slot(fade)['owns']
        fade.deactivate()
        assert mesh.get_material(0) == foreign
        assert abs(foreign.get_scalar_parameter_value('CameraFade') - 0.63) < 0.005
        records.append({'name': 'clear_target_and_external_material_ownership', 'passed': True})
        # Restore currently owned MID on deactivate and read actual camera again.
        _, bmesh, bfade = ctx['b']
        original = bfade.get_fade_status()[0].original_material
        bfade.deactivate()
        assert bmesh.get_material(0) == original
        before, after = ctx['camera_before'], _snapshot(ctx['a'][2])
        for field in ('fov', 'ortho_width', 'aspect_ratio', 'distance_to_pawn'):
            assert abs(before[field] - after[field]) < 0.05, (field, before, after)
        for field in ('location', 'rotation'):
            assert max(abs(a - b) for a, b in zip(before[field], after[field])) < 0.05, (field, before, after)
        assert before['orthographic'] == after['orthographic']
        records.append({'name': 'deactivate_restores_owned_material_camera_unchanged', 'passed': True, 'before': before, 'after': after})

    steps = [setup, layers_and_independence, restoration_and_tag, extra_target, external_replacement]

    def tick(_delta_time):
        if ctx['finished'] or time.monotonic() < ctx['next']:
            return
        try:
            steps[ctx['index']]()
            ctx['index'] += 1
            ctx['next'] = time.monotonic() + 0.9
            if ctx['index'] == len(steps):
                finish()
        except Exception:
            finish(traceback.format_exc())

    ctx['handle'] = unreal.register_slate_post_tick_callback(tick)
    unreal.log('Camera fade validation scheduled; keep the player still until the JSON result appears.')
