#!/usr/bin/env python3
"""프로필 두 벌이 생성기 문구·수치를 갈라 놓는지 확인한다."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import recipe_config as profile
import make_control
import make_hide_seek
import make_loadout_gauntlet
import make_micro_trial
import make_quiz
import make_room_escape
import make_rpg
import make_square_defense
import make_usemap
import make_wave_defense
import make_zombie
import scmap
from scmap import CliError

GENERATORS = [
    "make_control.py",
    "make_hide_seek.py",
    "make_loadout_gauntlet.py",
    "make_micro_trial.py",
    "make_quiz.py",
    "make_room_escape.py",
    "make_rpg.py",
    "make_square_defense.py",
    "make_usemap.py",
    "make_wave_defense.py",
    "make_zombie.py",
]


def _base_quiz(name, briefing, minerals, lives, prompt):
    return {
        "genre": "quiz",
        "language": "ko",
        "map": {"name": name, "description": briefing, "tileset": "jungle",
                "size": [64, 64], "seed": 3},
        "force_names": ["가", "나", "다", "라"],
        "text": {
            "objectives": briefing,
            "briefing": [briefing],
            "portrait": "Terran Civilian",
            "briefing_hold_ms": 400,
            "messages": {
                "start": briefing, "directions": briefing, "correct": briefing,
                "wrong": briefing, "no_answer": briefing, "out_of_lives": briefing,
                "survived": briefing, "leaderboard": briefing, "question": "{prompt}",
            },
        },
        "rules": {"players": 1, "lives": lives, "seconds": 9},
        "players": {"race": "terran"},
        "units": {
            "life_counter": "Dark Swarm", "question_counter": "Scanner Sweep",
            "shown_counter": "Protoss Scarab", "judged_counter": "Protoss Interceptor",
            "selection": "Terran Civilian", "o_marker": "Terran Beacon",
            "x_marker": "Protoss Beacon",
        },
        "labels": {"lobby": "대기", "pad_o": "참", "pad_x": "거짓", "start_prefix": "자리"},
        "questions": [{"prompt": prompt, "answer": "O"}],
        "starting_resources": {"minerals": minerals, "gas": 0},
        "upgrades": {"which": [], "free_levels": 0, "max_level": 0,
                     "minerals": 0, "gas": 0, "time": 1},
        "technologies": {"which": [], "available": "none", "researched": "none",
                         "minerals": 0, "gas": 0, "time": 1, "energy": 0},
        "unit_settings": {},
    }


def _base_chase(name, briefing, minerals, pursuers, timer):
    return {
        "genre": "chase",
        "language": "ko",
        "map": {"name": name, "description": briefing, "tileset": "jungle",
                "size": [96, 96], "seed": 9},
        "force_names": ["가", "나", "다", "라"],
        "text": {
            "objectives": briefing,
            "briefing": [briefing],
            "portrait": "Terran Civilian",
            "briefing_hold_ms": 500,
            "messages": {
                "intro": briefing, "checkpoint": briefing + " 구간",
                "pursuit": briefing + " 추격", "victory": briefing + " 성공",
                "defeat": briefing + " 실패",
            },
        },
        "rules": {"players": 1, "pursuer_count": pursuers, "timer_seconds": timer},
        "players": {"race": "terran", "enemy_race": "zerg"},
        "units": {"runner": "Terran Civilian", "pursuer": "Zerg Zergling"},
        "labels": {"start": "출발", "checkpoint": "관문", "goal": "출구"},
        "starting_resources": {"minerals": minerals, "gas": 1},
        "upgrades": {"which": [], "free_levels": 0, "max_level": 0,
                     "minerals": 0, "gas": 0, "time": 1},
        "technologies": {"which": [], "available": "none", "researched": "none",
                         "minerals": 0, "gas": 0, "time": 1, "energy": 0},
        "unit_settings": {},
    }


class RecipeSplitTest(unittest.TestCase):
    def test_config_is_required(self):
        for name in GENERATORS:
            script = HERE / name
            help_run = subprocess.run(
                [sys.executable, str(script), "--help"],
                capture_output=True, text=True)
            self.assertEqual(help_run.returncode, 0, help_run.stderr)
            self.assertIn("--config", help_run.stdout)
            missing = subprocess.run(
                [sys.executable, str(script), "unused.scx"],
                capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0, name)
            self.assertIn("--config", missing.stderr)

    def test_two_profiles_change_quiz_and_chase(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            paths = []
            for spec in (
                _base_quiz("알파 퀴즈", "알파 안내문", 111, 2, "알파 문항"),
                _base_quiz("베타 퀴즈", "베타 안내문", 222, 5, "베타 문항"),
            ):
                path = root / f"{spec['map']['name']}.json"
                path.write_text(json.dumps(spec), encoding="utf-8")
                paths.append(path)
            left = profile.load_profile(str(paths[0]), "quiz")
            right = profile.load_profile(str(paths[1]), "quiz")
            self.assertEqual(left["map"]["name"], "알파 퀴즈")
            self.assertEqual(right["map"]["name"], "베타 퀴즈")
            self.assertNotEqual(left["text"]["briefing"], right["text"]["briefing"])
            def pairs(cfg):
                return [(q["prompt"], q["answer"]) for q in cfg["questions"]]
            joined_l = make_quiz.build_triggers(left, pairs(left), left["labels"])
            joined_r = make_quiz.build_triggers(right, pairs(right), right["labels"])
            self.assertIn("알파 문항", joined_l)
            self.assertIn("베타 문항", joined_r)
            self.assertIn('"Dark Swarm", Set To, 2', joined_l)
            self.assertIn('"Dark Swarm", Set To, 5', joined_r)
            self.assertNotIn("베타 문항", joined_l)

            chase_paths = []
            for spec in (
                _base_chase("알파 추격", "알파 추격문", 30, 3, 40),
                _base_chase("베타 추격", "베타 추격문", 90, 7, 80),
            ):
                path = root / f"{spec['map']['name']}.json"
                path.write_text(json.dumps(spec), encoding="utf-8")
                chase_paths.append(path)
            c0 = profile.load_profile(str(chase_paths[0]), "chase")
            c1 = profile.load_profile(str(chase_paths[1]), "chase")
            t0 = make_usemap.chase_triggers(c0, "Player 2")
            t1 = make_usemap.chase_triggers(c1, "Player 2")
            self.assertIn("알파 추격문", t0)
            self.assertIn("베타 추격문", t1)
            self.assertIn("At most, 2", t0)
            self.assertIn("At most, 6", t1)
            self.assertIn(', 1, "', t0)
            self.assertIn("Set To, 40", t0)
            self.assertIn("Set To, 80", t1)
            ores = profile.resource_actions(c0, ["Player 1"])
            self.assertIn("30", ores[0])
            self.assertNotIn("90", ores[0])

    def test_examples_load_and_builders_follow_two_profiles(self):
        bundled = {}
        for path in (HERE / "recipe_profiles").glob("*.json"):
            raw = json.loads(path.read_text(encoding="utf-8"))
            loaded = profile.load_profile(str(path), raw["genre"])
            bundled[raw["genre"]] = loaded
        self.assertGreaterEqual(len(bundled), 10)

        def as_text(value):
            if isinstance(value, str):
                return value
            return "\n".join(value)

        def text_of(genre, cfg):
            if genre == "quiz":
                pairs = [(q["prompt"], q["answer"]) for q in cfg["questions"]]
                return as_text(make_quiz.build_triggers(cfg, pairs, cfg["labels"]))
            if genre == "control":
                return as_text(make_control.build_triggers(cfg, ["경기장"]))
            if genre == "square_defense":
                return as_text(make_square_defense.build_triggers(
                    cfg, "Player 8", "Player 9", ["마당"]))
            if genre == "wave_defense":
                res = scmap.MapResources()
                return as_text(make_wave_defense.build_triggers(
                    cfg, "Player 8", "Player 9", [(1, 1, 4, 4)], ["가", "나"], res))
            if genre == "rpg":
                return as_text(make_rpg.build_triggers(cfg, "Player 8", "Player 9"))
            if genre == "zombie":
                humans = [f"Player {i}" for i in range(1, cfg["rules"]["players"] + 1)]
                return as_text(make_zombie.build(cfg, "Player 8", "Player 9", humans))
            if genre == "hide_seek":
                units = cfg["units"]
                return as_text(make_hide_seek.build_triggers(
                    cfg, cfg["rules"]["hiders"] + 1, cfg["rules"]["prep"],
                    cfg["rules"]["survive"], units["hunter"], units["hider"],
                    units["state_token"]))
            if genre == "room_escape":
                return as_text(make_room_escape.build_triggers(
                    cfg, cfg["rules"]["time_limit"], cfg["units"]["player"],
                    cfg["units"]["state_token"], cfg["labels"]["areas"]))
            if genre == "micro_trial":
                return as_text(make_micro_trial.build_triggers(
                    cfg, cfg["rules"]["stages"], cfg["rules"]["time_limit"],
                    cfg["units"]["player"], cfg["units"]["state_token"]))
            if genre == "loadout_gauntlet":
                units = cfg["units"]
                humans = cfg["rules"]["players"]
                return as_text(make_loadout_gauntlet.build_triggers(
                    cfg, humans, cfg["offers"], cfg["waves"], cfg["labels"],
                    humans + 1, humans + 2, units["choice_state"], units["wave_state"],
                    units["notice_state"], units["presence_counter"],
                    units["recruit"], units["fallback"]))
            raise AssertionError(genre)

        for genre, base in bundled.items():
            left = json.loads(json.dumps(base))
            right = json.loads(json.dumps(base))
            left["starting_resources"]["minerals"] = 11117
            right["starting_resources"]["minerals"] = 22227
            left["text"]["briefing"] = ["왼쪽안내"]
            right["text"]["briefing"] = ["오른쪽안내"]
            if genre == "square_defense":
                left["rules"]["wave_clear_ore"] = 33331
                right["rules"]["wave_clear_ore"] = 44441
                left["text"]["messages"]["leak"] = "왼쪽누수"
                right["text"]["messages"]["leak"] = "오른쪽누수"
                left["text"]["messages"]["respawn"] = "왼쪽부활"
                right["text"]["messages"]["respawn"] = "오른쪽부활"
            if genre == "wave_defense":
                left["text"]["messages"]["respawn"] = "왼쪽재배치"
                right["text"]["messages"]["respawn"] = "오른쪽재배치"
            lt, rt = text_of(genre, left), text_of(genre, right)
            self.assertIn("Set To, 11117", lt, genre)
            self.assertIn("Set To, 22227", rt, genre)
            self.assertNotIn("Set To, 22227", lt, genre)
            self.assertNotIn("왼쪽안내", lt)
            if genre == "square_defense":
                self.assertIn("왼쪽누수", lt)
                self.assertIn("왼쪽부활", lt)
                self.assertIn("Add, 33331", lt)
                self.assertIn("Add, 44441", rt)
                self.assertNotIn("오른쪽누수", lt)
            if genre == "wave_defense":
                self.assertIn("왼쪽재배치", lt)
                self.assertNotIn("오른쪽재배치", lt)
                self.assertNotIn("병력을 다시 받았습니다", lt)
            if genre == "control":
                self.assertIn("At most, 0", lt)
                self.assertNotIn("At most, 2", lt)

        broken = json.loads(json.dumps(bundled["square_defense"]))
        del broken["rules"]["wave_clear_ore"]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "broken.json"
            path.write_text(json.dumps(broken), encoding="utf-8")
            with self.assertRaises(CliError):
                profile.load_profile(str(path), "square_defense")
            for key in ("defenses", "selection"):
                missing = json.loads(json.dumps(bundled["square_defense"]))
                del missing["units"][key]
                path.write_text(json.dumps(missing), encoding="utf-8")
                with self.assertRaises(CliError):
                    profile.load_profile(str(path), "square_defense")
            hid = json.loads(json.dumps(bundled["hide_seek"]))
            del hid["players"]
            path.write_text(json.dumps(hid), encoding="utf-8")
            with self.assertRaises(CliError):
                profile.load_profile(str(path), "hide_seek")

        chase_l = _base_chase("왼쪽추격", "왼쪽추격문", 11117, 4, 40)
        chase_r = _base_chase("오른쪽추격", "오른쪽추격문", 22227, 1, 80)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pl, pr = root / "l.json", root / "r.json"
            pl.write_text(json.dumps(chase_l), encoding="utf-8")
            pr.write_text(json.dumps(chase_r), encoding="utf-8")
            cl = profile.load_profile(str(pl), "chase")
            cr = profile.load_profile(str(pr), "chase")
        tl = make_usemap.chase_triggers(cl, "Player 2")
        tr = make_usemap.chase_triggers(cr, "Player 2")
        self.assertIn("At most, 3", tl)
        self.assertIn("At most, 0", tr)
        self.assertIn(', 1, "', tl)
        self.assertIn("Set To, 11117", tl)
        self.assertIn("Set To, 22227", tr)
        self.assertNotIn("At most, 2", tl)

        respawn = "\n".join(scmap.part_respawn(
            "Player 1", "Terran Marine", "집", "프로필부활문", 1, "Protoss Scarab"))
        other = "\n".join(scmap.part_respawn(
            "Player 1", "Terran Marine", "집", "다른부활문", 6, "Dark Swarm"))
        self.assertIn("프로필부활문", respawn)
        self.assertIn("다른부활문", other)
        self.assertIn(', 1, "', respawn)
        self.assertIn(', 6, "', other)
        self.assertNotIn("병력을 다시 받았습니다", respawn)
        self.assertNotIn("프로필부활문", other)

        beacon_a = json.loads(json.dumps(bundled["loadout_gauntlet"]))
        beacon_b = json.loads(json.dumps(bundled["loadout_gauntlet"]))
        beacon_a["units"]["draft_beacon"] = "Zerg Beacon"
        beacon_b["units"]["draft_beacon"] = "Protoss Beacon"
        self.assertEqual(make_loadout_gauntlet.draft_beacon_unit(beacon_a), "Zerg Beacon")
        self.assertEqual(make_loadout_gauntlet.draft_beacon_unit(beacon_b), "Protoss Beacon")
        self.assertIn("draft_beacon_unit(cfg)", Path(make_loadout_gauntlet.__file__).read_text(encoding="utf-8"))

        wave_a = json.loads(json.dumps(bundled["wave_defense"]))
        wave_b = json.loads(json.dumps(bundled["wave_defense"]))
        wave_a["text"]["messages"]["respawn"] = "웨이브부활A"
        wave_b["text"]["messages"]["respawn"] = "웨이브부활B"
        wave_a["text"]["messages"]["leak"] = "웨이브누수A"
        wave_b["text"]["messages"]["leak"] = "웨이브누수B"
        res = scmap.MapResources()
        wa = "\n".join(make_wave_defense.build_triggers(
            wave_a, "Player 8", "Player 9", [(1, 1, 4, 4)], ["가", "나"], res))
        res = scmap.MapResources()
        wb = "\n".join(make_wave_defense.build_triggers(
            wave_b, "Player 8", "Player 9", [(1, 1, 4, 4)], ["가", "나"], res))
        self.assertIn("웨이브부활A", wa)
        self.assertIn("웨이브누수A", wa)
        self.assertIn("웨이브부활B", wb)
        self.assertNotIn("웨이브부활A", wb)
        self.assertNotIn("병력을 다시 받았습니다", wa)

        race_a = json.loads(json.dumps(bundled["hide_seek"]))
        race_b = json.loads(json.dumps(bundled["micro_trial"]))
        race_a["players"]["race"] = "zerg"
        race_b["players"]["race"] = "protoss"
        self.assertEqual(profile.human_race(race_a), "zerg")
        self.assertEqual(profile.human_race(race_b), "protoss")
        self.assertNotEqual(profile.human_race(race_a), profile.human_race(race_b))

        def room_text(cfg):
            return "\n".join(make_room_escape.build_triggers(
                cfg, cfg["rules"]["time_limit"], cfg["units"]["player"],
                cfg["units"]["state_token"], cfg["labels"]["areas"]))

        short = json.loads(json.dumps(bundled["room_escape"]))
        long = json.loads(json.dumps(bundled["room_escape"]))
        short["units"]["seals"] = ["Terran Beacon", "Zerg Beacon"]
        short["units"]["seal_names"] = ["첫째", "둘째"]
        short["text"]["messages"]["seal_1"] = "첫째 봉인"
        short["text"]["messages"]["seal_2"] = "둘째 봉인"
        long["units"]["seals"] = ["Terran Beacon", "Zerg Beacon", "Protoss Beacon", "Terran Flag"]
        long["units"]["seal_names"] = ["하나", "둘", "셋", "넷"]
        long["labels"]["areas"] = list(long["labels"]["areas"]) + ["끝방"]
        long["text"]["messages"]["seal_4"] = "넷째 봉인"
        short_text = room_text(short)
        long_text = room_text(long)
        self.assertIn("At most, 1", short_text)
        self.assertIn("Exactly, 2", short_text)
        self.assertIn("At most, 3", long_text)
        self.assertIn("Exactly, 4", long_text)
        self.assertNotIn("At most, 2", short_text)

        for mod, needle in (
            (make_hide_seek, "profile.human_race(cfg)"),
            (make_room_escape, "profile.human_race(cfg)"),
            (make_micro_trial, "profile.human_race(cfg)"),
            (make_loadout_gauntlet, 'cfg["units"]["draft_beacon"]'),
        ):
            source = Path(mod.__file__).read_text(encoding="utf-8")
            self.assertIn(needle, source)
            self.assertNotIn('race="terran"', source)
            self.assertNotIn("recipe_profiles", source)
        for name in GENERATORS:
            source = (HERE / name).read_text(encoding="utf-8")
            self.assertNotIn("recipe_profiles", source)


if __name__ == "__main__":
    unittest.main()
