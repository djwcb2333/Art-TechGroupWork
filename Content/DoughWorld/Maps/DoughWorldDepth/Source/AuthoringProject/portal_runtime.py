"""Run alone during PIE after the map capture/walk tests. Uses the actual E/Enter C++ chain.
Only the initial test placement moves the pawn; entry itself must use ConfirmEntry.
The normal camera is never moved/replaced/configured by this script.
"""
import json
import math
import struct
import sys
import time
import traceback
from pathlib import Path
import unreal

P = Path(__file__).parent
sys.path.insert(0, str(P))
import scene_common as sc

C = P / 'Captures'
C.mkdir(exist_ok=True)
baseline = json.loads((P / 'CameraBaseline.json').read_text(encoding='utf-8'))
expected_arm = next(q['properties']['target_arm_length'] for q in baseline['components'] if q['class'] == 'SpringArmComponent')
ctx = {'phase': 'wait_player', 'next': 0.0, 'busy': False, 'finished': False,
       'started': time.monotonic(), 'checks': [], 'captures': [], 'warnings': []}


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def camera_state(world, pawn):
    camera = unreal.GameplayStatics.get_player_camera_manager(world, 0)
    arm = pawn.get_component_by_class(unreal.SpringArmComponent)
    actual_camera = pawn.get_component_by_class(unreal.CameraComponent)
    rotation = camera.get_camera_rotation()
    return {'world_rotation': {'pitch': float(rotation.pitch), 'yaw': float(rotation.yaw), 'roll': float(rotation.roll)},
            'arm_length': float(arm.target_arm_length), 'fov': float(camera.get_fov_angle()),
            'projection_mode': str(actual_camera.projection_mode), 'ortho_width': float(actual_camera.ortho_width),
            'absolute_rotation': bool(arm.get_editor_property('absolute_rotation')),
            'do_collision_test': bool(arm.get_editor_property('do_collision_test')),
            'enable_camera_lag': bool(arm.get_editor_property('enable_camera_lag'))}


def assert_camera_baseline(snapshot):
    assert abs(snapshot['arm_length'] - expected_arm) < 0.01, snapshot
    assert abs(snapshot['fov'] - baseline['fov']) < 0.01, snapshot
    for name, expected in baseline['camera_world_rotation'].items():
        error = abs((snapshot['world_rotation'][name] - expected + 180.0) % 360.0 - 180.0)
        assert error < 0.01, (name, error, snapshot)
    assert snapshot['absolute_rotation'] and not snapshot['do_collision_test'], snapshot


def prompt_state(world):
    found = []
    for widget in unreal.WidgetLibrary.get_all_widgets_of_class(world, unreal.DepthAbyssPromptWidget, False):
        item = {'visible': bool(widget.is_visible()), 'widget': widget.get_path_name()}
        try:
            text = widget.get_editor_property('prompt_text')
            item['text'] = str(text.get_text()) if text else None
        except Exception as exc:
            item['text_read_error'] = str(exc)
        found.append(item)
    return found


def finish(error=None):
    if ctx['finished']:
        return
    ctx['finished'] = True
    unreal.unregister_slate_post_tick_callback(ctx['handle'])
    if error:
        try:
            ctx['portal'].cancel_entry()
        except Exception:
            pass
    out = {k: v for k, v in ctx.items() if k not in ('handle', 'portal', 'screenshot_task', 'busy', 'next')}
    out.update(error=error,
               scope='actual portal confirmation methods used by E/Enter; hardware key dispatch not automated',
               direct_destination_teleport_used=False,
               camera_parameters_changed_by_script=False,
               successful_test_leaves_player_at_underground_destination=True)
    (P / 'PortalRuntime.json').write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('DEPTH_PORTAL_RUNTIME_DONE ' + str(error))


def begin_capture(name):
    file = C / (name + '.png')
    ctx['capture_file'] = str(file)
    ctx['capture_wall_time'] = time.time()
    ctx['capture_started'] = time.monotonic()
    # Automation screenshot may pump nested Slate ticks. tick()'s busy guard stays true here.
    ctx['screenshot_task'] = unreal.AutomationLibrary.take_high_res_screenshot(1280, 720, str(file))


def png_ready(filename, since):
    path = Path(filename)
    if not path.exists() or path.stat().st_mtime < since - 0.5:
        return None
    data = path.read_bytes()
    if len(data) < 32 or data[:8] != b'\x89PNG\r\n\x1a\n' or b'IEND' not in data[-16:]:
        return None
    return {'file': str(path), 'width': struct.unpack('>I', data[16:20])[0],
            'height': struct.unpack('>I', data[20:24])[0], 'bytes': len(data)}


def tick(_delta):
    if ctx['finished'] or ctx['busy']:
        return
    ctx['busy'] = True
    try:
        now = time.monotonic()
        if now < ctx['next']:
            return
        assert now - ctx['started'] < 180.0, 'Portal runtime test exceeded 180 seconds.'
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        pawn = unreal.GameplayStatics.get_player_character(world, 0) if world else None
        if not pawn:
            assert ctx['phase'] == 'wait_player', 'PIE player vanished during portal test.'
            assert now - ctx['started'] < 30.0, 'No PIE player after 30 seconds.'
            return
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        move = pawn.get_component_by_class(unreal.CharacterMovementComponent)
        phase = ctx['phase']

        if phase == 'wait_player':
            portals = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.DepthAbyssPortalProxy)
            assert len(portals) == 1, 'Expected exactly one candidate abyss proxy.'
            portal = portals[0]
            ctx['portal'] = portal
            ctx['initial_entry_count'] = int(portal.get_editor_property('confirmed_entry_count'))
            ctx['initial_position_cm'] = vec(pawn.get_actor_location())
            portal.cancel_entry()
            pc.stop_movement()
            move.stop_movement_immediately()
            pawn.set_actor_location(sc.world([418.2, 146.0, sc.h(418.2, 146.0) + 1.1]), False, True)
            ctx.update(phase='settle_apron', next=now + 3.0, stage_started=now)
            return

        portal = ctx['portal']
        if phase == 'settle_apron':
            if not move.is_moving_on_ground():
                assert now - ctx['stage_started'] < 10.0, 'Pawn did not settle on the abyss safety apron.'
                ctx['next'] = now + 0.2
                return
            position = vec(pawn.get_actor_location())
            assert portal.get_editor_property('confirmed_entry_count') == ctx['initial_entry_count'], 'Approach triggered automatic entry.'
            assert not portal.get_editor_property('awaiting_confirmation'), 'Approach triggered automatic confirmation.'
            assert portal.can_request_entry(), 'Safety apron is outside the usable portal range or character cannot interact.'
            ctx['camera_before'] = camera_state(world, pawn)
            assert_camera_baseline(ctx['camera_before'])
            ctx['checks'].append({'name': 'approach_no_autoentry', 'passed': True, 'position_cm': position})
            assert not portal.confirm_entry(), 'ConfirmEntry bypassed the first confirmation stage.'
            assert distance(position, vec(pawn.get_actor_location())) < 0.1
            ctx['checks'].append({'name': 'confirm_without_request_refused', 'passed': True})
            assert portal.request_entry_confirmation()
            assert portal.get_editor_property('awaiting_confirmation')
            ctx.update(phase='cancel_confirmation', next=now + 0.35)
            return

        if phase == 'cancel_confirmation':
            assert portal.get_editor_property('awaiting_confirmation')
            portal.cancel_entry()
            assert not portal.get_editor_property('awaiting_confirmation')
            assert not portal.confirm_entry(), 'Cancelled confirmation still allowed entry.'
            assert portal.get_editor_property('confirmed_entry_count') == ctx['initial_entry_count']
            ctx['checks'].append({'name': 'cancel_invalidates_confirmation', 'passed': True})
            assert portal.request_entry_confirmation()
            ctx.update(phase='capture_confirmation', next=now + 0.5)
            return

        if phase == 'capture_confirmation':
            assert portal.get_editor_property('awaiting_confirmation')
            ctx['confirmation_prompt'] = prompt_state(world)
            assert any(q['visible'] for q in ctx['confirmation_prompt']), 'No visible native confirmation widget.'
            ctx['phase'] = 'wait_confirmation_capture'
            begin_capture('17_AbyssConfirmation')
            ctx['next'] = time.monotonic() + 0.25
            return

        if phase in ('wait_confirmation_capture', 'wait_entry_capture'):
            image = png_ready(ctx['capture_file'], ctx['capture_wall_time'])
            task = ctx.get('screenshot_task')
            if image and (not task or task.is_task_done()):
                assert (image['width'], image['height']) == (1280, 720), image
                image['contains_umg_not_guaranteed'] = True
                ctx['captures'].append(image)
                ctx.pop('screenshot_task', None)
                if phase == 'wait_entry_capture':
                    ctx['checks'].append({'name': 'entry_automation_capture_created', 'passed': True})
                    finish()
                    return
                # High-resolution scene captures omit Slate. Also request a genuine UI screenshot
                # at the current viewport size; never resize/reconfigure the player's camera.
                ui_file = C / '17_AbyssConfirmation_UI.png'
                ctx['ui_file'] = str(ui_file)
                ctx['ui_wall_time'] = time.time()
                ctx['ui_started'] = time.monotonic()
                pc_path = ui_file.as_posix()
                unreal.SystemLibrary.execute_console_command(world, 'Shot showui filename="' + pc_path + '" -nosuffix', pc)
                ctx.update(phase='wait_confirmation_ui', next=time.monotonic() + 0.25)
                return
            assert now - ctx['capture_started'] < 40.0, 'Automation screenshot did not complete/write a new PNG.'
            ctx['next'] = now + 0.25
            return

        if phase == 'wait_confirmation_ui':
            image = png_ready(ctx['ui_file'], ctx['ui_wall_time'])
            if not image and now - ctx['ui_started'] < 10.0:
                ctx['next'] = now + 0.25
                return
            if image:
                image['native_showui_request'] = True
                ctx['captures'].append(image)
            else:
                ctx['warnings'].append('Native ShowUI screenshot file missing; the 1280x720 scene capture alone does not prove visible prompt text.')
            assert portal.get_editor_property('awaiting_confirmation')
            destination_actor = portal.get_editor_property('underground_destination_actor')
            destination = destination_actor.get_actor_location() if destination_actor else portal.get_editor_property('underground_destination')
            ctx['destination_cm'] = vec(destination)
            ctx['before_confirm_position_cm'] = vec(pawn.get_actor_location())
            assert portal.confirm_entry(), 'Confirmed entry failed: ' + str(portal.get_editor_property('last_entry_error'))
            ctx['position_immediately_after_confirm_cm'] = vec(pawn.get_actor_location())
            assert portal.get_editor_property('confirmed_entry_count') == ctx['initial_entry_count'] + 1
            assert not portal.get_editor_property('awaiting_confirmation')
            ctx.update(phase='settle_underground', next=now + 3.0, stage_started=now)
            return

        if phase == 'settle_underground':
            if not move.is_moving_on_ground():
                assert now - ctx['stage_started'] < 10.0, 'Pawn did not become grounded after confirmed entry.'
                ctx['next'] = now + 0.2
                return
            position = vec(pawn.get_actor_location())
            error = distance(position, ctx['destination_cm'])
            assert error < 160.0, ('Entry landed far from configured destination', error, position, ctx['destination_cm'])
            assert position[2] < -5000.0, 'Expected underground destination below -50m.'
            ctx['camera_after'] = camera_state(world, pawn)
            assert_camera_baseline(ctx['camera_after'])
            for name in ('arm_length', 'fov', 'projection_mode', 'ortho_width', 'absolute_rotation', 'do_collision_test', 'enable_camera_lag'):
                assert ctx['camera_after'][name] == ctx['camera_before'][name], (name, ctx['camera_before'], ctx['camera_after'])
            ctx['checks'].append({'name': 'confirmed_one_way_entry_grounded_camera_unchanged', 'passed': True,
                                  'position_cm': position, 'destination_error_cm': error,
                                  'entry_count': int(portal.get_editor_property('confirmed_entry_count')),
                                  'grounded': True})
            ctx['phase'] = 'wait_entry_capture'
            begin_capture('18_DeepEntry')
            ctx['next'] = time.monotonic() + 0.25
            return
        raise RuntimeError('Unknown portal test phase: ' + phase)
    except Exception:
        finish(traceback.format_exc())
    finally:
        ctx['busy'] = False


ctx['handle'] = unreal.register_slate_post_tick_callback(tick)
unreal.log('DEPTH_PORTAL_RUNTIME_ARMED')
