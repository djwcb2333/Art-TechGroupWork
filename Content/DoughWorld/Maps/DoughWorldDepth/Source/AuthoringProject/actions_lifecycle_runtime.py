"""Serial PIE regression, normally 8-11 seconds, hard limit 25 seconds.
Extra budget accommodates the observed ~3 FPS editor and possession callback settling.
Calls compiled actions and the real GameMode restart chain; no test-time source changes.
"""
import json
import math
import sys
import time
import traceback
from pathlib import Path
import unreal

P = Path(__file__).parent
sys.path.insert(0, str(P))
import scene_common as sc

baseline = json.loads((P / 'CameraBaseline.json').read_text(encoding='utf-8'))
arm_baseline = next(c['properties']['target_arm_length'] for c in baseline['components'] if c['class'] == 'SpringArmComponent')
ctx = {'phase': 'initial', 'busy': False, 'finished': False, 'next': 0.0,
       'start': time.monotonic(), 'checks': [], 'jump_samples': [], 'roll_samples': [],
       'lifecycle_trace': [], 'test_budget_seconds': 25.0,
       'budget_reason': 'Observed editor runs near 3 FPS; allow possession callbacks and restarted camera to settle.'}


def xyz(v):
    return [float(v.x), float(v.y), float(v.z)]


def actor_snapshot(actor):
    if actor is None:
        return {'path': None, 'class': None, 'valid': False}
    result = {'path': None, 'class': None, 'valid': bool(unreal.SystemLibrary.is_valid(actor))}
    try:
        result.update(path=actor.get_path_name(), **{'class': actor.get_class().get_path_name()})
    except Exception as exc:
        result['inspection_error'] = str(exc)
    return result


def possession_snapshot(controller, event):
    result = {'event': event, 'seconds': time.monotonic() - ctx['start'],
              'controller': actor_snapshot(controller),
              'controlled_pawn': actor_snapshot(controller.get_controlled_pawn())}
    ctx['lifecycle_trace'].append(result)
    return result['controlled_pawn']


def pawn_paths(world):
    return [a.get_path_name() for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
            if unreal.SystemLibrary.is_valid(a)]


def camera_snapshot(world, pawn):
    camera = unreal.GameplayStatics.get_player_camera_manager(world, 0)
    arm = pawn.get_component_by_class(unreal.SpringArmComponent)
    actual = pawn.get_component_by_class(unreal.CameraComponent)
    r = camera.get_camera_rotation()
    s = {'rotation': {'pitch': float(r.pitch), 'yaw': float(r.yaw), 'roll': float(r.roll)},
         'arm': float(arm.target_arm_length), 'fov': float(camera.get_fov_angle()),
         'projection_mode': str(actual.projection_mode), 'ortho_width': float(actual.ortho_width),
         'absolute_rotation': bool(arm.get_editor_property('absolute_rotation')),
         'do_collision_test': bool(arm.get_editor_property('do_collision_test'))}
    s['matches_baseline'] = abs(s['arm'] - arm_baseline) < .01 and abs(s['fov'] - baseline['fov']) < .01
    s['matches_baseline'] &= all(abs((s['rotation'][n] - q + 180) % 360 - 180) < .01 for n, q in baseline['camera_world_rotation'].items())
    s['matches_baseline'] &= s['absolute_rotation'] and not s['do_collision_test']
    return s


def fade_snapshot():
    result = {}
    for component in ctx['environment_components']:
        for state in component.get_fade_status():
            key = state.mesh.get_path_name() + ':' + str(state.slot_index)
            result[key] = {'fade': float(state.current_fade), 'baseline': float(state.original_fade),
                           'occluded': bool(state.get_editor_property('occluded_last_scan')),
                           'owns': bool(state.get_editor_property('owns_current_material')),
                           'mid': state.dynamic_material.get_path_name()}
    return result


def place(pawn, controller, x, y):
    controller.stop_movement()
    pawn.get_component_by_class(unreal.CharacterMovementComponent).stop_movement_immediately()
    pawn.set_actor_location(sc.world([x, y, sc.h(x, y) + 1.1]), False, True)


def finish(error=None):
    if ctx['finished']:
        return
    ctx['finished'] = True
    unreal.unregister_slate_post_tick_callback(ctx['handle'])
    # Never leave the local controller intentionally unpossessed if a diagnostic fails mid-chain.
    recovery = []
    try:
        pc = ctx.get('controller')
        gm = ctx.get('game_mode')
        if pc and gm:
            current = pc.get_controlled_pawn()
            current_state = possession_snapshot(pc, 'finish_before_recovery')
            if not current_state['valid'] or current_state['class'] != ctx.get('old_pawn_class'):
                if current is not None:
                    pc.un_possess()
                gm.restart_player(pc)
                recovery.append('RestartPlayer called to restore the configured player after interrupted test.')
            if not ctx.get('initial_move_input_ignored', True) and pc.is_move_input_ignored():
                pc.reset_ignore_move_input()
                recovery.append('Cleared a diagnostic movement-input lock; original input was enabled.')
            possession_snapshot(pc, 'finish_after_recovery')
    except Exception as exc:
        recovery.append(str(exc))
    excluded = {'handle', 'busy', 'next', 'environment_components', 'old_pawn', 'controller', 'game_mode'}
    out = {k: v for k, v in ctx.items() if k not in excluded}
    out.update(error=error, recovery=recovery, elapsed_seconds=time.monotonic() - ctx['start'],
               passed=error is None and bool(ctx['checks']) and all(q['passed'] for q in ctx['checks']),
               scope='existing C++ action methods and actual UnPossess/DestroyActor/GameMode.RestartPlayer lifecycle; no hardware-key injection',
               source_or_map_assets_modified=False)
    (P / 'ActionsLifecycleRuntime.json').write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding='utf-8')
    # Release Python references to PIE objects after unregistering the callback.
    for key in excluded:
        if key not in ('busy', 'next', 'handle'):
            ctx.pop(key, None)
    unreal.log('DEPTH_ACTIONS_LIFECYCLE_DONE ' + str(out['passed']) + ' ' + str(error))


def tick(_delta):
    if ctx['busy'] or ctx['finished']:
        return
    ctx['busy'] = True
    try:
        now = time.monotonic()
        assert now - ctx['start'] < ctx['test_budget_seconds'], '25-second test budget exceeded.'
        if now < ctx['next']:
            return
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        assert world, 'PIE must be running.'
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        pawn = unreal.GameplayStatics.get_player_character(world, 0)
        phase = ctx['phase']
        if phase == 'initial':
            assert pawn and pc, 'No possessed local player.'
            ctx['controller'] = pc
            ctx['game_mode'] = unreal.GameplayStatics.get_game_mode(world)
            assert ctx['game_mode'], 'No authoritative PIE GameMode.'
            ctx['old_pawn'] = pawn
            ctx['old_pawn_path'] = pawn.get_path_name()
            ctx['old_pawn_class'] = pawn.get_class().get_path_name()
            ctx['controller_path'] = pc.get_path_name()
            ctx['initial_move_input_ignored'] = bool(pc.is_move_input_ignored())
            assert not ctx['initial_move_input_ignored'], 'Test requires initially enabled movement input.'
            possession_snapshot(pc, 'initial_player')
            ctx['environment_components'] = [a.get_component_by_class(unreal.CameraOccluderFadeComponent)
                for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.DepthEnvironmentActor)]
            place(pawn, pc, 230.0, 350.0)
            pawn.set_actor_rotation(unreal.Rotator(pitch=0, yaw=0, roll=0), True)
            ctx.update(phase='start_jump', next=now + 1.2, stage_start=now)
            return

        if phase == 'without_pawn':
            # Observe callbacks rather than assuming the controller stays empty for 0.75 s.
            assert pc.get_path_name() == ctx['controller_path'], 'Local player controller changed during test.'
            observed = possession_snapshot(pc, 'after_old_player_removal_interval')
            ctx['old_pawn_after_interval'] = actor_snapshot(ctx['old_pawn'])
            ctx['live_pawn_paths_after_removal'] = pawn_paths(world)
            old_removed = ctx['old_pawn_path'] not in ctx['live_pawn_paths_after_removal']
            ctx['checks'].append({'name': 'old_player_destroyed_and_no_longer_controlled',
                'passed': old_removed and observed['path'] != ctx['old_pawn_path'],
                'old_removed_from_live_pawns': old_removed, 'observed_controlled_pawn': observed})
            assert old_removed and observed['path'] != ctx['old_pawn_path'], 'Old player has not left the world/controller.'
            # Existing environment MIDs must recover after their former player disappears.
            after = fade_snapshot()
            previous = ctx['fading_before_destroy']
            residual = {key: after.get(key) for key in previous
                if key not in after or not after[key]['owns'] or abs(after[key]['fade'] - after[key]['baseline']) > .015}
            ctx['fade_without_pawn'] = {key: after.get(key) for key in previous}
            ctx['checks'].append({'name': 'old_target_fades_restore_after_old_player_removed',
                                  'passed': bool(previous) and not residual,
                                  'previously_fading_slots': len(previous), 'residual_slots': residual})
            # GameMode reuses any currently controlled pawn, so explicitly detach a callback-created
            # replacement first. Record it; do not destroy unrelated/interim actors to force a pass.
            if pc.get_controlled_pawn() is not None:
                ctx['interim_pawn_before_explicit_restart'] = observed
                pc.un_possess()
            before_restart = possession_snapshot(pc, 'immediately_before_explicit_restart')
            assert before_restart['path'] is None, 'UnPossess callback immediately repossessed a pawn; cannot prove a fresh RestartPlayer spawn.'
            ctx['pawn_paths_before_restart'] = pawn_paths(world)
            ctx['game_mode'].restart_player(pc)
            ctx['explicit_restart_calls'] = 1
            ctx['pawn_immediately_after_restart'] = possession_snapshot(pc, 'immediately_after_explicit_restart')
            ctx['pawn_paths_after_restart'] = pawn_paths(world)
            ctx.update(phase='wait_new_pawn', next=now + .2, stage_start=now)
            return

        if phase == 'wait_new_pawn':
            if not pawn or pawn.get_path_name() == ctx['old_pawn_path']:
                assert now - ctx['stage_start'] < 3.0, 'RestartPlayer did not possess a new pawn.'
                ctx['next'] = now + .1
                return
            ctx['new_pawn_path'] = pawn.get_path_name()
            ctx['new_pawn_class'] = pawn.get_class().get_path_name()
            controlled = possession_snapshot(pc, 'restarted_player_settled')
            immediate = ctx['pawn_immediately_after_restart']
            created_by_call = (immediate['valid'] and immediate['path'] == ctx['new_pawn_path']
                and ctx['new_pawn_path'] not in ctx['pawn_paths_before_restart']
                and ctx['new_pawn_path'] in ctx['pawn_paths_after_restart'])
            ctx['checks'].append({'name': 'real_game_mode_restart_creates_new_pawn',
                'passed': created_by_call and controlled['path'] == ctx['new_pawn_path']
                    and ctx['new_pawn_class'] == ctx['old_pawn_class'],
                'spawned_during_explicit_restart_call': created_by_call,
                'controlled_pawn': controlled, 'explicit_restart_calls': ctx['explicit_restart_calls']})
            place(pawn, pc, 221.7, 243.3)
            ctx.update(phase='verify_new_pawn', next=now + 1.1, stage_start=now)
            return

        assert pawn, 'Player unexpectedly missing before planned restart.'
        move = pawn.get_component_by_class(unreal.CharacterMovementComponent)
        position = xyz(pawn.get_actor_location())
        if phase == 'start_jump':
            if not move.is_moving_on_ground():
                assert now - ctx['stage_start'] < 2.2, 'Test start point is not stable ground.'
                ctx['next'] = now + .1
                return
            ctx['camera_before'] = camera_snapshot(world, pawn)
            ctx['jump_start_cm'] = position
            ctx['jump_peak_cm'] = position[2]
            ctx['jump_airborne_seen'] = False
            pawn.start_action_jump()
            ctx.update(phase='sample_jump', stage_start=now)
            return

        if phase == 'sample_jump':
            elapsed = now - ctx['stage_start']
            airborne = bool(move.is_falling())
            ctx['jump_airborne_seen'] |= airborne
            ctx['jump_peak_cm'] = max(ctx['jump_peak_cm'], position[2])
            ctx['jump_samples'].append({'seconds': elapsed, 'position_cm': position, 'falling': airborne,
                                        'grounded': bool(move.is_moving_on_ground())})
            if elapsed > .12:
                pawn.stop_jumping()
            if (ctx['jump_airborne_seen'] and move.is_moving_on_ground() and elapsed > .2) or elapsed > 2.2:
                peak = ctx['jump_peak_cm'] - ctx['jump_start_cm'][2]
                ctx['checks'].append({'name': 'existing_space_jump_peak_and_landing',
                    'passed': ctx['jump_airborne_seen'] and peak > 40.0 and bool(move.is_moving_on_ground()),
                    'peak_height_cm': peak, 'seconds': elapsed, 'landed': bool(move.is_moving_on_ground())})
                ctx.update(phase='start_roll', next=now + .15)
            return

        if phase == 'start_roll':
            # Reset only the fixture position/orientation; movement itself must come from StartForwardRoll.
            place(pawn, pc, 230.0, 350.0)
            pawn.set_actor_rotation(unreal.Rotator(pitch=0, yaw=0, roll=0), True)
            ctx.update(phase='roll_ground_wait', next=now + .5)
            return

        if phase == 'roll_ground_wait':
            ctx['roll_start_cm'] = position
            ctx['roll_requested_cm'] = float(pawn.get_editor_property('roll_distance'))
            mesh = pawn.get_editor_property('mesh')
            ctx['mesh_before_roll'] = {'location': xyz(mesh.get_editor_property('relative_location')),
                'rotation': [float(mesh.get_editor_property('relative_rotation').pitch), float(mesh.get_editor_property('relative_rotation').yaw), float(mesh.get_editor_property('relative_rotation').roll)]}
            pawn.start_forward_roll()
            ctx['roll_started'] = bool(pawn.get_editor_property('is_rolling'))
            ctx.update(phase='sample_roll', stage_start=now)
            return

        if phase == 'sample_roll':
            elapsed = now - ctx['stage_start']
            rolling = bool(pawn.get_editor_property('is_rolling'))
            ctx['roll_samples'].append({'seconds': elapsed, 'position_cm': position, 'rolling': rolling,
                                        'grounded': bool(move.is_moving_on_ground())})
            if (elapsed > .15 and not rolling) or elapsed > 1.1:
                horizontal = math.hypot(position[0] - ctx['roll_start_cm'][0], position[1] - ctx['roll_start_cm'][1])
                expected = ctx['roll_requested_cm']
                mesh = pawn.get_editor_property('mesh')
                r = mesh.get_editor_property('relative_rotation')
                pose = {'location': xyz(mesh.get_editor_property('relative_location')), 'rotation': [float(r.pitch), float(r.yaw), float(r.roll)]}
                pose_error = max(abs(a - b) for key in ('location', 'rotation') for a, b in zip(pose[key], ctx['mesh_before_roll'][key]))
                restored = not pc.is_move_input_ignored()
                ctx['checks'].append({'name': 'existing_shift_roll_open_ground',
                    'passed': ctx['roll_started'] and not rolling and restored and abs(horizontal - expected) <= max(65.0, expected * .2) and pose_error < .01,
                    'started': ctx['roll_started'], 'distance_cm': horizontal, 'requested_cm': expected, 'seconds': elapsed,
                    'grounded': bool(move.is_moving_on_ground()), 'finished': not rolling,
                    'move_input_restored': restored, 'mesh_pose_max_error': pose_error,
                    'tested_compiled_actions_without_test_time_cpp_changes': True})
                place(pawn, pc, 221.7, 243.3)
                ctx.update(phase='capture_old_fades', next=now + .85)
            return

        if phase == 'capture_old_fades':
            before = fade_snapshot()
            ctx['fading_before_destroy'] = {key: value for key, value in before.items()
                if value['owns'] and value['fade'] < value['baseline'] - .05}
            ctx['fade_slot_count'] = len(before)
            pc.stop_movement()
            move.stop_movement_immediately()
            pawn.stop_jumping()
            possession_snapshot(pc, 'before_unpossess_old_player')
            pc.un_possess()
            ctx['pawn_immediately_after_unpossess'] = possession_snapshot(pc, 'immediately_after_unpossess_old_player')
            pawn.destroy_actor()
            ctx['old_pawn_after_destroy_call'] = actor_snapshot(pawn)
            possession_snapshot(pc, 'immediately_after_destroy_old_player')
            ctx.update(phase='without_pawn', next=now + .75)
            return

        if phase == 'verify_new_pawn':
            after = fade_snapshot()
            active = {key: value for key, value in after.items()
                if value['owns'] and value['fade'] < value['baseline'] - .05 and value['occluded']}
            ctx['fading_after_restart'] = active
            ctx['camera_after_restart'] = camera_snapshot(world, pawn)
            ctx['checks'].append({'name': 'new_pawn_camera_baseline_grounded',
                'passed': ctx['camera_after_restart']['matches_baseline'] and bool(move.is_moving_on_ground()),
                'grounded': bool(move.is_moving_on_ground()), 'position_cm': position})
            ctx['checks'].append({'name': 'restarted_player_controls_and_action_interfaces_present',
                'passed': not pc.is_move_input_ignored() and callable(getattr(pawn, 'start_action_jump', None))
                    and callable(getattr(pawn, 'start_forward_roll', None)),
                'move_input_enabled': not pc.is_move_input_ignored(),
                'scope': 'Action methods already executed before restart; this check verifies the new configured pawn interface and input state.'})
            ctx['checks'].append({'name': 'existing_environment_fades_follow_restarted_player',
                'passed': bool(active), 'active_slots': len(active), 'managed_slots': len(after)})
            finish()
            return
        raise RuntimeError('Unexpected phase ' + phase)
    except Exception:
        finish(traceback.format_exc())
    finally:
        ctx['busy'] = False


ctx['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.log('DEPTH_ACTIONS_LIFECYCLE_ARMED')

