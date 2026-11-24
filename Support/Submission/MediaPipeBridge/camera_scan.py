from __future__ import annotations

import argparse
import time

import cv2

from common import load_config, save_config


BACKENDS = {
    "AUTO": cv2.CAP_ANY,
    "DSHOW": cv2.CAP_DSHOW,
    "MSMF": cv2.CAP_MSMF,
}


def open_capture(index: int, backend_name: str) -> cv2.VideoCapture:
    backend = BACKENDS.get(backend_name.upper(), cv2.CAP_DSHOW)
    return cv2.VideoCapture(index, backend)


def scan(max_index: int, backend_name: str) -> list[int]:
    available: list[int] = []

    print(f"\n=== 카메라 검색: backend={backend_name} ===")

    for index in range(max_index + 1):
        capture = open_capture(index, backend_name)

        try:
            if not capture.isOpened():
                print(f"[{index}] 열기 실패")
                continue

            success, frame = capture.read()

            if not success or frame is None:
                print(f"[{index}] 열렸지만 프레임 없음")
                continue

            height, width = frame.shape[:2]
            actual_backend = capture.getBackendName()
            print(
                f"[{index}] 사용 가능: {width}x{height}, "
                f"backend={actual_backend}"
            )
            available.append(index)
        finally:
            capture.release()
            time.sleep(0.05)

    return available


def preview(index: int, backend_name: str) -> None:
    capture = open_capture(index, backend_name)

    if not capture.isOpened():
        raise RuntimeError(
            f"카메라를 열 수 없습니다: index={index}, backend={backend_name}"
        )

    print("미리보기 종료: Q 또는 ESC")

    try:
        while True:
            success, frame = capture.read()

            if not success or frame is None:
                raise RuntimeError("카메라 프레임을 읽지 못했습니다.")

            cv2.putText(
                frame,
                f"Camera Index {index} / {backend_name}",
                (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.8,
                (0, 255, 0),
                2,
            )
            cv2.imshow("Camera Preview", frame)

            key = cv2.waitKey(1) & 0xFF
            if key == ord("q") or key == 27:
                break
    finally:
        capture.release()
        cv2.destroyAllWindows()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="DroidCam/OpenCV 카메라 인덱스 검색 및 설정"
    )
    parser.add_argument("--max-index", type=int, default=5)
    parser.add_argument("--backend", choices=BACKENDS.keys(), default=None)
    parser.add_argument("--preview", type=int, default=None)
    parser.add_argument("--select", type=int, default=None)
    args = parser.parse_args()

    config = load_config()
    backend_name = args.backend or str(config.get("camera_backend", "DSHOW"))

    if args.preview is not None:
        preview(args.preview, backend_name)
        return

    if args.select is not None:
        config["camera_index"] = args.select
        config["camera_backend"] = backend_name
        save_config(config)
        print(
            f"config.json 저장 완료: "
            f"camera_index={args.select}, backend={backend_name}"
        )
        return

    available = scan(args.max_index, backend_name)

    if not available and backend_name != "AUTO":
        print("\n지정 백엔드에서 찾지 못해 AUTO로 다시 검색합니다.")
        available = scan(args.max_index, "AUTO")

    if available:
        print("\n후보 확인 예시:")
        print(f"  python camera_scan.py --preview {available[0]}")
        print("DroidCam 번호를 확인한 뒤 저장:")
        print("  python camera_scan.py --select <번호>")
    else:
        print("\n사용 가능한 카메라를 찾지 못했습니다.")
        print("Windows 카메라 권한과 DroidCam 연결 상태를 확인하십시오.")


if __name__ == "__main__":
    main()
