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
import trigger_contracts
import make_bomb_dodge
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
    "make_bomb_dodge.py",
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


def _as_text(value):
    if isinstance(value, str):
        return value
    return "\n".join(value)


def _bomb_dodge_args(cfg):
    """main() 이 만드는 것과 같은 모양의 게이트·폭탄 셀 로케이션 이름.

    실제 좌표는 필요 없다 — 여기서는 트리거 글이 프로필 값을 제대로
    반영하는지만 본다.
    """
    rules, labels = cfg["rules"], cfg["labels"]
    stages, players = rules["stages"], rules["players"]
    operator = f"Player {players + 1}"
    gates = ([labels["start"]]
             + [f"{labels['checkpoint_prefix']}{i}" for i in range(1, stages)]
             + [labels["goal"]])
    stage_cell_locs = [
        [f"{labels['bomb_prefix']}{s + 1}-{i + 1}"
         for i in range(cfg["stages"][s]["grid"][0] * cfg["stages"][s]["grid"][1])]
        for s in range(stages)]
    return operator, gates, stage_cell_locs


def _trigger_text(genre, cfg):
    """배송된 build / build_triggers / chase_triggers 가 만든 트리거 글."""
    if genre == "bomb_dodge":
        operator, gates, stage_cell_locs = _bomb_dodge_args(cfg)
        return _as_text(make_bomb_dodge.build_triggers(cfg, operator, gates, stage_cell_locs))
    if genre == "quiz":
        pairs = [(q["prompt"], q["answer"]) for q in cfg["questions"]]
        return _as_text(make_quiz.build_triggers(cfg, pairs, cfg["labels"]))
    if genre == "control":
        return _as_text(make_control.build_triggers(cfg, ["경기장"]))
    if genre == "square_defense":
        return _as_text(make_square_defense.build_triggers(
            cfg, "Player 8", "Player 9", ["마당"]))
    if genre == "wave_defense":
        return _as_text(make_wave_defense.build_triggers(
            cfg, "Player 8", "Player 9", [(1, 1, 4, 4)], ["가", "나"],
            scmap.MapResources()))
    if genre == "rpg":
        return _as_text(make_rpg.build_triggers(cfg, "Player 8", "Player 9"))
    if genre == "zombie":
        humans = [f"Player {i}" for i in range(1, cfg["rules"]["players"] + 1)]
        return _as_text(make_zombie.build(cfg, "Player 8", "Player 9", humans))
    if genre == "hide_seek":
        units = cfg["units"]
        return _as_text(make_hide_seek.build_triggers(
            cfg, cfg["rules"]["hiders"] + 1, cfg["rules"]["prep"],
            cfg["rules"]["survive"], units["hunter"], units["hider"],
            units["state_token"]))
    if genre == "room_escape":
        return _as_text(make_room_escape.build_triggers(
            cfg, cfg["rules"]["time_limit"], cfg["units"]["player"],
            cfg["units"]["state_token"], cfg["labels"]["areas"]))
    if genre == "micro_trial":
        return _as_text(make_micro_trial.build_triggers(
            cfg, cfg["rules"]["stages"], cfg["rules"]["time_limit"],
            cfg["units"]["player"], cfg["units"]["state_token"]))
    if genre == "loadout_gauntlet":
        units = cfg["units"]
        humans = cfg["rules"]["players"]
        return _as_text(make_loadout_gauntlet.build_triggers(
            cfg, humans, cfg["offers"], cfg["waves"], cfg["labels"],
            humans + 1, humans + 2, units["choice_state"], units["wave_state"],
            units["notice_state"], units["presence_counter"],
            units["recruit"], units["fallback"]))
    if genre == "chase":
        return _as_text(make_usemap.chase_triggers(cfg, "Player 2"))
    raise AssertionError(genre)


def _load_bundled():
    bundled = {}
    for path in (HERE / "recipe_profiles").glob("*.json"):
        raw = json.loads(path.read_text(encoding="utf-8"))
        bundled[raw["genre"]] = profile.load_profile(str(path), raw["genre"])
    return bundled


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
            if genre == "bomb_dodge":
                operator, gates, stage_cell_locs = _bomb_dodge_args(cfg)
                return as_text(make_bomb_dodge.build_triggers(cfg, operator, gates, stage_cell_locs))
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

    def test_flagged_fields_differ_in_trigger_text(self):
        """부활·누수·추격 상한·컨트롤 재소환·종족·비콘이 프로필 두 벌에서 갈린다."""
        bundled = {}
        for path in (HERE / "recipe_profiles").glob("*.json"):
            raw = json.loads(path.read_text(encoding="utf-8"))
            bundled[raw["genre"]] = profile.load_profile(str(path), raw["genre"])

        square_a = json.loads(json.dumps(bundled["square_defense"]))
        square_b = json.loads(json.dumps(bundled["square_defense"]))
        square_a["text"]["messages"]["leak"] = "누수A"
        square_b["text"]["messages"]["leak"] = "누수B"
        square_a["text"]["messages"]["respawn"] = "부활A"
        square_b["text"]["messages"]["respawn"] = "부활B"
        square_a["rules"]["starting_unit_count"] = 1
        square_b["rules"]["starting_unit_count"] = 8
        sa = make_square_defense.build_triggers(square_a, "Player 8", "Player 9", ["마당"])
        sb = make_square_defense.build_triggers(square_b, "Player 8", "Player 9", ["마당"])
        self.assertIn("누수A", sa)
        self.assertIn("부활A", sa)
        self.assertIn(', 1, "', sa)
        self.assertIn("누수B", sb)
        self.assertIn("부활B", sb)
        self.assertIn(', 8, "', sb)
        self.assertNotIn("누수B", sa)
        self.assertNotIn("병력을 다시 받았습니다", sa)

        wave_a = json.loads(json.dumps(bundled["wave_defense"]))
        wave_b = json.loads(json.dumps(bundled["wave_defense"]))
        wave_a["text"]["messages"]["respawn"] = "재배치A"
        wave_b["text"]["messages"]["respawn"] = "재배치B"
        wa = "\n".join(make_wave_defense.build_triggers(
            wave_a, "Player 8", "Player 9", [(1, 1, 4, 4)], ["가", "나"], scmap.MapResources()))
        wb = "\n".join(make_wave_defense.build_triggers(
            wave_b, "Player 8", "Player 9", [(1, 1, 4, 4)], ["가", "나"], scmap.MapResources()))
        self.assertIn("재배치A", wa)
        self.assertIn("재배치B", wb)
        self.assertNotIn("재배치A", wb)

        chase_a = _base_chase("추격A", "문장A", 11, 1, 40)
        chase_b = _base_chase("추격B", "문장B", 22, 6, 80)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pa, pb = root / "a.json", root / "b.json"
            pa.write_text(json.dumps(chase_a), encoding="utf-8")
            pb.write_text(json.dumps(chase_b), encoding="utf-8")
            ta = make_usemap.chase_triggers(profile.load_profile(str(pa), "chase"), "Player 2")
            tb = make_usemap.chase_triggers(profile.load_profile(str(pb), "chase"), "Player 2")
        self.assertIn("At most, 0", ta)
        self.assertIn("At most, 5", tb)
        self.assertIn(', 1, "', ta)
        self.assertNotIn("At most, 2", ta)

        control_a = json.loads(json.dumps(bundled["control"]))
        control_b = json.loads(json.dumps(bundled["control"]))
        control_a["text"]["messages"]["respawn"] = "증원A"
        control_b["text"]["messages"]["respawn"] = "증원B"
        control_a["squads"][control_a["starting_squad"]]["count"] = 1
        control_b["squads"][control_b["starting_squad"]]["count"] = 8
        ca = make_control.build_triggers(control_a, ["경기장"])
        cb = make_control.build_triggers(control_b, ["경기장"])
        self.assertIn("증원A", ca)
        self.assertIn("증원B", cb)
        self.assertIn("At most, 0", ca)
        self.assertIn("At most, 0", cb)
        self.assertNotIn("At most, 2", ca)
        self.assertIn(', 1, "', ca)
        self.assertIn(', 8, "', cb)
        self.assertNotIn("증원A", cb)

        for genre, race in (("hide_seek", "zerg"), ("room_escape", "protoss"), ("micro_trial", "terran")):
            cfg = json.loads(json.dumps(bundled[genre]))
            cfg["players"]["race"] = race
            self.assertEqual(profile.human_race(cfg), race)
        self.assertNotEqual(
            profile.human_race({**bundled["hide_seek"], "players": {**bundled["hide_seek"]["players"], "race": "zerg"}}),
            profile.human_race({**bundled["room_escape"], "players": {**bundled["room_escape"]["players"], "race": "protoss"}}),
        )

        beacon_a = json.loads(json.dumps(bundled["loadout_gauntlet"]))
        beacon_b = json.loads(json.dumps(bundled["loadout_gauntlet"]))
        beacon_a["units"]["draft_beacon"] = "Terran Flag"
        beacon_b["units"]["draft_beacon"] = "Zerg Flag"
        self.assertEqual(make_loadout_gauntlet.draft_beacon_unit(beacon_a), "Terran Flag")
        self.assertEqual(make_loadout_gauntlet.draft_beacon_unit(beacon_b), "Zerg Flag")
        self.assertIn("place_beacon_shop(\n            cli, draft_beacon_unit(cfg)", Path(make_loadout_gauntlet.__file__).read_text(encoding="utf-8"))

        def zombie_text(population):
            cfg = json.loads(json.dumps(bundled["zombie"]))
            cfg["rules"]["zombie_population"] = population
            humans = [f"Player {i}" for i in range(1, cfg["rules"]["players"] + 1)]
            return make_zombie.build(cfg, "Player 8", "Player 9", humans)

        za = zombie_text(4)
        zb = zombie_text(10)
        grave = bundled["zombie"]["labels"]["graveyard"]
        zunit = bundled["zombie"]["units"]["zombie"]
        lock = bundled["zombie"]["units"]["spawn_lock"]
        self.assertIn(f'Bring("Player 8", "{zunit}", "{grave}", At most, 3)', za)
        self.assertIn(f'Bring("Player 8", "{zunit}", "{grave}", At most, 9)', zb)
        self.assertIn(f'Set Deaths("Player 8", "{lock}", Set To, 2)', za)
        self.assertIn(f'Set Deaths("Player 8", "{lock}", Set To, 5)', zb)
        self.assertEqual(make_zombie.placed_zombies({"rules": {"zombie_population": 4}}), 2)
        self.assertEqual(make_zombie.placed_zombies({"rules": {"zombie_population": 10}}), 5)
        self.assertNotIn(
            f'Bring("Player 8", "{zunit}", "{bundled["zombie"]["labels"]["field"]}", At most,',
            za)
        self.assertIn(f'Deaths("Player 9", "{lock}", Exactly, 0)', za)
        self.assertNotIn(f'Any unit", "{bundled["zombie"]["labels"]["field"]}", At most, 0', za)
        self.assertIn(f'Set Deaths("Current Player", "{lock}", Set To, 1)', za)
        self.assertIn('Command("Current Player", "Men", At least, 1)', za)
        self.assertIn(f'Set Deaths("Current Player", "{lock}", Set To, 0)', za)

        buy = make_control.build_triggers(json.loads(json.dumps(bundled["control"])), ["경기장"])
        self.assertIn('Bring("Player 1", "Men",', buy)
        self.assertNotIn('Bring("Player 1", "Any unit",', buy)

    def test_trigger_contract_shapes(self):
        """검사기가 빈 함수가 아닌지, 걸쇠의 정상 순서는 통과하는지."""
        jumped = '''
Trigger("Player 1"){
Conditions:
	Elapsed Time(At least, 180);
	Deaths("Current Player", "Protoss Scarab", At most, 6);

Actions:
	Set Deaths("Current Player", "Protoss Scarab", Set To, 7);
	Preserve Trigger();
}
Trigger("Player 1"){
Conditions:
	Elapsed Time(At least, 240);
	Deaths("Current Player", "Protoss Scarab", At most, 3);

Actions:
	Set Deaths("Current Player", "Protoss Scarab", Set To, 4);
	Preserve Trigger();
}
'''
        self.assertTrue(trigger_contracts.check(jumped))
        ordered = '''
Trigger("Player 1"){
Conditions:
	Elapsed Time(At least, 60);
	Deaths("Current Player", "Protoss Scarab", At most, 0);

Actions:
	Set Deaths("Current Player", "Protoss Scarab", Set To, 1);
	Preserve Trigger();
}
Trigger("Player 1"){
Conditions:
	Elapsed Time(At least, 120);
	Deaths("Current Player", "Protoss Scarab", At most, 1);

Actions:
	Set Deaths("Current Player", "Protoss Scarab", Set To, 2);
	Preserve Trigger();
}
'''
        self.assertEqual(trigger_contracts.check(ordered), [])
        self.assertTrue(trigger_contracts.check(
            'Play WAV("sound\\Misc\\Button.wav", 0);'))
        self.assertEqual(trigger_contracts.check(
            'Play WAV("sound\\\\Misc\\\\Button.wav", 0);'), [])
        sticky = '''
Trigger("Player 1"){
Conditions:
	Bring("Player 1", "Men", "pad", At least, 1);
	Accumulate("Player 1", At least, 50, ore);

Actions:
	Set Resources("Player 1", Subtract, 50, ore);
	Preserve Trigger();
}
'''
        self.assertTrue(trigger_contracts.check(sticky))
        pushed = sticky.replace(
            "Set Resources(\"Player 1\", Subtract, 50, ore);",
            "Set Resources(\"Player 1\", Subtract, 50, ore);\n"
            "\tMove Unit(\"Player 1\", \"Men\", All, \"pad\", \"home\");")
        self.assertEqual(trigger_contracts.check(pushed), [])
        self.assertTrue(trigger_contracts.check('''
Trigger("Player 8"){
Conditions:
	Always();

Actions:
	Create Unit("Player 8", "Zerg Zergling", 1, "grave");
	Preserve Trigger();
}
'''))

    def test_awkward_profiles_satisfy_trigger_contracts(self):
        """보스가 마지막 웨이브보다 이르고, 치료비가 1 이상인 프로필도 계약을 지킨다."""
        bundled = _load_bundled()
        awkward = {}
        for genre, cfg in bundled.items():
            cfg = json.loads(json.dumps(cfg))
            if genre == "zombie":
                cfg["rules"]["boss_seconds"] = 1
                cfg["text"]["messages"]["boss_wav"] = "sound\\Custom\\Horn.wav"
            elif genre == "control":
                cfg["rules"]["heal_cost"] = 1
            elif genre == "rpg":
                cfg["rules"]["heal_cost"] = 1
            elif genre == "wave_defense":
                cfg["text"]["wave_wav"] = "sound\\Custom\\Wave.wav"
                cfg["text"]["boss_wav"] = "sound\\Custom\\Boss.wav"
            awkward[genre] = cfg
        chase = _base_chase("추격계약", "추격계약문", 15, 3, 40)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "chase.json"
            path.write_text(json.dumps(chase), encoding="utf-8")
            awkward["chase"] = profile.load_profile(str(path), "chase")

        for genre, cfg in awkward.items():
            findings = trigger_contracts.check(_trigger_text(genre, cfg))
            self.assertEqual(findings, [], genre + "\n" + "\n".join(findings))

        zombie = awkward["zombie"]
        text = _trigger_text("zombie", zombie)
        rules = zombie["rules"]
        units = zombie["units"]
        last = rules["wave_count"]
        seconds = last * rules["survive_seconds"] // last
        self.assertIn(
            zombie["text"]["messages"]["wave"].format(
                number=last, total=last, seconds=seconds),
            text)
        self.assertIn(
            zombie["text"]["messages"]["boss"].format(minutes=rules["boss_seconds"] // 60),
            text)
        self.assertIn(
            f'Deaths("Current Player", "{units["wave_counter"]}", At most, {last - 1})',
            text)
        self.assertNotIn(
            f'Set Deaths("Current Player", "{units["wave_counter"]}", Set To, {last + 1})',
            text)
        self.assertIn(
            f'Set Deaths("Current Player", "{units["boss_seen"]}", Set To, 1)',
            text)
        self.assertIn('Play WAV("sound\\\\Custom\\\\Horn.wav"', text)
        self.assertIn('Play WAV("sound\\\\Custom\\\\Wave.wav"',
                      _trigger_text("wave_defense", awkward["wave_defense"]))
        self.assertIn('Play WAV("sound\\\\Custom\\\\Boss.wav"',
                      _trigger_text("wave_defense", awkward["wave_defense"]))
        self.assertIn('Play WAV("sound\\\\Misc\\\\PowerDown.wav"',
                      _trigger_text("wave_defense", awkward["wave_defense"]))

        shared = json.loads(json.dumps(zombie))
        shared["units"]["boss_seen"] = shared["units"]["wave_counter"]
        self.assertTrue(trigger_contracts.check(_trigger_text("zombie", shared)))

        control = awkward["control"]
        control_text = _trigger_text("control", control)
        spawn = f'경기장 {control["labels"]["spawn_suffix"]}'
        gate = f'경기장 {control["labels"]["gate_suffix"]}'
        self.assertIn(
            f'Move Unit("Player 1", "Men", All, "{spawn}", "{gate}")',
            control_text)
        self.assertIn('Subtract, 1, ore', control_text)
        free = json.loads(json.dumps(bundled["control"]))
        free["rules"]["heal_cost"] = 0
        free_text = _trigger_text("control", free)
        self.assertNotIn(
            f'Move Unit("Player 1", "Men", All, "{spawn}", "{gate}")',
            free_text)
        self.assertEqual(trigger_contracts.check(free_text), [])

        zombie_paid = awkward["zombie"]
        pad = zombie_paid["labels"]["heal_pad"]
        shelter = zombie_paid["labels"]["shelter"]
        home = (f'{zombie_paid["labels"]["home_prefix"]}1 '
                f'{zombie_paid["labels"]["home_suffix"]}')
        self.assertGreater(zombie_paid["rules"]["heal_cost"], 0)
        self.assertIn(f'Bring("Player 1", "Men", "{pad}", At least, 1)', text)
        self.assertIn(
            f'Move Unit("Player 1", "Men", All, "{pad}", "{home}")', text)
        self.assertNotIn(
            f'Bring("Player 1", "Men", "{shelter}", At least, 1)', text)
        zombie_free = json.loads(json.dumps(zombie_paid))
        zombie_free["rules"]["heal_cost"] = 0
        free_zombie = _trigger_text("zombie", zombie_free)
        self.assertIn(f'Bring("Player 1", "Men", "{shelter}", At least, 1)', free_zombie)
        self.assertNotIn(f'Move Unit("Player 1", "Men", All, "{pad}"', free_zombie)
        self.assertEqual(trigger_contracts.check(free_zombie), [])
        # 거점이 피난처 밖으로 가장 멀리 나가는 합법 범위에서도 발판과 안 겹친다.
        shelter_box = (4, 4, 20, 40)
        field_box = (27, 4, 12, 40)
        pad_box = make_zombie.heal_pad_box(field_box)
        for index in range(6):
            self.assertFalse(make_zombie._rects_overlap(
                make_zombie.home_box(shelter_box, index, 18), pad_box))
        with self.assertRaises(CliError):
            scmap.part_heal_zone("Player 1", "같은칸", cost=1, push_to="같은칸")

        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "zombie.json"
            missing = json.loads(json.dumps(bundled["zombie"]))
            del missing["units"]["boss_seen"]
            path.write_text(json.dumps(missing), encoding="utf-8")
            with self.assertRaises(CliError):
                profile.load_profile(str(path), "zombie")
            collided = json.loads(json.dumps(bundled["zombie"]))
            collided["units"]["boss_seen"] = collided["units"]["wave_counter"]
            path.write_text(json.dumps(collided), encoding="utf-8")
            with self.assertRaises(CliError):
                profile.load_profile(str(path), "zombie")
            presence = json.loads(json.dumps(bundled["zombie"]))
            presence["units"]["boss_seen"] = scmap.PRESENCE_UNIT
            path.write_text(json.dumps(presence), encoding="utf-8")
            with self.assertRaises(CliError):
                profile.load_profile(str(path), "zombie")
            same_pad = json.loads(json.dumps(bundled["zombie"]))
            same_pad["labels"]["heal_pad"] = same_pad["labels"]["shelter"]
            path.write_text(json.dumps(same_pad), encoding="utf-8")
            with self.assertRaises(CliError):
                profile.load_profile(str(path), "zombie")
            same_gate = json.loads(json.dumps(bundled["control"]))
            same_gate["rules"]["heal_cost"] = 1
            same_gate["labels"]["gate_suffix"] = same_gate["labels"]["spawn_suffix"]
            path.write_text(json.dumps(same_gate), encoding="utf-8")
            with self.assertRaises(CliError):
                profile.load_profile(str(path), "control")

    def test_chase_lane_keeps_corners_out(self):
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


if __name__ == "__main__":
    unittest.main()
