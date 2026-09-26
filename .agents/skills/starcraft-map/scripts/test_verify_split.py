"""verify_map.assess 가 구조 결함만 실패로 세는지 확인한다."""

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
