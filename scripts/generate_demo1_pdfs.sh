#!/usr/bin/env bash
# generate_demo1_pdfs.sh — Generate professional HTML→PDF documents for DEMO-1
# Uses pandoc to generate styled HTML, then Playwright to compile high-fidelity PDFs.
#
# Usage:
#   bash scripts/generate_demo1_pdfs.sh
#
# Output:
#   evidence/demo1-pdfs/ConOps-v0.1.pdf
#   evidence/demo1-pdfs/VV-Plan-v0.1.pdf
#   evidence/demo1-pdfs/cert-evidence-tracking.pdf
#   evidence/demo1-pdfs/simulator-qualification-report.pdf
#

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$PROJECT_ROOT/evidence/demo1-pdfs"
mkdir -p "$OUT_DIR"

DATE=$(date +%Y-%m-%d)

# CSS for professional print-ready styling
CSS_FILE="$OUT_DIR/.print-style.css"
cat > "$CSS_FILE" << 'CSSEOF'
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap');

:root {
  --brand-primary: #1a365d;
  --brand-accent: #2b6cb0;
  --text-primary: #1a202c;
  --text-secondary: #4a5568;
  --border: #e2e8f0;
  --bg-subtle: #f7fafc;
}

body {
  font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
  color: var(--text-primary);
  line-height: 1.7;
  max-width: 210mm;
  margin: 0 auto;
  padding: 20mm;
  font-size: 11pt;
}

h1 {
  color: var(--brand-primary);
  border-bottom: 3px solid var(--brand-accent);
  padding-bottom: 0.5em;
  font-size: 1.8em;
  margin-top: 2em;
}

h1:first-of-type {
  margin-top: 0;
  font-size: 2.2em;
  text-align: center;
  border-bottom: 4px solid var(--brand-accent);
}

h2 {
  color: var(--brand-accent);
  border-bottom: 1px solid var(--border);
  padding-bottom: 0.3em;
  font-size: 1.4em;
}

h3 { color: var(--text-primary); font-size: 1.1em; }

table {
  width: 100%;
  border-collapse: collapse;
  margin: 1em 0;
  font-size: 0.9em;
}

th {
  background: var(--brand-primary);
  color: white;
  padding: 8px 12px;
  text-align: left;
  font-weight: 600;
}

td {
  padding: 6px 12px;
  border-bottom: 1px solid var(--border);
}

tr:nth-child(even) { background: var(--bg-subtle); }

code {
  background: #edf2f7;
  padding: 2px 6px;
  border-radius: 3px;
  font-size: 0.9em;
}

pre {
  background: #2d3748;
  color: #e2e8f0;
  padding: 1em;
  border-radius: 6px;
  overflow-x: auto;
}

pre code {
  background: none;
  color: inherit;
  padding: 0;
}

blockquote {
  border-left: 4px solid var(--brand-accent);
  margin: 1em 0;
  padding: 0.5em 1em;
  background: var(--bg-subtle);
  color: var(--text-secondary);
}

.header-meta {
  text-align: center;
  color: var(--text-secondary);
  margin-bottom: 2em;
  font-size: 0.95em;
}

@media print {
  body { padding: 15mm; }
  h1 { page-break-before: always; }
  h1:first-of-type { page-break-before: avoid; }
  table { page-break-inside: avoid; }
  @page { margin: 20mm; }
  @page :first { margin-top: 40mm; }
}

/* Page footer for print */
@media print {
  body::after {
    content: "MASS ADAS L3 — Confidential | Generated ${DATE}";
    position: fixed;
    bottom: 10mm;
    left: 0;
    right: 0;
    text-align: center;
    font-size: 8pt;
    color: #a0aec0;
  }
}
CSSEOF

echo "=== Generating DEMO-1 HTML presentation documents ==="

# Helper function to convert markdown to HTML
convert_to_html() {
  local src="$1"
  local dest="$2"
  local title="$3"
  
  if [ -f "$src" ]; then
    pandoc "$src" \
      -o "$dest" \
      --standalone \
      --css=".print-style.css" \
      --metadata title="$title" \
      --metadata date="$DATE" \
      --toc \
      --toc-depth=3 \
      -V lang=zh-CN
    echo "✅ Generated HTML: $(basename "$dest")"
  else
    echo "❌ Source not found: $src"
    return 1
  fi
}

# 1. ConOps v0.1
convert_to_html \
  "$PROJECT_ROOT/docs/Design/ConOps/01-conops-v0.1.md" \
  "$OUT_DIR/ConOps-v0.1.html" \
  "MASS ADAS L3 — Concept of Operations (ConOps) v0.1"

# 2. V&V Plan v0.1
convert_to_html \
  "$PROJECT_ROOT/docs/Design/Phase 1/D1.5-vv-plan-scenario-qual/V&V_Plan/00-vv-strategy-v0.1.md" \
  "$OUT_DIR/VV-Plan-v0.1.html" \
  "MASS ADAS L3 — Verification & Validation Plan v0.1"

# 3. Cert Evidence Tracking
convert_to_html \
  "$PROJECT_ROOT/docs/Design/Cert/cert-evidence-tracking.md" \
  "$OUT_DIR/cert-evidence-tracking.html" \
  "MASS ADAS L3 — Certification Evidence Tracking"

# 4. Simulator Qualification Report
convert_to_html \
  "$PROJECT_ROOT/docs/Design/Phase 1/D1.3-sil-framework/D1.3.1-mmg-simulator/01-simulator-qualification-report.md" \
  "$OUT_DIR/simulator-qualification-report.html" \
  "MASS ADAS L3 — Simulator Qualification Report"

echo ""
echo "=== Converting HTML to professional PDF documents via Playwright ==="

# Helper function to run the node script for HTML-to-PDF conversion
compile_pdf() {
  local html="$1"
  local pdf="$2"
  
  if [ -f "$html" ]; then
    NODE_PATH="$PROJECT_ROOT/web/node_modules" node "$PROJECT_ROOT/tools/sil/html_to_pdf.js" "$html" "$pdf"
  else
    echo "⚠️ HTML file not found, skipping PDF conversion: $html"
  fi
}

compile_pdf "$OUT_DIR/ConOps-v0.1.html" "$OUT_DIR/ConOps-v0.1.pdf"
compile_pdf "$OUT_DIR/VV-Plan-v0.1.html" "$OUT_DIR/VV-Plan-v0.1.pdf"
compile_pdf "$OUT_DIR/cert-evidence-tracking.html" "$OUT_DIR/cert-evidence-tracking.pdf"
compile_pdf "$OUT_DIR/simulator-qualification-report.html" "$OUT_DIR/simulator-qualification-report.pdf"

echo ""
echo "=== Done ==="
echo "Output directory: $OUT_DIR/"
ls -lh "$OUT_DIR"/*.pdf
echo ""
