"""Generate a two-page printable certificate from a committed UE training snapshot."""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import tempfile
import uuid
from datetime import datetime, timezone
from html import escape

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, PageBreak, Table, TableStyle, KeepTogether

STEPS = {
    "WeaponCheck": "총기 확인", "AimHold": "조준 유지", "Reload": "재장전",
    "Calibration": "센서 보정", "Accuracy": "영점 사격", "Cover": "엄폐",
    "Countdown": "사격 준비", "TimedTargets": "기록사격", "Breach": "브리칭",
}


def number(data, key, *, integer=False, minimum=0):
    value = data[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"Invalid number: {key}")
    if value < minimum or (integer and int(value) != value):
        raise ValueError(f"Invalid range: {key}")
    return value


def validate(data):
    if data.get("schema_version") != 1 or data.get("passed") is not True or data.get("finished") is not True:
        raise ValueError("Only completed, qualified sessions can issue a certificate")
    if data.get("aborted") is not False or data.get("test_inputs") is not False:
        raise ValueError("Aborted or development-test sessions cannot issue a certificate")
    uuid.UUID(data["session_id"])
    datetime.fromisoformat(data["completed_at_utc"].replace("Z", "+00:00"))
    for key in ("learner_name", "learner_id"):
        if not isinstance(data[key], str) or len(data[key]) > 120:
            raise ValueError(f"{key} must be a string of at most 120 characters")
        if any(ord(c) < 32 and c not in "\n\t" for c in data[key]):
            raise ValueError(f"Invalid control character: {key}")
    for key in ("total_seconds", "recorded_fire_seconds", "recorded_fire_limit_seconds"):
        number(data, key)
    for key in ("shots_fired", "valid_hits", "unique_targets_hit", "total_targets", "passing_target_count"):
        number(data, key, integer=True)
    if not 0 < data["passing_target_count"] <= data["unique_targets_hit"] <= data["total_targets"]:
        raise ValueError("Distinct target pass threshold not satisfied")
    if not data["unique_targets_hit"] <= data["valid_hits"] <= data["shots_fired"]:
        raise ValueError("Inconsistent shot/hit counts")
    if data["recorded_fire_limit_seconds"] <= 0 or data["recorded_fire_seconds"] < data["recorded_fire_limit_seconds"] - 0.05:
        raise ValueError("Recorded fire must run for its full duration")
    steps = data["steps"]
    if not isinstance(steps, list) or len(steps) != len(STEPS) or {s["code"] for s in steps} != set(STEPS):
        raise ValueError("The nine required exercises must occur exactly once")
    for step in steps:
        if step["outcome"] != "합격":
            raise ValueError("Every required exercise must pass")
        number(step, "attempts", integer=True, minimum=1)
        number(step, "elapsed_seconds")
    return data


def find_font(explicit=None):
    candidates = [Path(explicit)] if explicit else [
        Path(__file__).parent / "fonts" / "TrainingKorean.ttf",
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts" / "malgun.ttf",
    ]
    for path in candidates:
        if path.is_file():
            return path
    raise FileNotFoundError("Korean TrueType font unavailable; use --font or install Malgun Gothic")


def generate(data, output: Path, font=None):
    validate(data)
    pdfmetrics.registerFont(TTFont("TrainingKorean", str(find_font(font))))
    ink = colors.HexColor("#172D43")
    styles = {
        "title": ParagraphStyle("title", fontName="TrainingKorean", fontSize=30, leading=42, textColor=ink, alignment=TA_CENTER),
        "subtitle": ParagraphStyle("subtitle", fontName="TrainingKorean", fontSize=12, leading=18, textColor=ink, alignment=TA_CENTER),
        "body": ParagraphStyle("body", fontName="TrainingKorean", fontSize=12, leading=20, textColor=ink, wordWrap="CJK"),
        "small": ParagraphStyle("small", fontName="TrainingKorean", fontSize=9, leading=14, textColor=ink, wordWrap="CJK"),
        "heading": ParagraphStyle("heading", fontName="TrainingKorean", fontSize=20, leading=30, textColor=ink),
    }
    def p(text, kind="body"):
        return Paragraph(escape(str(text)).replace("\n", "<br/>"), styles[kind])
    session = str(uuid.UUID(data["session_id"]))
    completed = datetime.fromisoformat(data["completed_at_utc"].replace("Z", "+00:00")).astimezone(timezone.utc)
    date = completed.strftime("%Y년 %m월 %d일 %H:%M UTC")
    name = data["learner_name"].strip() or "미입력"
    identity = data["learner_id"].strip() or "미입력"
    rows = [[p("훈련자"), p(name)], [p("식별번호"), p(identity)], [p("이수 일시"), p(date)]]
    identity_table = Table(rows, colWidths=[32*mm, 126*mm])
    identity_table.setStyle(TableStyle([
        ("VALIGN", (0,0), (-1,-1), "TOP"), ("BOTTOMPADDING", (0,0), (-1,-1), 13),
        ("TOPPADDING", (0,0), (-1,-1), 13), ("LINEBELOW", (0,0), (-1,-1), 0.4, colors.HexColor("#DCE4EB")),
    ]))
    story = [Spacer(1,20*mm), p("OPERATION BREAKPOINT", "subtitle"), Spacer(1,8*mm),
             p("훈련 이수증", "title"), Spacer(1,18*mm), identity_table, Spacer(1,15*mm),
             p("위 훈련자는 총기 확인, 조준 유지, 재장전, 센서 보정 및 영점 사격을 완료하고, 엄폐·기록사격·브리칭 평가 기준을 충족하여 훈련을 이수하였습니다."),
             Spacer(1,12*mm), p(f"기록사격 {data['unique_targets_hit']}/{data['total_targets']}개 표적 명중 · 브리칭 합격", "subtitle"),
             Spacer(1,14*mm), p("OPERATION BREAKPOINT 훈련 시스템", "subtitle"),
             Spacer(1,7*mm), p(f"이수 기록 번호\n{session}", "small"), PageBreak(),
             p("훈련 평가표", "heading"), p(f"훈련자: {name}\n식별번호: {identity}", "small"), Spacer(1,8*mm)]
    rate = 100*data["valid_hits"]/data["shots_fired"] if data["shots_fired"] else 0
    metrics = [["평가 항목", "기록"], ["서로 다른 표적 명중", f"{data['unique_targets_hit']} / {data['total_targets']}개"],
               ["발사 / 유효 명중", f"{data['shots_fired']} / {data['valid_hits']}발"], ["탄환 명중률", f"{rate:.1f}%"],
               ["기록사격 시간", f"{data['recorded_fire_seconds']:.1f}초"], ["전체 훈련 시간", f"{data['total_seconds']:.1f}초"]]
    detail = [["훈련 행동", "결과", "시도", "소요 시간"]]
    detail += [[STEPS[s["code"]], s["outcome"], str(s["attempts"]), f"{s['elapsed_seconds']:.1f}초"] for s in data["steps"]]
    def table(rows, widths):
        result = Table([[p(cell,"small") for cell in row] for row in rows], colWidths=widths, repeatRows=1)
        result.setStyle(TableStyle([
            ("BACKGROUND", (0,0), (-1,0), colors.HexColor("#E6EEF5")),
            ("VALIGN", (0,0), (-1,-1), "TOP"), ("TOPPADDING", (0,0), (-1,-1), 7),
            ("BOTTOMPADDING", (0,0), (-1,-1), 7), ("LINEBELOW", (0,0), (-1,-1), 0.3, colors.HexColor("#DCE4EB")),
        ]))
        return result
    story += [table(metrics,[80*mm,78*mm]), Spacer(1,8*mm), table(detail,[70*mm,28*mm,20*mm,40*mm]),
              Spacer(1,6*mm), p("탄환 명중률은 유효 명중 ÷ 발사 수입니다. 같은 표적의 반복 명중은 탄환 명중률에 포함되며, 서로 다른 표적 수에는 한 번만 포함됩니다.","small")]
    def decorate(canvas, doc):
        canvas.saveState(); canvas.setStrokeColor(ink); canvas.setLineWidth(0.7)
        canvas.rect(15*mm,15*mm,A4[0]-30*mm,A4[1]-30*mm)
        canvas.setFont("TrainingKorean",8); canvas.setFillColor(ink)
        canvas.drawString(24*mm,20*mm,"OPERATION BREAKPOINT | "+session)
        canvas.drawRightString(A4[0]-24*mm,20*mm,f"{doc.page} / 2")
        if data.get("sample") is True:
            canvas.drawCentredString(A4[0]/2,A4[1]-21*mm,"테스트 출력 · 실제 이수 기록 아님")
        canvas.restoreState()
    output = Path(output); output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=output.stem+"-", suffix=".pdf", dir=output.parent)
    os.close(fd)
    try:
        doc = SimpleDocTemplate(temporary, pagesize=A4, leftMargin=26*mm, rightMargin=26*mm,
                                topMargin=24*mm, bottomMargin=28*mm, title="훈련 이수증 및 평가표", author="OPERATION BREAKPOINT")
        doc.build(story, onFirstPage=decorate, onLaterPages=decorate)
        if doc.page != 2:
            raise ValueError("Report exceeded its two-page layout; shorten learner identity fields")
        os.replace(temporary, output)
    finally:
        Path(temporary).unlink(missing_ok=True)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--input",required=True,type=Path); parser.add_argument("--output",required=True,type=Path)
    parser.add_argument("--font",type=Path)
    args=parser.parse_args()
    if args.input.stat().st_size > 1024*1024:
        raise ValueError("Input larger than 1 MiB")
    with args.input.open(encoding="utf-8-sig") as stream:
        data=json.load(stream)
    generate(data,args.output,args.font)

if __name__ == "__main__":
    main()
