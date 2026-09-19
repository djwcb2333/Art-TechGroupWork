"""PIE-only real-character roll against a transient blocking cube; 20s limit.

Execute after entering PIE and finishing all other player-moving diagnostics.
The one initial teleport only places the fixture. Every measured movement after
StartForwardRoll comes from the existing action; no compensating translation.
"""
import json
import math
import time
import traceback
from datetime import datetime, timezone
from pathlib import Path

import unreal

P = Path(__file__).parent
BASELINE = json.loads((P / 'CameraBaseline.json').read_text(encoding='utf-8'))
HEIGHTS = json.loads((P / 'TerrainDesign/terrain_data.json').read_text(encoding='utf-8'))['heights_m']
ARM_BASELINE = next(c['properties'] for c in BASELINE['components'] if c['class'] == 'SpringArmComponent')
CAMERA_BASELINE = next(c['properties'] for c in BASELINE['components'] if c['class'] == 'CameraComponent')
ctx = {'phase': 'initial', 'busy': False, 'finished': False, 'next': 0.0,
       'start': time.monotonic(), 'checks': [], 'samples': [], 'stable_samples': 0,
       'timestamp_utc': datetime.now(timezone.utc).isoformat()}


def xyz(v):
    return [float(v.x), float(v.y), float(v.z)]


def rotation(r):
    return {'pitch': float(r.pitch), 'yaw': float(r.yaw), 'roll': float(r.roll)}


def height(x, y):
    u, v = x / 500.0 * 504, y / 500.0 * 504
    i, j = int(u), int(v)
    tx, ty = u - i, v - j
    return ((1 - ty) * ((1 - tx) * HEIGHTS[j][i] + tx * HEIGHTS[j][i + 1]) +
            ty * ((1 - tx) * HEIGHTS[j + 1][i] + tx * HEIGHTS[j + 1][i + 1]))


def camera_snapshot(world, pawn):
    camera = unreal.GameplayStatics.get_player_camera_manager(world, 0)
    arm = pawn.get_component_by_class(unreal.SpringArmComponent)
    component = pawn.get_component_by_class(unreal.CameraComponent)
    snap = {'rotation': rotation(camera.get_camera_rotation()), 'arm': float(arm.target_arm_length),
            'fov': float(camera.get_fov_angle()), 'projection_mode': str(component.projection_mode),
            'ortho_width': float(component.ortho_width),
            'absolute_rotation': bool(arm.get_editor_property('absolute_rotation')),
            'do_collision_test': bool(arm.get_editor_property('do_collision_test')),
            'socket_offset': xyz(arm.get_editor_property('socket_offset')),
            'target_offset': xyz(arm.get_editor_property('target_offset'))}
    snap['matches_baseline'] = (
        abs(snap['arm'] - ARM_BASELINE['target_arm_length']) < .01 and
        abs(snap['fov'] - BASELINE['fov']) < .01 and
        abs(snap['ortho_width'] - CAMERA_BASELINE['ortho_width']) < .01 and
        snap['projection_mode'] == CAMERA_BASELINE['projection_mode'] and
        snap['absolute_rotation'] == ARM_BASELINE['absolute_rotation'] and
        snap['do_collision_test'] == ARM_BASELINE['do_collision_test'] and
        all(abs((snap['rotation'][n] - q + 180) % 360 - 180) < .01
            for n, q in BASELINE['camera_world_rotation'].items()) and
        all(abs(a - b) < .01 for field in ('socket_offset', 'target_offset')
            for a, b in zip(snap[field], ARM_BASELINE[field])))
    return snap


def pose(pawn):
    mesh = pawn.get_editor_property('mesh')
    return {'location': xyz(mesh.get_editor_property('relative_location')),
            'rotation': rotation(mesh.get_editor_property('relative_rotation')),
            'pause_anims': bool(mesh.get_editor_property('pause_anims'))}


def finish(error=None):
    if ctx['finished']:
        return
    ctx['finished'] = True
    unreal.unregister_slate_post_tick_callback(ctx['handle'])
    cleanup_errors = []
    wall = ctx.get('wall')
    if wall:
        try:
            result = wall.destroy_actor()
            ctx['transient_wall_destroy_result'] = result
            if result is False:
                cleanup_errors.append('DestroyActor returned false for transient wall.')
        except Exception as exc:
            cleanup_errors.append(str(exc))
    excluded = {'handle', 'wall', 'wall_mesh', 'pawn', 'busy', 'next'}
    out = {key: value for key, value in ctx.items() if key not in excluded}
    out.update(error=error, cleanup_errors=cleanup_errors,
               elapsed_seconds=time.monotonic() - ctx['start'],
               passed=error is None and not cleanup_errors and bool(ctx['checks']) and
                      all(check['passed'] for check in ctx['checks']),
               scope='Existing StartForwardRoll on the actual PIE pawn against a transient BlockAll cube; no hardware-key injection.',
               source_or_map_assets_modified=False,
               measured_movement_teleport_or_compensation=False,
               camera_world_location_not_compared='The fixed camera follows the moving pawn; settings and orientation are compared.')
    (P / 'RollCollisionRuntime.json').write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding='utf-8')
    for key in ('wall', 'wall_mesh', 'pawn'):
        ctx.pop(key, None)
    unreal.log('DEPTH_ROLL_COLLISION_DONE ' + str(out['passed']) + ' ' + str(error))


def tick(_delta):
    if ctx['busy'] or ctx['finished']:
        return
    ctx['busy'] = True
    try:
        now = time.monotonic()
        assert now - ctx['start'] < 20.0, '20-second collision test budget exceeded.'
        if now < ctx['next']:
            return
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        assert world, 'PIE must be running.'
        assert 'L_DoughWorld_Depth' in world.get_path_name(), 'Expected candidate Depth map in PIE.'
        pawn = unreal.GameplayStatics.get_player_character(world, 0)
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        assert pawn and pc, 'Actual possessed local character is required.'
        move = pawn.get_component_by_class(unreal.CharacterMovementComponent)
        position = xyz(pawn.get_actor_location())

        if ctx['phase'] == 'initial':
            assert not pawn.get_editor_property('is_rolling'), 'Do not overlap another roll diagnostic.'
            assert not pc.is_move_input_ignored(), 'Move input was already blocked before this test.'
            ctx['pawn'] = pawn
            ctx['pawn_path'] = pawn.get_path_name()
            pc.stop_movement()
            move.stop_movement_immediately()
            pawn.set_actor_location(unreal.Vector(-2000.0, 10000.0, (height(230, 350) + 1.10) * 100), False, True)
            pawn.set_actor_rotation(unreal.Rotator(pitch=0, yaw=0, roll=0), True)
            ctx.update(phase='settle', next=now + 1.2, stage_start=now)
            return

        assert pawn == ctx['pawn'], 'Possessed pawn changed during the collision test.'
        if ctx['phase'] == 'settle':
            previous = ctx.get('last_ground_position', position)
            stable = bool(move.is_moving_on_ground()) and max(abs(a - b) for a, b in zip(position, previous)) < .5
            ctx['stable_samples'] = ctx['stable_samples'] + 1 if stable else 0
            ctx['last_ground_position'] = position
            assert now - ctx['stage_start'] < 6.0, 'Fixture point did not settle on real ground.'
            if ctx['stable_samples'] < 3:
                ctx['next'] = now + .10
                return
            requested = float(pawn.get_editor_property('roll_distance'))
            assert abs(requested - 400.0) < .01, 'Expected the existing configured 400 cm roll; test will not change it.'
            capsule = pawn.get_component_by_class(unreal.CapsuleComponent)
            radius = float(capsule.get_scaled_capsule_radius())
            half_height = float(capsule.get_scaled_capsule_half_height())
            wall_center = unreal.Vector(position[0] + 250.0, position[1], position[2] - half_height + 300.0)
            wall = unreal.DepthEnvironmentActor.spawn_transient_validation_actor(world, unreal.Transform(location=wall_center))
            assert wall, 'Native transient validation actor factory returned null.'
            ctx['wall'] = wall  # Register for cleanup before configuring any component.
            mesh = wall.get_editor_property('environment_mesh')
            ctx['wall_mesh'] = mesh
            mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
            mesh.set_editor_property('can_ever_affect_navigation', False)
            mesh.set_simulate_physics(False)
            cube = unreal.load_asset('/Engine/BasicShapes/Cube')
            assert cube, 'Engine basic cube is unavailable.'
            mesh.set_static_mesh(cube)
            mesh.set_relative_scale3d(unreal.Vector(1, 8, 6))
            mesh.set_collision_profile_name('BlockAll')
            mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
            wall.get_editor_property('camera_fade_component').deactivate()
            actual_center = mesh.get_world_transform().translation
            assert (actual_center - wall_center).length() < .01, 'Transient wall component has incorrect attachment/transform.'
            # CollisionResponse is a struct; the response enum is CollisionResponseType.
            assert mesh.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN) == unreal.CollisionResponseType.ECR_BLOCK
            ctx.update(wall_path=wall.get_path_name(), wall_center_cm=xyz(actual_center),
                       wall_dimensions_cm=[100.0, 800.0, 600.0], capsule_radius_cm=radius,
                       requested_distance_cm=requested, wall_near_face_x_cm=float(actual_center.x) - 50.0,
                       phase='start_roll', next=now + .20)
            return

        if ctx['phase'] == 'start_roll':
            assert move.is_moving_on_ground(), 'Pawn became airborne before roll.'
            ctx['start_position_cm'] = position
            ctx['expected_stop_x_cm'] = ctx['wall_near_face_x_cm'] - ctx['capsule_radius_cm']
            ctx['pose_before'] = pose(pawn)
            ctx['orient_to_movement_before'] = bool(move.get_editor_property('orient_rotation_to_movement'))
            ctx['camera_before'] = camera_snapshot(world, pawn)
            assert ctx['camera_before']['matches_baseline'], 'Camera was already different from the saved baseline.'
            pawn.start_forward_roll()
            ctx['roll_started'] = bool(pawn.get_editor_property('is_rolling'))
            assert ctx['roll_started'], 'Existing StartForwardRoll rejected the grounded request.'
            ctx.update(phase='rolling', stage_start=now)
            return

        if ctx['phase'] == 'rolling':
            elapsed = now - ctx['stage_start']
            rolling = bool(pawn.get_editor_property('is_rolling'))
            ctx['samples'].append({'seconds': elapsed, 'position_cm': position, 'rolling': rolling,
                                   'grounded': bool(move.is_moving_on_ground())})
            assert elapsed < 4.0, 'Roll failed to finish within four seconds.'
            if not rolling:
                ctx.update(phase='verify', next=now + .40)
            return

        if ctx['phase'] == 'verify':
            distance = math.hypot(position[0] - ctx['start_position_cm'][0], position[1] - ctx['start_position_cm'][1])
            stop_error = abs(position[0] - ctx['expected_stop_x_cm'])
            lateral = abs(position[1] - ctx['start_position_cm'][1])
            farthest = max([s['position_cm'][0] for s in ctx['samples']] + [position[0]])
            final_pose = pose(pawn)
            pose_error = max(
                [abs(a - b) for a, b in zip(final_pose['location'], ctx['pose_before']['location'])] +
                [abs((final_pose['rotation'][key] - ctx['pose_before']['rotation'][key] + 180) % 360 - 180)
                 for key in ('pitch', 'yaw', 'roll')])
            rolling = bool(pawn.get_editor_property('is_rolling'))
            grounded = bool(move.is_moving_on_ground())
            restored = not pc.is_move_input_ignored()
            orient_restored = bool(move.get_editor_property('orient_rotation_to_movement')) == ctx['orient_to_movement_before']
            ctx['camera_after'] = camera_snapshot(world, pawn)
            ctx['checks'].append({'name': 'real_capsule_stops_at_blocking_wall',
                'passed': 50.0 < distance < 250.0 and stop_error < 4.0 and lateral < 3.0 and
                          farthest + ctx['capsule_radius_cm'] <= ctx['wall_near_face_x_cm'] + .5,
                'distance_cm': distance, 'stop_position_cm': position, 'expected_stop_x_cm': ctx['expected_stop_x_cm'],
                'stop_error_cm': stop_error, 'lateral_displacement_cm': lateral,
                'maximum_sampled_capsule_front_x_cm': farthest + ctx['capsule_radius_cm']})
            ctx['checks'].append({'name': 'collision_roll_finishes_and_restores_character',
                'passed': not rolling and grounded and restored and orient_restored and pose_error < .01 and
                          final_pose['pause_anims'] == ctx['pose_before']['pause_anims'],
                'is_rolling': rolling, 'grounded': grounded, 'move_input_restored': restored,
                'orient_to_movement_restored': orient_restored, 'mesh_pose_max_error': pose_error,
                'pause_anims_restored': final_pose['pause_anims'] == ctx['pose_before']['pause_anims']})
            ctx['checks'].append({'name': 'fixed_camera_baseline_unchanged',
                                   'passed': ctx['camera_before']['matches_baseline'] and ctx['camera_after']['matches_baseline']})
            finish()
            return
        raise RuntimeError('Unexpected phase ' + ctx['phase'])
    except Exception:
        finish(traceback.format_exc())
    finally:
        ctx['busy'] = False


ctx['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.log('DEPTH_ROLL_COLLISION_ARMED')
