"""Convert Markdown reports to PDF (Markdown -> HTML -> headless Chrome).

Usage: /tmp/mdvenv/bin/python md2pdf.py FILE.md [FILE.md ...]   (writes FILE.pdf next to each)
"""
import html
import pathlib
import subprocess
import sys
import tempfile

import markdown

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
CSS = """
@page { size: A4; margin: 16mm 14mm; }
body { font-family: -apple-system, "Helvetica Neue", Arial, sans-serif; font-size: 10pt; line-height: 1.4; color: #111; }
h1 { font-size: 17pt; border-bottom: 2px solid #333; padding-bottom: 4px; }
h2 { font-size: 13.5pt; margin-top: 18px; border-bottom: 1px solid #bbb; padding-bottom: 2px; }
h3 { font-size: 11.5pt; margin-top: 14px; }
table { border-collapse: collapse; margin: 8px 0; font-size: 8.6pt; width: 100%; page-break-inside: avoid; }
th, td { border: 1px solid #999; padding: 3px 5px; vertical-align: top; }
th { background: #e8e8e8; }
tr:nth-child(even) td { background: #f7f7f7; }
code { font-family: Menlo, Consolas, monospace; font-size: 8.6pt; background: #f1f1f1; padding: 0 2px; }
pre { background: #f4f4f4; border: 1px solid #ddd; padding: 6px; font-size: 8.2pt; white-space: pre-wrap; }
pre code { background: none; padding: 0; }
.footer { margin-top: 24px; font-size: 8pt; color: #666; border-top: 1px solid #ccc; padding-top: 4px; }
"""


def convert(md_path: pathlib.Path) -> pathlib.Path:
    # "\|" escapes a pipe inside GitHub table cells; Python-Markdown would print the backslash.
    text = md_path.read_text(encoding="utf-8").replace("\\|", "|")
    body = markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])
    page = (f"<!doctype html><html><head><meta charset='utf-8'><title>{html.escape(md_path.stem)}</title>"
            f"<style>{CSS}</style></head><body>{body}"
            f"<div class='footer'>Source: {html.escape(md_path.name)}</div></body></html>")
    pdf = md_path.with_suffix(".pdf")
    with tempfile.NamedTemporaryFile("w", suffix=".html", delete=False, encoding="utf-8") as f:
        f.write(page)
        tmp = f.name
    subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--no-pdf-header-footer",
                    f"--print-to-pdf={pdf}", f"file://{tmp}"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    pathlib.Path(tmp).unlink()
    return pdf


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        out = convert(pathlib.Path(arg))
        print(f"{out}  ({out.stat().st_size // 1024} KB)")
