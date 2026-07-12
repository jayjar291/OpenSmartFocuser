#!/usr/bin/env python3

"""Interactive serial exerciser for OpenSmartFocuser.

Usage:
  python scripts/serial_target_slew_test.py --port COM5

The script performs three interactive tests:
  1. Cycle through five target states, mixing named targets and cleared targets.
    2. Simulate RA/Dec slew updates by repeatedly setting target coordinates at
         50/100/150/200/250 ms cadence for about five seconds each.
    3. Run a five-second demo slew at the recommended driver refresh cadence.

You can also run only a single direct slew test:
    python scripts/serial_target_slew_test.py --port COM5 --hz 10 --time 5

It speaks the framed serial protocol used by the firmware command index.
"""

from __future__ import annotations

import argparse
import statistics
import sys
import time
from dataclasses import dataclass
from typing import Iterable, Optional

try:
    import serial
    import serial.tools.list_ports
except ImportError as exc:  # pragma: no cover - runtime dependency only.
    raise SystemExit(
        "pyserial is required. Install it with: pip install pyserial"
    ) from exc


BAUD_RATE = 115200
CONNECT_RETRIES = 6
CONNECT_RETRY_DELAY_S = 0.5
RECOMMENDED_REFRESH_HZ = 10.0
RECOMMENDED_CADENCE_MS = int(1000 / RECOMMENDED_REFRESH_HZ)
DEMO_SLEW_DURATION_S = 5.0
ACK_RESPONSE = ":ACK#"
ACK_BENCHMARK_DURATION_S = 2.5
ACK_TIMEOUT_S = 1.0


@dataclass(frozen=True)
class TargetState:
    ra_deg: float
    dec_deg: float
    name: Optional[str]


def build_frame(token: str, payload: str = "") -> str:
    if not token.startswith(":"):
        raise ValueError(f"token must start with ':'; got {token!r}")
    if len(token) < 2:
        raise ValueError(f"token is too short: {token!r}")
    return f"{token}{payload}#"


def send_frame(ser: serial.Serial, token: str, payload: str = "") -> None:
    frame = build_frame(token, payload)
    ser.write(frame.encode("ascii"))
    ser.flush()
    print(f">>> {frame}")


def drain_responses(ser: serial.Serial, quiet_for_s: float = 0.25) -> list[str]:
    deadline = time.monotonic() + quiet_for_s
    responses: list[str] = []

    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("ascii", errors="replace").strip()
        if not text:
            continue
        responses.append(text)
        print(f"<<< {text}")
        deadline = time.monotonic() + quiet_for_s

    return responses


def wait_for_ack(ser: serial.Serial, timeout_s: float = 1.0) -> Optional[float]:
    start = time.monotonic()
    deadline = start + timeout_s

    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        text = raw.decode("ascii", errors="replace").strip()
        if not text:
            continue
        print(f"<<< {text}")
        if text == ACK_RESPONSE:
            return time.monotonic() - start

    return None


def send_target_and_wait_ack(
    ser: serial.Serial,
    ra_deg: float,
    dec_deg: float,
    name: str,
    timeout_s: float = ACK_TIMEOUT_S,
) -> bool:
    send_frame(ser, ":TS", f"{ra_deg:.4f},{dec_deg:.4f},{name}")
    ack_rtt_s = wait_for_ack(ser, timeout_s=timeout_s)
    if ack_rtt_s is None:
        print("ACK timeout after :TS update")
        return False
    return True


def compute_linear_slew_target(elapsed_s: float) -> tuple[float, float]:
    # Constant-velocity sky motion model to mimic telescope slewing.
    ra_start_deg = 210.0
    dec_start_deg = 22.0
    ra_rate_deg_per_s = 1.8
    dec_rate_deg_per_s = 0.35

    ra = (ra_start_deg + (ra_rate_deg_per_s * elapsed_s)) % 360.0
    dec = dec_start_deg + (dec_rate_deg_per_s * elapsed_s)
    if dec > 89.0:
        dec = 89.0
    if dec < -89.0:
        dec = -89.0
    return ra, dec


def benchmark_ack_limited_rate(ser: serial.Serial, duration_s: float = ACK_BENCHMARK_DURATION_S) -> None:
    print("\n=== ACK throughput benchmark (:TS -> :ACK#) ===")
    print(f"Measuring for about {duration_s:.1f} s...")

    rtts: list[float] = []
    start_time = time.monotonic()
    update_index = 0
    benchmark_name = "AckBench"

    while (time.monotonic() - start_time) < duration_s:
        elapsed = time.monotonic() - start_time
        ra, dec = compute_linear_slew_target(elapsed)
        send_frame(ser, ":TS", f"{ra:.4f},{dec:.4f},{benchmark_name}")
        ack_rtt_s = wait_for_ack(ser, timeout_s=ACK_TIMEOUT_S)
        if ack_rtt_s is None:
            print("ACK timeout during benchmark; stopping early.")
            break
        rtts.append(ack_rtt_s)
        update_index += 1

    send_frame(ser, ":TC")
    wait_for_ack(ser, timeout_s=ACK_TIMEOUT_S)
    drain_responses(ser, quiet_for_s=0.1)

    if not rtts:
        print("No ACK RTT samples collected.")
        return

    avg_rtt_s = statistics.mean(rtts)
    min_rtt_s = min(rtts)
    max_rtt_s = max(rtts)
    p95_rtt_s = max(rtts) if len(rtts) < 20 else statistics.quantiles(rtts, n=20)[-1]

    avg_hz = 1.0 / avg_rtt_s if avg_rtt_s > 0.0 else 0.0
    conservative_hz = 1.0 / p95_rtt_s if p95_rtt_s > 0.0 else 0.0
    peak_hz = 1.0 / min_rtt_s if min_rtt_s > 0.0 else 0.0

    print(f"ACK samples: {len(rtts)}")
    print(
        "ACK RTT (ms): "
        f"avg={avg_rtt_s * 1000.0:.1f}, "
        f"p95={p95_rtt_s * 1000.0:.1f}, "
        f"min={min_rtt_s * 1000.0:.1f}, "
        f"max={max_rtt_s * 1000.0:.1f}"
    )
    print(
        "Estimated ACK-limited max update rate (Hz): "
        f"conservative~{conservative_hz:.1f}, "
        f"average~{avg_hz:.1f}, "
        f"peak~{peak_hz:.1f}"
    )
    print(
        "Use the conservative value as an upper bound, then run below it "
        "for smoother display updates."
    )


def list_ports() -> list[str]:
    return [p.device for p in serial.tools.list_ports.comports()]


def try_probe_target_command(ser: serial.Serial) -> bool:
    send_frame(ser, ":TG")
    responses = drain_responses(ser, quiet_for_s=0.35)
    for response in responses:
        if response.startswith(":TG") and response.endswith("#"):
            return True
        if response == ":ER01#" or response == ":ER02#" or response == ":ERR#":
            # Any framed parser response confirms we are connected to firmware.
            return True
    return False


def connect_serial(port: str, baud: int, timeout: float) -> serial.Serial:
    ser = serial.Serial(port, baud, timeout=timeout, write_timeout=1)

    # Many boards reset when DTR toggles. Allow boot time before probing.
    time.sleep(2.0)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    for attempt in range(1, CONNECT_RETRIES + 1):
        print(f"Probing command link ({attempt}/{CONNECT_RETRIES})...")
        if try_probe_target_command(ser):
            print("Serial command link is active.")
            return ser
        time.sleep(CONNECT_RETRY_DELAY_S)

    ser.close()
    raise RuntimeError(
        "Connected to serial port, but no valid response to :TG#. "
        "Ensure firmware is running and no other monitor is attached."
    )


def wait_for_enter(message: str) -> None:
    input(f"{message} Press Enter to continue...")


def cycle_targets(ser: serial.Serial) -> None:
    print("\n=== Test 1: target cycle ===")
    targets: list[TargetState] = [
        TargetState(101.2871, -16.7161, "Sirius"),
        TargetState(0.0, 0.0, None),
        TargetState(279.2346, 38.7836, "Vega"),
        TargetState(0.0, 0.0, None),
        TargetState(83.8221, -5.3911, "Betelgeuse"),
    ]

    for index, target in enumerate(targets, start=1):
        print(f"\nTarget {index}/5")
        if target.name:
            send_frame(ser, ":TS", f"{target.ra_deg:.4f},{target.dec_deg:.4f},{target.name}")
        else:
            send_frame(ser, ":TC")

        drain_responses(ser)
        send_frame(ser, ":TG")
        drain_responses(ser)
        wait_for_enter(f"Inspect target state {index}/5.")


def run_slew_cadence_test(ser: serial.Serial) -> None:
    print("\n=== Test 2: simulated RA/Dec slew cadence ===")
    wait_for_enter("Begin the cadence test when ready.")

    cadence_ms = [50, 100, 150, 200, 250]
    duration_s = 5.0
    target_name = "SlewCadence"

    for cadence in cadence_ms:
        print(f"\nCadence {cadence} ms for about {duration_s:.1f} s")
        start_time = time.monotonic()
        next_tick = start_time
        update_index = 0
        missed_acks = 0

        while True:
            elapsed = time.monotonic() - start_time
            if elapsed >= duration_s:
                break

            ra, dec = compute_linear_slew_target(elapsed)
            if not send_target_and_wait_ack(ser, ra, dec, target_name):
                missed_acks += 1

            update_index += 1
            next_tick += cadence / 1000.0
            sleep_s = next_tick - time.monotonic()
            if sleep_s > 0.0:
                time.sleep(sleep_s)

        print(f"Sent {update_index} updates, ACK timeouts: {missed_acks}")

    send_frame(ser, ":TC")
    wait_for_ack(ser, timeout_s=ACK_TIMEOUT_S)
    drain_responses(ser, quiet_for_s=0.1)


def run_single_slew_demo(ser: serial.Serial, hz: float, duration_s: float, title: str) -> None:
    cadence_s = 1.0 / hz
    cadence_ms = int(round(cadence_s * 1000.0))

    print(f"\n=== {title} ===")
    print(f"Slew update rate: {hz:.2f} Hz ({cadence_ms} ms cadence) for {duration_s:.1f} s")
    wait_for_enter("Begin the slew demo when ready.")

    target_name = "SlewDemo"

    start_time = time.monotonic()
    next_tick = start_time
    update_index = 0
    missed_acks = 0

    while True:
        elapsed = time.monotonic() - start_time
        if elapsed >= duration_s:
            break

        ra, dec = compute_linear_slew_target(elapsed)
        if not send_target_and_wait_ack(ser, ra, dec, target_name):
            missed_acks += 1
        update_index += 1

        next_tick += cadence_s
        sleep_s = next_tick - time.monotonic()
        if sleep_s > 0.0:
            time.sleep(sleep_s)

    send_frame(ser, ":TC")
    wait_for_ack(ser, timeout_s=ACK_TIMEOUT_S)
    drain_responses(ser, quiet_for_s=0.1)
    print(f"Sent {update_index} updates, ACK timeouts: {missed_acks}")


def run_recommended_slew_demo(ser: serial.Serial, duration_s: float = DEMO_SLEW_DURATION_S) -> None:
    run_single_slew_demo(
        ser,
        hz=RECOMMENDED_REFRESH_HZ,
        duration_s=duration_s,
        title="Test 3: recommended-cadence slew demo",
    )


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial port, for example COM5")
    parser.add_argument("--baud", type=int, default=BAUD_RATE, help="Serial baud rate")
    parser.add_argument("--timeout", type=float, default=0.25, help="Serial read timeout in seconds")
    parser.add_argument(
        "--hz",
        type=float,
        help=(
            "Run only one direct slew demo at this update rate in Hz "
            "(for example --hz 10)."
        ),
    )
    parser.add_argument(
        "-t",
        "--time",
        type=float,
        default=DEMO_SLEW_DURATION_S,
        help="Slew demo duration in seconds (default: 5.0).",
    )
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)

    if args.hz is not None and args.hz <= 0.0:
        print("--hz must be greater than 0")
        return 2
    if args.time <= 0.0:
        print("-t/--time must be greater than 0")
        return 2

    available = list_ports()
    if args.port not in available:
        print(f"Requested port {args.port} was not found among: {available}")
        return 2

    try:
        with connect_serial(args.port, args.baud, args.timeout) as ser:
            print(f"Connected to {args.port} at {args.baud} baud")
            benchmark_ack_limited_rate(ser)

            if args.hz is not None:
                run_single_slew_demo(
                    ser,
                    hz=args.hz,
                    duration_s=args.time,
                    title="Direct slew demo (--hz mode)",
                )
                return 0

            cycle_targets(ser)
            wait_for_enter("Test 1 complete.")

            run_slew_cadence_test(ser)
            wait_for_enter("Test 2 complete.")

            run_recommended_slew_demo(ser, duration_s=args.time)
            wait_for_enter("Test 3 complete.")
    except serial.SerialException as exc:
        print(f"Serial error: {exc}")
        return 3
    except RuntimeError as exc:
        print(f"Connection check failed: {exc}")
        return 4

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))