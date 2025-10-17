#!/usr/bin/env python3
"""VCD Waveform Analysis Script for VGA Nyancat

Analyzes VCD trace files to verify VGA timing and signal integrity.
Detects timing violations, signal glitches, and validates sync signals.

Usage:
    python3 analyze-vcd.py waves.vcd [--report report.txt]

Requirements:
    None - uses built-in Python libraries only
"""

import sys
import argparse
from collections import defaultdict


class VCDParser:
    """Minimal VCD parser - no external dependencies"""

    def __init__(self, filename):
        self.filename = filename
        self.timescale = "1 ns"
        self.endtime = 0
        self.signals = {}  # id_code -> signal_name
        self.signal_data = defaultdict(list)  # signal_name -> [(time, value), ...]

    def parse(self):
        """Parse VCD file"""
        with open(self.filename, "r") as f:
            in_header = True
            current_time = 0

            for line in f:
                line = line.strip()

                if not line:
                    continue

                # Header section
                if in_header:
                    if line.startswith("$timescale"):
                        self.timescale = line.split()[1]
                    elif line.startswith("$var"):
                        # $var type size id_code reference_name $end
                        parts = line.split()
                        if len(parts) >= 5:
                            id_code = parts[3]
                            ref_name = parts[4]
                            self.signals[id_code] = ref_name
                    elif line.startswith("$enddefinitions"):
                        in_header = False
                    continue

                # Value change dump section
                if line.startswith("#"):
                    # Timestamp
                    current_time = int(line[1:])
                    self.endtime = max(self.endtime, current_time)
                elif line.startswith("b"):
                    # Binary value: b0101 id_code
                    parts = line.split()
                    if len(parts) >= 2:
                        value = parts[0][1:]  # Remove 'b' prefix
                        id_code = parts[1]
                        if id_code in self.signals:
                            signal_name = self.signals[id_code]
                            self.signal_data[signal_name].append((current_time, value))
                elif len(line) >= 2 and line[0] in "01xzXZ":
                    # Scalar value: 0id_code or 1id_code
                    value = line[0]
                    id_code = line[1:]
                    if id_code in self.signals:
                        signal_name = self.signals[id_code]
                        self.signal_data[signal_name].append((current_time, value))

        return True

    def get_signal(self, signal_name):
        """Get signal data by name (partial match)"""
        for name, data in self.signal_data.items():
            if signal_name in name:
                return data
        return None


class VGATimingAnalyzer:
    """Analyzes VGA timing signals from VCD trace"""

    def __init__(self, vcd_file):
        self.vcd_file = vcd_file
        self.vcd = None
        self.analysis_results = {
            "hsync_count": 0,
            "vsync_count": 0,
            "hsync_period": [],
            "vsync_period": [],
            "activevideo_cycles": 0,
            "glitches": [],
            "timing_violations": [],
        }

    def parse_vcd(self):
        """Parse VCD file and extract signal data"""
        print(f"Parsing VCD file: {self.vcd_file}")

        try:
            self.vcd = VCDParser(self.vcd_file)
            self.vcd.parse()
        except Exception as e:
            print(f"Error parsing VCD: {e}")
            return False

        print(f"Timescale: {self.vcd.timescale}")
        print(f"End time: {self.vcd.endtime}")

        # List available signals
        signal_count = len(self.vcd.signal_data)
        print(f"Found {signal_count} signals")

        return True

    def get_signal(self, signal_name):
        """Get signal by name (search in hierarchy)"""
        return self.vcd.get_signal(signal_name)

    def analyze_sync_signals(self):
        """Analyze hsync and vsync timing"""
        print("\n=== Analyzing Sync Signals ===")

        # Find hsync signal
        hsync_sig = self.get_signal("hsync")
        if hsync_sig:
            hsync_edges = self._find_edges(hsync_sig, falling=True)
            self.analysis_results["hsync_count"] = len(hsync_edges)

            # Calculate hsync period
            for i in range(1, len(hsync_edges)):
                period = hsync_edges[i] - hsync_edges[i - 1]
                self.analysis_results["hsync_period"].append(period)

            if self.analysis_results["hsync_period"]:
                avg_period = sum(self.analysis_results["hsync_period"]) / len(
                    self.analysis_results["hsync_period"]
                )
                print(f"  Hsync pulses: {self.analysis_results['hsync_count']}")
                print(f"  Avg hsync period: {avg_period:.0f} time units")
        else:
            print("  Warning: hsync signal not found")

        # Find vsync signal
        vsync_sig = self.get_signal("vsync")
        if vsync_sig:
            vsync_edges = self._find_edges(vsync_sig, falling=True)
            self.analysis_results["vsync_count"] = len(vsync_edges)

            # Calculate vsync period
            for i in range(1, len(vsync_edges)):
                period = vsync_edges[i] - vsync_edges[i - 1]
                self.analysis_results["vsync_period"].append(period)

            if self.analysis_results["vsync_period"]:
                avg_period = sum(self.analysis_results["vsync_period"]) / len(
                    self.analysis_results["vsync_period"]
                )
                print(f"  Vsync pulses: {self.analysis_results['vsync_count']}")
                print(f"  Avg vsync period: {avg_period:.0f} time units")
        else:
            print("  Warning: vsync signal not found")

    def analyze_activevideo(self):
        """Analyze activevideo signal"""
        print("\n=== Analyzing Active Video ===")

        activevideo_sig = self.get_signal("activevideo")
        if not activevideo_sig:
            print("  Warning: activevideo signal not found")
            return

        tv = activevideo_sig
        total_high = 0

        for i in range(len(tv) - 1):
            time, value = tv[i]
            next_time = tv[i + 1][0]

            if value == "1":
                total_high += next_time - time

        self.analysis_results["activevideo_cycles"] = total_high
        print(f"  Active video cycles: {total_high}")

    def detect_glitches(self):
        """Detect signal glitches (very short pulses)"""
        print("\n=== Detecting Glitches ===")

        glitch_threshold = 10  # Time units

        # Check key signals for glitches (exclude clk - it's supposed to toggle fast)
        key_signals = ["hsync", "vsync", "activevideo"]

        for sig_name in key_signals:
            sig = self.get_signal(sig_name)
            if not sig:
                continue

            tv = sig
            if len(tv) < 2:
                continue

            for i in range(len(tv) - 1):
                time1, val1 = tv[i]
                time2, val2 = tv[i + 1]

                if time2 - time1 < glitch_threshold and val1 != val2:
                    glitch = {
                        "signal": sig_name,
                        "time": time1,
                        "duration": time2 - time1,
                    }
                    self.analysis_results["glitches"].append(glitch)

        if self.analysis_results["glitches"]:
            print(
                f"  Found {len(self.analysis_results['glitches'])} potential glitches"
            )
            for g in self.analysis_results["glitches"][:5]:  # Show first 5
                print(f"    {g['signal']} @ {g['time']}: {g['duration']} units")
        else:
            print("  No glitches detected")

    def validate_vga_timing(self):
        """Validate VGA timing against expected values"""
        print("\n=== VGA Timing Validation ===")

        # Expected values for VGA 640×480 @ 72Hz
        expected_hsync_period = 832 * 2  # H_TOTAL * 2 (rising/falling edges)
        expected_vsync_period = 832 * 520 * 2  # H_TOTAL * V_TOTAL * 2

        tolerance = 0.05  # 5% tolerance

        # Validate hsync period
        if self.analysis_results["hsync_period"]:
            avg_hsync = sum(self.analysis_results["hsync_period"]) / len(
                self.analysis_results["hsync_period"]
            )
            deviation = abs(avg_hsync - expected_hsync_period) / expected_hsync_period

            if deviation > tolerance:
                violation = f"Hsync period deviation: {deviation*100:.1f}% (expected {expected_hsync_period}, got {avg_hsync:.0f})"
                self.analysis_results["timing_violations"].append(violation)
                print(f"  ✗ {violation}")
            else:
                print(
                    f"  ✓ Hsync period: {avg_hsync:.0f} (expected {expected_hsync_period})"
                )

        # Validate vsync period
        if self.analysis_results["vsync_period"]:
            avg_vsync = sum(self.analysis_results["vsync_period"]) / len(
                self.analysis_results["vsync_period"]
            )
            deviation = abs(avg_vsync - expected_vsync_period) / expected_vsync_period

            if deviation > tolerance:
                violation = f"Vsync period deviation: {deviation*100:.1f}% (expected {expected_vsync_period}, got {avg_vsync:.0f})"
                self.analysis_results["timing_violations"].append(violation)
                print(f"  ✗ {violation}")
            else:
                print(
                    f"  ✓ Vsync period: {avg_vsync:.0f} (expected {expected_vsync_period})"
                )

    def _find_edges(self, signal_tv, falling=True):
        """Find falling or rising edges in signal data"""
        edges = []

        for i in range(len(signal_tv) - 1):
            time1, val1 = signal_tv[i]
            time2, val2 = signal_tv[i + 1]

            if falling and val1 == "1" and val2 == "0":
                edges.append(time2)
            elif not falling and val1 == "0" and val2 == "1":
                edges.append(time2)

        return edges

    def list_signals(self):
        """List all available signals in VCD"""
        print("\nAvailable signals:")
        for sig_name in sorted(self.vcd.signal_data.keys()):
            print(f"  {sig_name}")

    def generate_report(self, output_file=None):
        """Generate analysis report"""
        report = []
        report.append("=" * 60)
        report.append("VGA Nyancat VCD Waveform Analysis Report")
        report.append("=" * 60)
        report.append(f"\nVCD File: {self.vcd_file}")
        report.append(f"Timescale: {self.vcd.timescale}")
        report.append(f"Simulation duration: {self.vcd.endtime} time units")
        report.append(f"Signals analyzed: {len(self.vcd.signal_data)}")

        report.append("\n--- Sync Signal Statistics ---")
        report.append(f"Hsync pulses: {self.analysis_results['hsync_count']}")
        report.append(f"Vsync pulses: {self.analysis_results['vsync_count']}")

        if self.analysis_results["hsync_period"]:
            avg = sum(self.analysis_results["hsync_period"]) / len(
                self.analysis_results["hsync_period"]
            )
            report.append(f"Avg hsync period: {avg:.2f} time units")

        if self.analysis_results["vsync_period"]:
            avg = sum(self.analysis_results["vsync_period"]) / len(
                self.analysis_results["vsync_period"]
            )
            report.append(f"Avg vsync period: {avg:.2f} time units")

        report.append(
            f"\nActive video cycles: {self.analysis_results['activevideo_cycles']}"
        )

        report.append(f"\n--- Issues Detected ---")
        report.append(f"Glitches: {len(self.analysis_results['glitches'])}")
        report.append(
            f"Timing violations: {len(self.analysis_results['timing_violations'])}"
        )

        if self.analysis_results["timing_violations"]:
            report.append("\nTiming Violations:")
            for v in self.analysis_results["timing_violations"]:
                report.append(f"  - {v}")

        report.append("\n" + "=" * 60)

        report_text = "\n".join(report)

        if output_file:
            with open(output_file, "w") as f:
                f.write(report_text)
            print(f"\nReport saved to: {output_file}")

        return report_text


def main():
    parser = argparse.ArgumentParser(
        description="Analyze VCD waveform traces from VGA Nyancat simulation"
    )
    parser.add_argument("vcd_file", help="Input VCD file")
    parser.add_argument("--report", help="Output report file (optional)")
    parser.add_argument(
        "--signals", action="store_true", help="List all signals in VCD file"
    )

    args = parser.parse_args()

    analyzer = VGATimingAnalyzer(args.vcd_file)

    if not analyzer.parse_vcd():
        print("Error: Failed to parse VCD file")
        return 1

    if args.signals:
        analyzer.list_signals()
        return 0

    # Run analysis
    analyzer.analyze_sync_signals()
    analyzer.analyze_activevideo()
    analyzer.detect_glitches()
    analyzer.validate_vga_timing()

    # Generate report
    print("\n" + analyzer.generate_report(args.report))

    # Exit with error if violations found
    if analyzer.analysis_results["timing_violations"]:
        print("\n❌ Analysis FAILED: Timing violations detected")
        return 1
    else:
        print("\n✅ Analysis PASSED: No timing violations")
        return 0


if __name__ == "__main__":
    sys.exit(main())
