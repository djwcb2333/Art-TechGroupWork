"""Read-only collision check. Editor Visibility includes a deliberate no-hole editing shape.
Camera channel in editor, and Visibility in PIE, test the gameplay heightfield instead.
"""
import json
from pathlib import Path
import unreal


def main():
    p = Path(__file__).parent
    subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    game_world = subsystem.get_game_world()
    world = game_world or subsystem.get_editor_world()
    assert world and '/Maps/DoughWorldDepth/' in world.get_path_name(), world
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    landscapes = [a for a in actors if isinstance(a, unreal.LandscapeProxy)]
    ignored = [a for a in actors if a not in landscapes]
    data = json.loads((p / 'TerrainDesign/depth_supplement.json').read_text(encoding='utf-8'))
    queries = [(q['source_id'], q['xy_m'], True) for q in data['holes']]
    queries += [('bridge_underpass', [393.3, 170.0], True), ('solid_corner', [20.0, 20.0], False), ('solid_south', [250.0, 420.0], False)]
    out = {'world': world.get_path_name(), 'pie': bool(game_world), 'landscapes': [a.get_path_name() for a in landscapes], 'rays': []}
    camera_channel = getattr(unreal.TraceTypeQuery, 'ECC_CAMERA', None)
    assert camera_channel is not None, 'Read trace enum names first: ' + repr(dir(unreal.TraceTypeQuery))
    for label, xy, expect_hole in queries:
        start = unreal.Vector((xy[0] - 250) * 100, (xy[1] - 250) * 100, 12000)
        end = unreal.Vector(start.x, start.y, -12000)
        for complex_query in (False, True):
            hit = unreal.SystemLibrary.line_trace_single(world, start, end, camera_channel,
                    complex_query, ignored, unreal.DrawDebugTrace.NONE)
            row = {'id': label, 'channel': 'Camera', 'complex': complex_query,
                   'expected_hole': expect_hole, 'hit': bool(hit), 'passed': bool(hit) != expect_hole}
            if hit:
                values = hit.to_tuple()
                row.update(actor=values[9].get_path_name() if values[9] else None,
                           component=values[10].get_path_name() if values[10] else None,
                           z_m=float(values[4].z) / 100.0)
            out['rays'].append(row)
        if game_world:
            hit = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.ECC_VISIBILITY,
                    True, ignored, unreal.DrawDebugTrace.NONE)
            out['rays'].append({'id': label, 'channel': 'Visibility in PIE', 'complex': True,
                               'expected_hole': expect_hole, 'hit': bool(hit), 'passed': bool(hit) != expect_hole})
    out['all_passed'] = all(row['passed'] for row in out['rays'])
    output = p / ('HoleGameCollision_PIE.json' if game_world else 'HoleGameCollision_EditorCamera.json')
    output.write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('DEPTH_GAMEPLAY_HOLE_CHECK ' + str(out['all_passed']))


main()
