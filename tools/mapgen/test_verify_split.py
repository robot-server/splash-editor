"""verify_map.assess 가 구조 결함만 실패로 세는지 확인한다."""

import inspect
import unittest

import verify_map


def _structural():
    return {
        "outside_units": 2,
        "blocked_pct": 80,
        "burn_hp_zero": ["Command Center"],
        "inactive_progress": ["Player 8"],
        "bad_resource_bits": 3,
        "starts": 0,
        "starts_unwalkable": True,
        "extension": ".scx",
        "version": "StarCraft",
        "deaths_var_placed": ["Marine"],
        "modify_unit_reversed": 1,
    }


def _concept():
    return {
        "outside_units": 0,
        "blocked_pct": 12,
        "burn_hp_zero": [],
        "inactive_progress": [],
        "bad_resource_bits": 0,
        "starts": 4,
        "starts_unwalkable": False,
        "extension": ".scx",
        "version": "Brood War",
        "deaths_var_placed": [],
        "modify_unit_reversed": 0,
        "symmetric": False,
        "briefing": "",
        "forces": 0,
        "switches": 0,
        "sounds": 0,
        "timers": [("Countdown Timer", 30), ("Elapsed Time", 45)],
        "unis_all_zero": ["Marine"],
    }


class ParserAndStructure(unittest.TestCase):
    def test_inactive_slot_survives_wide_korean(self):
        text = "\n".join([
            "  P 1  저그        열림          세력 1",
            "  P 2  프로토스  사용 안 함  세력 1",
            "  P 3  선택 가능  닫힘      세력 2",
        ])
        rows = verify_map.parse_player_rows(text)
        self.assertEqual(
            [(row["player"], row["slot"]) for row in rows],
            [(1, "열림"), (2, "사용 안 함"), (3, "닫힘")])

    def test_zero_hp_only_burn_buildings(self):
        self.assertIn(106, verify_map._BURN_BUILDING_IDS)
        self.assertNotIn(0, verify_map._BURN_BUILDING_IDS)
        self.assertNotIn(174, verify_map._BURN_BUILDING_IDS)
        self.assertNotIn(200, verify_map._BURN_BUILDING_IDS)

    def test_trigger_structure_is_not_gated_on_classify(self):
        text = 'Set Deaths("Player 1", "Marine", Set To, 1);\n'
        findings = verify_map.trigger_structure_findings(
            text, {"Marine": 1})
        self.assertEqual(verify_map.failure_count(findings), 1)
        self.assertIn("trigger_structure_findings",
                      inspect.getsource(verify_map.check_basics))


class PlacementAndProgress(unittest.TestCase):
    def test_unwalkable_resource_and_critter_fail(self):
        def prop_at(tx, ty):
            if (tx, ty) == (3, 3):
                return (0, 0, 0)
            return (1, 1, 1)

        units = [
            {"name": "Mineral Field (Type 1)", "tx": 3, "ty": 3, "role": "resource"},
            {"name": "Rhynadon (Badlands Critter)", "tx": 3, "ty": 3, "role": "ground"},
        ]
        faults = verify_map.placement_faults(units, prop_at)
        self.assertTrue(any("건설 불가" in item or "못 걷는" in item for item in faults))
        findings = verify_map.assess({
            "outside_units": 0, "blocked_pct": 10, "burn_hp_zero": [],
            "inactive_progress": [], "bad_resource_bits": 0, "starts": 2,
            "starts_unwalkable": False, "extension": ".scx",
            "version": "Brood War", "deaths_var_placed": [],
            "modify_unit_reversed": 0, "illegal_placement": faults,
        })
        self.assertGreater(verify_map.failure_count(findings), 0)

    def test_zero_weapon_damage_fails(self):
        faults = verify_map.weapon_zero_faults({0: 6, 130: 0}, {0: 0}, True)
        self.assertTrue(faults)
        self.assertEqual(verify_map.weapon_zero_faults({0: 6}, {0: 6}, True), [])
        self.assertEqual(verify_map.weapon_zero_faults({0: 6}, {0: 0}, False), [])
        # 무기 0은 가우스다. 빈 값으로 읽으면 피해 0을 검사하지 않는다.
        rows = [{"ground_weapon": 0, "ground_damage": 6,
                 "air_weapon": 130, "air_damage": 0}]
        parsed = verify_map.dat_weapon_damage(rows)
        self.assertEqual(parsed.get(0), 6)
        self.assertNotIn(130, parsed)
        self.assertTrue(verify_map.weapon_zero_faults(parsed, {0: 0}, True))
        findings = verify_map.assess({
            "outside_units": 0, "blocked_pct": 10, "burn_hp_zero": [],
            "inactive_progress": [], "bad_resource_bits": 0, "starts": 1,
            "starts_unwalkable": False, "extension": ".scx",
            "version": "Brood War", "deaths_var_placed": [],
            "modify_unit_reversed": 0, "weapon_zero": faults,
        })
        self.assertGreater(verify_map.failure_count(findings), 0)

    def test_initial_victory_from_empty_command_fails(self):
        text = """
Trigger("Player 1"){
Conditions:
	Command("Player 2", "Zerg Zergling", At most, 0);

Actions:
	Victory();
}
"""
        faults = verify_map.initial_progress_faults(text, {})
        self.assertTrue(faults)
        quiet = """
Trigger("Player 1"){
Conditions:
	Always();

Actions:
	Set Deaths("Player 8", "Flag", Set To, 10);
}
"""
        self.assertEqual(verify_map.initial_progress_faults(quiet, {}), [])
        owned = """
Trigger("Player 1","Player 2"){
Conditions:
	Command("Current Player", "Terran Vulture", Exactly, 0);

Actions:
	Defeat();
}
"""
        placed = {
            ("Player 1", "Terran Vulture"): 1,
            ("Player 2", "Terran Vulture"): 1,
        }
        self.assertEqual(verify_map.initial_progress_faults(owned, placed), [])
        separated = owned.replace(
            "Trigger(",
            "//-----------------------------------------------------------------//\n\nTrigger(",
            1)
        placed_with_computer = dict(placed)
        placed_with_computer[("Player 3", "Start Location")] = 1
        self.assertEqual(
            verify_map.initial_progress_faults(separated, placed_with_computer), [])
        lives = """
Trigger("Player 1","Player 2"){
Conditions:
	Always();

Actions:
	Set Deaths("Current Player", "Flag", Set To, 3);
}

//-----------------------------------------------------------------//

Trigger("Player 1","Player 2"){
Conditions:
	Deaths("Current Player", "Flag", At most, 0);

Actions:
	Defeat();
}
"""
        self.assertEqual(verify_map.initial_progress_faults(lives, {}), [])
        spawn = """
Trigger("Player 3"){
Conditions:
	Command("Player 3", "Zerg Zergling", At most, 5);

Actions:
	Create Unit("Player 3", "Zerg Zergling", 1, "검문소");
	Display Text Message(Always Display, "추격이 붙습니다.");
}
"""
        self.assertTrue(verify_map.initial_progress_faults(spawn, {}))
        occupied = """
Trigger("Player 1"){
Conditions:
	Command("Player 1", "Men", At most, 0);

Actions:
	Create Unit("Player 1", "Terran Marine", 1, "길목");
}
"""
        self.assertEqual(verify_map.initial_progress_faults(
            occupied, {("Player 1", "Terran Marine"): 4}), [])
        self.assertTrue(verify_map.initial_progress_faults(occupied, {}))
        armed = """
Trigger("All players"){
Conditions:
	Always();

Actions:
	Set Switch("추격시작", set);
}
"""
        gated = """
Trigger("Player 3"){
Conditions:
	Switch("추격시작", set);
	Command("Player 3", "Zerg Zergling", At most, 5);

Actions:
	Create Unit("Player 3", "Zerg Zergling", 1, "검문소");
}
"""
        # 스위치가 꺼진 채로는 생성되지 않는다. 켜진 뒤에는 생성된다.
        self.assertEqual(verify_map.initial_progress_faults(gated, {}), [])
        self.assertTrue(verify_map.initial_progress_faults(
            armed + "\n\n//-----------------------------------------------------------------//\n\n" + gated, {}))
        self.assertTrue(verify_map.initial_progress_faults(
            owned, {("Player 1", "Terran Vulture"): 1}))
        findings = verify_map.assess({
            "outside_units": 0, "blocked_pct": 10, "burn_hp_zero": [],
            "inactive_progress": [], "bad_resource_bits": 0, "starts": 1,
            "starts_unwalkable": False, "extension": ".scx",
            "version": "Brood War", "deaths_var_placed": [],
            "modify_unit_reversed": 0, "initial_progress": faults,
        })
        self.assertGreater(verify_map.failure_count(findings), 0)

    def test_open_ring_is_not_a_choke(self):
        walk = [[1] * 30 for _ in range(30)]
        self.assertTrue(verify_map.choke_faults(walk, [(15, 15)]))
        walled = [[1] * 30 for _ in range(30)]
        for y in range(30):
            for x in range(30):
                dist = max(abs(x - 15), abs(y - 15))
                if 8 <= dist <= 12 and not (abs(y - 15) <= 1 and x >= 15):
                    walled[y][x] = 0
        self.assertEqual(verify_map.choke_faults(walled, [(15, 15)]), [])

    def test_building_footprint_checks_every_cell(self):
        def prop_at(tx, ty):
            if (tx, ty) == (5, 4):
                return (1, 0, 0)
            return (1, 1, 1)

        units = [{"name": "Terran Barracks", "tx": 4, "ty": 4,
                  "role": "building", "foot": (4, 3)}]
        faults = verify_map.placement_faults(units, prop_at)
        self.assertTrue(any("건설 불가" in item for item in faults))
        findings = verify_map.assess({
            "outside_units": 0, "blocked_pct": 10, "burn_hp_zero": [],
            "inactive_progress": [], "bad_resource_bits": 0, "starts": 1,
            "starts_unwalkable": False, "extension": ".scx",
            "version": "Brood War", "deaths_var_placed": [],
            "modify_unit_reversed": 0, "illegal_placement": faults,
        })
        self.assertGreater(verify_map.failure_count(findings), 0)

    def test_mixed_height_resource_fails(self):
        def prop_at(tx, ty):
            return (2 if tx >= 3 else 1, 1, 1)

        units = [{"name": "Mineral Field (Type 1)", "tx": 3, "ty": 3,
                  "role": "resource"}]
        faults = verify_map.placement_faults(units, prop_at)
        self.assertTrue(any("높이" in item for item in faults))

    def test_base_needs_mineral_gas_and_addon_pad(self):
        def prop_at(tx, ty):
            return (1, 1, 1)

        gasless = [[{"name": "Mineral Field (Type 1)", "tx": 15, "ty": 15}]]
        self.assertTrue(verify_map.melee_base_faults(gasless, prop_at))
        both = [[
            {"name": "Mineral Field (Type 1)", "tx": 15, "ty": 15},
            {"name": "Vespene Geyser", "tx": 15, "ty": 18},
        ]]
        self.assertEqual(verify_map.melee_base_faults(both, prop_at), [])
        # 옆 멀티는 한 기지로 묶이지 않아야 각자 패드를 가질 수 있다.
        split = [
            {"name": "Mineral Field (Type 1)", "tx": 10, "ty": 10},
            {"name": "Vespene Geyser", "tx": 10, "ty": 13},
            {"name": "Mineral Field (Type 1)", "tx": 24, "ty": 10},
            {"name": "Vespene Geyser", "tx": 24, "ty": 13},
        ]
        self.assertEqual(
            verify_map.melee_base_faults(verify_map._resource_clusters(split), prop_at),
            [])

    def test_ring_keeps_protected_resource_cell(self):
        import scmap
        cells = scmap.ring_wall_cells(40, 40, [(15, 15)], set(), {(23, 15)})
        self.assertNotIn((23, 15), cells)
        self.assertIn((23, 16), cells)

    def test_segment_corridor_stays_on_the_line(self):
        import scmap
        cells = set(scmap.segment_corridor(0, 0, 6, 0, 1))
        self.assertIn((3, 0), cells)
        self.assertIn((3, 1), cells)
        self.assertNotIn((3, 3), cells)

    def test_reopen_only_the_walled_path(self):
        import scmap
        pre = [[1] * 5 for _ in range(5)]
        pre[2][0] = 0
        cells = scmap.reopen_cells(pre, {(2, 2), (0, 0)}, [(2, 2)])
        self.assertIn((2, 2), cells)
        self.assertNotIn((0, 0), cells)
        self.assertNotIn((2, 0), cells)

    def test_ray_gap_is_only_toward_the_next_base(self):
        import scmap
        gap = set(scmap.ray_gap_cells((15, 15), (15, 40)))
        self.assertIn((15, 24), gap)
        self.assertNotIn((23, 15), gap)
        walled = scmap.ring_wall_cells(40, 40, [(15, 15)], gap, set())
        self.assertNotIn((15, 24), walled)
        self.assertIn((23, 15), walled)

    def test_halo_opens_beside_paths_not_the_whole_map(self):
        import scmap
        props = {1: (0, 1, 1, 0, 0xFFFF), 2: (0, 0, 0, 0, 0)}
        grid = [[2] * 30 for _ in range(30)]
        for y in range(14, 16):
            for x in range(14, 16):
                grid[y][x] = 1
        out, opened = scmap.halo_open_grid(grid, props, 1, limit=60)
        self.assertGreater(opened, 0)
        self.assertLessEqual(scmap.blocked_mini_pct(out, props), 60)
        self.assertEqual(out[1][1], 2)
        self.assertEqual(out[14][13], 1)
        held, _n = scmap.halo_open_grid(grid, props, 1, limit=60, protect={(14, 13)})
        self.assertEqual(held[13][14], 2)
        self.assertEqual(held[14][13], 1)
        full = [[1] * 5 for _ in range(5)]
        same, none_opened = scmap.halo_open_grid(full, props, 1, limit=60)
        self.assertEqual(none_opened, 0)
        self.assertEqual(same[0][0], 1)

    def test_chase_lane_keeps_corners_out(self):
        import make_usemap
        x, y, w, h = make_usemap.chase_lane_rect(128, 96)
        self.assertLess(y + h, 96)
        self.assertGreater(y, 0)
        self.assertLessEqual(y, int(0.50 * 96))
        self.assertGreaterEqual(y + h, int(0.50 * 96))
        self.assertNotIn(0, range(y, y + h))
        # 검문 틈의 한가운데는 벽 사각형 밖이다.
        cx, cy = int(0.25 * 128), int(0.50 * 96)
        inside = False
        for rx0, ry0, rx1, ry1 in make_usemap.WALLS:
            if rx0 * 128 <= cx < rx1 * 128 and ry0 * 96 <= cy < ry1 * 96:
                inside = True
        self.assertFalse(inside)

    def test_illegal_footprint_moves_or_uses_isom(self):
        import scmap

        def prop_at(tx, ty):
            if (tx, ty) == (5, 5):
                return (1, 0, 0, 0, 0)
            return (1, 1, 1, 0, 0xFFFF)

        moved = scmap.footprint_remedy(5, 5, 1, 1, prop_at, 20, 20, 7)
        self.assertEqual(moved["action"], "move")
        self.assertNotEqual(moved["at"], (5, 5))
        self.assertNotIn("tiles", moved)

        def nowhere(tx, ty):
            return (0, 0, 0, 0, 0)

        def other_height(tx, ty):
            if (tx, ty) == (3, 3):
                return (1, 0, 0, 0, 0)
            return (2, 1, 1, 0, 0xFFFF)

        stayed = scmap.footprint_remedy(3, 3, 1, 1, other_height, 10, 10, 4)
        self.assertEqual(stayed["action"], "isom")
        painted = scmap.footprint_remedy(4, 4, 2, 1, nowhere, 20, 20, 7)
        self.assertEqual(painted["action"], "isom")
        self.assertTrue(painted["strokes"])
        self.assertTrue(all(stroke[2] == 7 and stroke[3] == 1 for stroke in painted["strokes"]))
        self.assertNotIn("tiles", painted)

    def test_shop_preview_matches_create_and_price_is_not_in_the_name(self):
        import scmap
        actions = ['Create Unit("{player}", "Protoss Zealot", 1, "{home}")']
        self.assertEqual(scmap.shop_preview_type(actions, "Protoss Gateway"), "Protoss Zealot")
        self.assertEqual(scmap.shop_preview_type(
            ["Modify Unit Shield Points(\"{player}\", \"Men\", 100, 0, \"{shop}\")"],
            "Protoss Shield Battery"), "Protoss Shield Battery")
        names = scmap.merge_display_names(
            {"Terran Medic": "의무병 · 분대 회복",
             "Protoss Shield Battery": "보호막 회복 · 미네랄 100",
             "Terran Marine": "해병 돌격"},
            {"Protoss Zealot": "질럿 80광물"})
        self.assertEqual(names["Terran Medic"], "의무병 · 분대 회복")
        self.assertEqual(names["Protoss Shield Battery"], "보호막 회복")
        self.assertEqual(names["Terran Marine"], "해병 돌격")
        self.assertEqual(names["Protoss Zealot"], "질럿")
        self.assertNotIn("광물", names["Protoss Zealot"])
        spot = scmap.price_mineral_spot(5, 5, lambda cell: True, {(5, 5)})
        self.assertNotEqual(spot, (5, 5))
        self.assertEqual(max(abs(spot[0] - 5), abs(spot[1] - 5)), 1)
        self.assertEqual(scmap.price_mineral_spot(
            5, 5, lambda cell: cell == (5, 7), set()), (5, 7))
        self.assertEqual(scmap.shop_preview_count(
            ['Create Unit("{player}", "Protoss Dragoon", 2, "{center}")']), 2)
        self.assertEqual(scmap.shop_preview_count(
            ['Modify Unit Shield Points("{player}", "Men", 100, 0, "{shop}")']), 1)
        self.assertEqual(scmap.middle_group_tile([16, 23, 31, 22]), 23)
        self.assertEqual(scmap.reveal_scope("quiz"), "board")
        self.assertEqual(scmap.reveal_scope("wave_defense"), "board")
        self.assertEqual(scmap.reveal_scope("chase"), "start")
        self.assertEqual(scmap.reveal_scope("hide_seek"), "start")
        self.assertEqual(scmap.reveal_scope("square_defense"), "board")
        self.assertEqual(scmap.reveal_scope("loadout"), "board")
        self.assertEqual(scmap.reveal_scope("control"), "board")
        self.assertEqual(scmap.reveal_scope("micro_trial"), "board")
        self.assertEqual(scmap.reveal_scope("rpg"), "start")
        self.assertEqual(scmap.reveal_scope("zombie"), "start")
        self.assertEqual(scmap.reveal_scope("room_escape"), "start")


class AssessSplit(unittest.TestCase):
    def test_structural_fails(self):
        findings = verify_map.assess(_structural())
        self.assertGreater(verify_map.failure_count(findings), 0)
        text = "\n".join(line for _, line in findings)
        self.assertIn("맵 밖 유닛", text)
        self.assertIn("80", text)
        self.assertIn("체력이 0", text)
        self.assertIn("Modify Unit", text)

    def test_concept_does_not_fail_but_is_reported(self):
        findings = verify_map.assess(_concept())
        self.assertEqual(verify_map.failure_count(findings), 0)
        text = "\n".join(line for _, line in findings)
        self.assertIn("대칭", text)
        self.assertIn("브리핑이 없습니다", text)
        self.assertIn("포스 0개", text)
        self.assertIn("스위치 0개", text)
        self.assertIn("소리 0개", text)
        self.assertIn("Countdown Timer 30", text)
        self.assertIn("Elapsed Time 45", text)
        self.assertIn("통째로 0", text)
        self.assertNotIn("Null", text)

    def test_timer_number_alone_is_not_a_failure(self):
        findings = verify_map.assess({"timers": [("Countdown Timer", 11)]})
        self.assertEqual(verify_map.failure_count(findings), 0)
        self.assertTrue(any("11" in line for _, line in findings))


if __name__ == "__main__":
    unittest.main()
