import copy
import json
from pathlib import Path
import tempfile
import unittest
import uuid
from training_report import generate, validate, STEPS


def fixture():
    return dict(schema_version=1, sample=True, session_id=str(uuid.UUID("11111111-2222-4333-8444-555555555555")),
                learner_name="테스트 훈련자", learner_id="TEST-001", completed_at_utc="2026-09-30T10:00:00Z",
                passed=True, finished=True, aborted=False, test_inputs=False, total_seconds=100,
                shots_fired=20, valid_hits=12, unique_targets_hit=8, total_targets=10,
                passing_target_count=6, recorded_fire_seconds=30, recorded_fire_limit_seconds=30,
                steps=[dict(code=code, name=name, outcome="합격", attempts=1,
                            elapsed_seconds={"AimHold": 8, "Countdown": 10, "TimedTargets": 30}.get(code, 3))
                       for code,name in STEPS.items()])


class ReportTests(unittest.TestCase):
    def test_rejects_failed_aborted_debug_and_short_fire(self):
        for field,value in [("passed",False),("finished",False),("aborted",True),("test_inputs",True),
                            ("recorded_fire_seconds",29),("valid_hits",21),("unique_targets_hit",5),
                            ("shots_fired",float("nan")),("total_seconds",-1)]:
            with self.subTest(field=field):
                data=fixture(); data[field]=value
                with self.assertRaises(ValueError): validate(data)

    def test_required_exercises_cannot_be_omitted_duplicated_or_failed(self):
        for edit in (lambda s:s.pop(), lambda s:s.__setitem__(1,copy.deepcopy(s[0])),
                     lambda s:s[0].__setitem__("outcome","불합격")):
            data=fixture(); edit(data["steps"])
            with self.assertRaises(ValueError): validate(data)

    def test_generates_unicode_and_preserves_existing_pdf_on_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"result.pdf"; data=fixture()
            data["learner_name"]="훈련자 <테스트 & 확인>"
            generate(data,path)
            original=path.read_bytes(); self.assertTrue(original.startswith(b"%PDF"))
            data["passed"]=False
            with self.assertRaises(ValueError): generate(data,path)
            self.assertEqual(original,path.read_bytes())
            self.assertEqual([path],list(Path(directory).glob("*.pdf")))

    def test_long_identity_keeps_two_page_layout(self):
        data=fixture(); data["learner_name"]="긴이름"*25; data["learner_id"]="A"*100
        with tempfile.TemporaryDirectory() as directory:
            generate(data,Path(directory)/"long.pdf")


if __name__ == "__main__":
    unittest.main()
