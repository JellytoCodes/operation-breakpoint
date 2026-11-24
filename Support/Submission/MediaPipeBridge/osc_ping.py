from __future__ import annotations

from pythonosc.udp_client import SimpleUDPClient

from common import load_config, resolve_osc_ip


def main() -> None:
    config = load_config()
    osc_ip = resolve_osc_ip(config)
    osc_port = int(config.get("osc_port", 8000))
    client = SimpleUDPClient(osc_ip, osc_port)

    print(f"OSC Ping 대상: {osc_ip}:{osc_port}")
    print("UE를 Play 상태로 둔 뒤 Enter를 누르십시오.")
    print("종료: Ctrl+C")

    sequence = 0

    while True:
        input()
        sequence += 1
        client.send_message(
            "/test",
            [float(sequence), 1.0, 2.0, 3.0],
        )
        print(f"/test 전송 완료: sequence={sequence}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nOSC Ping 종료")
