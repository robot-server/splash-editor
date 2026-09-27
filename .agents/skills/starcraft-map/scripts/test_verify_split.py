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
