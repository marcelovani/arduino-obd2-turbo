"""
Visual scenario monitor — terminal simulation of the OLED display.

Shows a driving scenario playing in real time with:
  - Simulated OLED panel with current values
  - Progress bar through the scenario
  - BOV trigger events highlighted in yellow
  - Parameter tuning via command-line flags

Usage:
    python tests/visual_monitor.py                     # run all scenarios
    python tests/visual_monitor.py first_gear_change   # run one scenario
    python tests/visual_monitor.py --list              # list available scenarios

Tuning BOV thresholds:
    python tests/visual_monitor.py --throttle-high 35 --rpm-min 1200

Run via Makefile:
    make scenario
    make scenario SCENARIO=first_gear_change
    make scenario SCENARIO=first_gear_change THROTTLE_HIGH=35 RPM_MIN=1200
"""

import sys
import os
import time
import argparse

sys.path.insert(0, os.path.dirname(__file__))

from rich.console   import Console
from rich.panel     import Panel
from rich.table     import Table
from rich.layout    import Layout
from rich.live      import Live
from rich.progress  import Progress, BarColumn, TextColumn, TimeElapsedColumn
from rich.text      import Text
from rich           import box

from obd_logic            import BovTrigger, estimate_gear
from scenarios.definitions import SCENARIOS, EXPECTED_BOV_COUNTS

console = Console()


# ── OLED simulation ───────────────────────────────────────────────────────

def make_oled_panel(tps: float, speed: float, rpm: float, gear: int,
                    bov_recent: bool, bov_count: int) -> Panel:
    """Render a fake OLED display as a Rich panel."""
    lines = Text()

    if bov_recent:
        lines.append(f"  *** PSSSSH! #{bov_count} ***\n", style="bold yellow on black")
    else:
        lines.append(f"  BOV count: {bov_count}\n", style="dim white on black")

    lines.append(f"  TPS:  {tps:5.0f}%   G:{gear}\n", style="bold green on black")
    lines.append(f"  SPD:  {speed:5.0f} km/h\n",       style="bold cyan on black")
    lines.append(f"  RPM:  {rpm:5.0f}\n",              style="bold white on black")

    # Throttle bar (28 chars wide to fit in panel)
    bar_len = 28
    filled  = int(bar_len * tps / 100)
    bar     = "█" * filled + "░" * (bar_len - filled)
    lines.append(f"  [{bar}]\n", style="green on black")

    return Panel(lines, title="[white]── OLED ──[/white]",
                 border_style="white", width=38)


def make_data_table(tps: float, speed: float, rpm: float, gear: int,
                    prev_tps: float, bov: BovTrigger) -> Table:
    """Render a live data table alongside the OLED panel."""
    t = Table(box=box.SIMPLE, show_header=True, header_style="bold blue")
    t.add_column("Parameter", style="cyan",  width=16)
    t.add_column("Value",     style="white", width=14)
    t.add_column("Threshold", style="dim",   width=14)

    tps_style   = "bold yellow" if tps   < bov.throttle_low else "green"
    rpm_style   = "bold green"  if rpm   > bov.rpm_min      else "red"
    gear_style  = "bold green"  if 0 < gear <= bov.max_gear else "dim white"
    prev_style  = "bold green"  if prev_tps > bov.throttle_high else "dim white"

    t.add_row("Throttle (now)",  f"[{tps_style}]{tps:6.1f} %[/]",
              f"> {bov.throttle_low:.0f}% = lifted")
    t.add_row("Throttle (prev)", f"[{prev_style}]{prev_tps:6.1f} %[/]",
              f"> {bov.throttle_high:.0f}% = was floored")
    t.add_row("RPM",             f"[{rpm_style}]{rpm:6.0f}[/]",
              f"> {bov.rpm_min:.0f} = in boost")
    t.add_row("Speed",           f"{speed:6.0f} km/h", "")
    t.add_row("Gear",            f"[{gear_style}]{gear}[/]",
              f"<= {bov.max_gear} = triggers BOV")

    return t


def make_event_log(events: list[dict], last_n: int = 4) -> Panel:
    """Show the last N BOV trigger events."""
    if not events:
        return Panel("[dim]No BOV events yet[/dim]", title="BOV Events",
                     border_style="yellow")
    lines = Text()
    for e in events[-last_n:]:
        lines.append(
            f"  t={e['time_ms']:5}ms  TPS {e['prev_tps']:.0f}→{e['tps']:.0f}%"
            f"  RPM {e['rpm']:.0f}  Gear {e['gear']}\n",
            style="yellow"
        )
    return Panel(lines, title=f"[yellow]BOV Events ({len(events)} total)[/yellow]",
                 border_style="yellow")


# ── Scenario runner ───────────────────────────────────────────────────────

def run_scenario(name: str, bov: BovTrigger, speed_factor: float = 1.0):
    """Replay a scenario in real time with live terminal output."""
    scenario  = SCENARIOS[name]
    expected  = EXPECTED_BOV_COUNTS.get(name, "?")
    last_t    = scenario[-1][0]
    bov_recent_until = 0.0

    console.print(f"\n[bold cyan]Scenario:[/bold cyan] {name}  "
                  f"[dim]Expected BOV count: {expected}[/dim]")
    console.print(f"[dim]Thresholds: TPS_HIGH={bov.throttle_high}%  "
                  f"TPS_LOW={bov.throttle_low}%  "
                  f"RPM_MIN={bov.rpm_min:.0f}  "
                  f"MAX_GEAR={bov.max_gear}  "
                  f"COOLDOWN={bov.cooldown_ms}ms[/dim]\n")

    prev_tps = 0.0

    with Live(console=console, refresh_per_second=20) as live:
        for i, (t_ms, tps, rpm, speed) in enumerate(scenario):

            triggered = bov.update(tps, rpm, speed, t_ms)
            if triggered:
                bov_recent_until = time.time() + 0.8

            gear       = estimate_gear(rpm, speed)
            bov_recent = time.time() < bov_recent_until
            prev_tps_val = bov._prev_tps if not triggered else tps  # after update

            # Progress through scenario
            pct   = int(100 * t_ms / max(last_t, 1))
            bar   = "█" * (pct // 4) + "░" * (25 - pct // 4)
            progress_line = f"  [dim]t={t_ms:4}ms[/dim]  [{bar}]  {pct:3}%"

            layout = Table.grid(padding=1)
            layout.add_row(
                make_oled_panel(tps, speed, rpm, gear, bov_recent, bov.count),
                make_data_table(tps, speed, rpm, gear, prev_tps, bov),
            )

            from rich.console import Group
            live.update(Group(
                Text(progress_line),
                layout,
                make_event_log(bov.events),
            ))

            prev_tps = tps

            # Wait until the next data point (scaled by speed_factor)
            if i + 1 < len(scenario):
                next_t   = scenario[i + 1][0]
                wait_ms  = (next_t - t_ms) / speed_factor
                time.sleep(wait_ms / 1000)

    # Final summary
    result_style = "bold green" if bov.count == expected else "bold red"
    console.print(f"\n[{result_style}]Result: {bov.count} BOV trigger(s) "
                  f"(expected {expected})[/]\n")


# ── CLI ───────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Visual BOV scenario monitor — simulates driving on terminal"
    )
    parser.add_argument(
        "scenario", nargs="?", default=None,
        help="Scenario name to run (default: run all). Use --list to see options."
    )
    parser.add_argument("--list",    action="store_true", help="List available scenarios")
    parser.add_argument("--speed",   type=float, default=1.0,
                        help="Playback speed multiplier (default 1.0, try 0.5 for slow-mo)")

    # Tunable BOV thresholds
    parser.add_argument("--throttle-high", type=float, default=None,
                        help=f"Override BOV_THROTTLE_HIGH (default 40%%)")
    parser.add_argument("--throttle-low",  type=float, default=None,
                        help=f"Override BOV_THROTTLE_LOW (default 10%%)")
    parser.add_argument("--rpm-min",       type=float, default=None,
                        help=f"Override BOV_RPM_MIN (default 1500)")
    parser.add_argument("--max-gear",      type=int,   default=None,
                        help=f"Override BOV_MAX_GEAR (default 2)")
    parser.add_argument("--cooldown",      type=float, default=None,
                        help=f"Override BOV_COOLDOWN_MS (default 2000)")

    args = parser.parse_args()

    if args.list:
        console.print("\n[bold]Available scenarios:[/bold]")
        for name, expected in EXPECTED_BOV_COUNTS.items():
            console.print(f"  {name:<35} expected BOV: {expected}")
        return

    # Build BovTrigger with any overrides
    kwargs = {}
    if args.throttle_high is not None: kwargs["throttle_high"] = args.throttle_high
    if args.throttle_low  is not None: kwargs["throttle_low"]  = args.throttle_low
    if args.rpm_min       is not None: kwargs["rpm_min"]       = args.rpm_min
    if args.max_gear      is not None: kwargs["max_gear"]      = args.max_gear
    if args.cooldown      is not None: kwargs["cooldown_ms"]   = args.cooldown

    names = [args.scenario] if args.scenario else list(SCENARIOS.keys())

    for name in names:
        if name not in SCENARIOS:
            console.print(f"[red]Unknown scenario: {name}[/red]")
            console.print("Use --list to see available scenarios.")
            sys.exit(1)
        run_scenario(name, BovTrigger(**kwargs), speed_factor=args.speed)
        if len(names) > 1:
            time.sleep(0.5)


if __name__ == "__main__":
    main()
