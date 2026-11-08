"""Battery-life estimate from USER-MEASURED currents on the battery side.

No defaults for electrical measurements: unknown values must remain unknown.
Example inputs are deliberately not supplied as board measurements.
"""
import argparse
import math


def nonnegative(value):
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise argparse.ArgumentTypeError("must be finite and >= 0")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capacity-mah", type=nonnegative, required=True)
    parser.add_argument("--active-ma", type=nonnegative, required=True)
    parser.add_argument("--sleep-ma", type=nonnegative, required=True)
    parser.add_argument("--active-seconds", type=nonnegative, required=True)
    parser.add_argument("--sleep-seconds", type=nonnegative, required=True)
    parser.add_argument("--usable-fraction", type=nonnegative, required=True,
                        help="usable battery capacity fraction, >0 and <=1")
    args = parser.parse_args()
    cycle = args.active_seconds + args.sleep_seconds
    if cycle <= 0 or args.capacity_mah <= 0 or not 0 < args.usable_fraction <= 1:
        parser.error("cycle and capacity must be positive; usable fraction must be in (0,1]")
    average = (args.active_ma * args.active_seconds + args.sleep_ma * args.sleep_seconds) / cycle
    if average <= 0:
        parser.error("average measured current must be positive")
    hours = args.capacity_mah * args.usable_fraction / average
    print(f"Average current: {average:.6f} mA")
    print(f"Estimated runtime: {hours:.2f} hours ({hours / 24:.2f} days)")
    print("Estimate only: include startup/transmission charge, regulator losses and all board loads.")
    print("Currents must be measured at the same battery voltage and supply point; do not mix USB and battery values.")


if __name__ == "__main__":
    main()
