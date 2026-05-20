"""D1.7 1100-cell COLREG coverage heatmap generator.

Reads traceability.csv and generates an HTML heatmap showing
which cells of the 1100-cell cube (11 Rule × 4 ODD × 5 Disturbance × 5 Seed)
are covered.

Usage:
    python tools/sil/scenarios_coverage.py \\
        --traceability reports/traceability.csv \\
        --output reports/coverage_heatmap.html
"""
from __future__ import annotations

import argparse
import textwrap
from datetime import datetime, timezone
from pathlib import Path

from coverage_cube import CoverageCube, COLREG_RULES, ODD_ZONES, TOTAL_CELLS


def generate_heatmap(traceability_csv: Path, output_html: Path) -> None:
    cube = CoverageCube.load_from_csv(str(traceability_csv))
    matrix = cube.to_heatmap_matrix()
    cells_lit = cube.cells_lit()
    html = _render_heatmap_html(matrix, cells_lit)
    output_html.write_text(html, encoding="utf-8")


def _render_heatmap_html(
    matrix: list[list[int]],
    cells_lit: int,
) -> str:
    rows_html: list[str] = []
    for i, rule in enumerate(COLREG_RULES):
        cells = ""
        for j, odd in enumerate(ODD_ZONES):
            count = matrix[i][j]
            max_per_cell = 25  # 5 disturbance × 5 seed
            ratio = count / max_per_cell if max_per_cell > 0 else 0.0
            r = int(255 * (1.0 - ratio))
            g = int(255 - r // 2)
            b = 100
            color = f"rgb({r}, {g}, {b})"
            cells += (
                f'<td style="background:{color};color:#fff;text-align:center;'
                f'padding:6px">{count}/{max_per_cell}</td>'
            )
        rows_html.append(
            f"<tr><td style='font-weight:bold'>{rule}</td>{cells}</tr>"
        )

    odd_headers = "".join(f"<th>{o}</th>" for o in ODD_ZONES)
    pct = cells_lit / TOTAL_CELLS * 100 if TOTAL_CELLS > 0 else 0.0

    return textwrap.dedent(f"""\
    <!DOCTYPE html>
    <html><head><meta charset="utf-8">
    <title>COLREGs Coverage Cube Heatmap</title>
    <style>
      body {{ font-family: -apple-system, sans-serif; margin: 20px; }}
      table {{ border-collapse: collapse; }}
      th, td {{ border: 1px solid #333; padding: 8px; }}
      th {{ background: #444; color: #fff; }}
    </style></head><body>
    <h1>1100-Cell COLREGs Coverage Heatmap</h1>
    <p>Generated: {datetime.now(tz=timezone.utc).isoformat()}</p>
    <p>Cells lit: <strong>{cells_lit}</strong> / {TOTAL_CELLS} ({pct:.1f}%)</p>
    <table>
    <tr><th>Rule</th>{odd_headers}</tr>
    {"".join(rows_html)}
    </table>
    <p><em>Cell format: lit_cells / 25 (5 disturbance × 5 seed). \
Green=high coverage, Yellow=medium, Red=low.</em></p>
    </body></html>
    """)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--traceability", required=True, type=Path)
    ap.add_argument("--output", required=True, type=Path)
    args = ap.parse_args()
    generate_heatmap(args.traceability, args.output)
