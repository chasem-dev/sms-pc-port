#!/usr/bin/env python3
"""Local-only design study for a category breakdown progress card."""

import json
from pathlib import Path


REPORT = Path('/home/netflix/sms/build/GMSE01/report.json')
OUTPUT = Path(__file__).with_name('progress-breakdown-concept-v1.svg')

report = json.loads(REPORT.read_text())
total_bytes = int(report['measures']['total_code'])
rows = [
    ('GAME', report['categories'][0]['measures'], 'Game code'),
    ('JSystem', report['categories'][1]['measures'], 'Middleware'),
    ('SDK', report['categories'][2]['measures'], 'Platform code'),
    ('TOTAL', report['measures'], 'All measured code'),
]
metrics = [
    ('FUZZY', 'fuzzy_match_percent', 'fuzzy', '#83F2DF'),
    ('EXACT', 'matched_code_percent', 'exact', '#FFE18A'),
    ('LINKED', 'complete_code_percent', 'linked', '#FFAF91'),
]

svg = ['''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 880" width="1200" height="880" role="img" aria-labelledby="title description">
  <title id="title">GMSE01 matching breakdown across Game, JSystem, SDK, and Total</title>
  <desc id="description">Verified 2026-09-29 from the GMSE01 build report at revision 3370b47b. Each category has separate fuzzy similarity, byte-perfect code, and source-linked code tracks. Fuzzy similarity is an approximate score, not exact byte coverage.</desc>
  <defs>
    <linearGradient id="background" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#0F202C"/><stop offset="1" stop-color="#19333C"/></linearGradient>
    <linearGradient id="fuzzy"><stop stop-color="#30BFD6"/><stop offset="1" stop-color="#8AF4D5"/></linearGradient>
    <linearGradient id="exact"><stop stop-color="#E99A56"/><stop offset="1" stop-color="#FFE589"/></linearGradient>
    <linearGradient id="linked"><stop stop-color="#F07B84"/><stop offset="1" stop-color="#FFC18A"/></linearGradient>
    <pattern id="grid" width="36" height="36" patternUnits="userSpaceOnUse"><path d="M36 0H0V36" fill="none" stroke="#B8ECE8" stroke-opacity=".045"/></pattern>
    <pattern id="ticks" width="10.9" height="13" patternUnits="userSpaceOnUse"><path d="M10.4 0V13" stroke="#102635" stroke-width="1" stroke-opacity=".52"/></pattern>
    <style>
      .sans { font-family: 'DejaVu Sans', Arial, sans-serif; }
      .mono { font-family: 'DejaVu Sans Mono', 'Courier New', monospace; font-weight: 700; letter-spacing: .8px; }
    </style>
  </defs>
  <rect x="1" y="1" width="1198" height="878" rx="25" fill="url(#background)" stroke="#487783" stroke-width="2"/>
  <rect x="2" y="2" width="1196" height="876" rx="24" fill="url(#grid)"/>
  <path d="M34 202H1166" stroke="#81BEBD" stroke-opacity=".3"/>
  <rect x="50" y="37" width="12" height="12" rx="2" fill="#8AEAD8"/>
  <text x="74" y="48" fill="#95D4D3" font-size="14" class="mono">GMSE01 / MATCH PROFILE</text>
  <text x="50" y="110" fill="#FBF7EC" font-size="46" font-weight="800" class="sans" letter-spacing="-1.7">Where the match lives.</text>
  <text x="52" y="147" fill="#B7D1D1" font-size="17" class="sans">Game · JSystem · SDK · measured code total</text>
  <circle cx="1017" cy="92" r="63" fill="none" stroke="#E9BB77" stroke-opacity=".35" stroke-width="2"/>
  <circle cx="1017" cy="92" r="43" fill="none" stroke="#FFD993" stroke-opacity=".52" stroke-width="2"/>
  <circle cx="1017" cy="92" r="20" fill="#FFE0A0" opacity=".9"/>
  <path d="M1017 12v14M1017 158v14M937 92h14M1083 92h14M961 36l10 10M1063 138l10 10M1073 36l-10 10M971 138l-10 10" stroke="#FFD48F" stroke-opacity=".55" stroke-width="2.5" stroke-linecap="round"/>
  <text x="51" y="187" fill="#88ADB1" font-size="12" class="mono">CODE FAMILY / SIZE</text>
  <text x="459" y="187" fill="#88ADB1" font-size="12" class="mono">THREE DISTINCT MEASURES</text>
  <text x="1149" y="187" text-anchor="end" fill="#88ADB1" font-size="12" class="mono">PERCENT</text>''']

for index, (name, data, subtitle) in enumerate(rows):
    y = 220 + index * 152
    is_total = index == 3
    panel = '#1D3941' if is_total else ('#152D38' if index % 2 == 0 else '#18313B')
    stroke = '#79CED0' if is_total else '#315864'
    code_bytes = int(data['total_code'])
    share = code_bytes / total_bytes * 100
    size = f'{code_bytes / 1_000_000:.2f} MB'
    svg.append(f'''
  <rect x="36" y="{y}" width="1128" height="136" rx="18" fill="{panel}" stroke="{stroke}" stroke-width="{'2' if is_total else '1'}"/>
  <rect x="36" y="{y + 20}" width="5" height="96" rx="2.5" fill="{'#A5E8D9' if is_total else '#618B95'}"/>
  <text x="62" y="{y + 43}" fill="#FBF7EC" font-size="{'27' if is_total else '25'}" font-weight="800" class="sans">{name}</text>
  <text x="63" y="{y + 69}" fill="#AFCACD" font-size="14" class="sans">{subtitle}</text>
  <text x="63" y="{y + 107}" fill="#91BEC1" font-size="12" class="mono">{size}  ·  {share:.1f}% OF CODE</text>''')
    for metric_index, (label, key, gradient, color) in enumerate(metrics):
        bar_y = y + 25 + metric_index * 37
        value = float(data[key])
        bar_width = 545 * value / 100
        svg.append(f'''
  <circle cx="348" cy="{bar_y + 6.5}" r="4" fill="{color}"/>
  <text x="365" y="{bar_y + 11}" fill="#CEE3E4" font-size="12" class="mono">{label}</text>
  <rect x="460" y="{bar_y}" width="545" height="13" rx="6.5" fill="#28434C"/>
  <rect x="460" y="{bar_y}" width="{bar_width:.2f}" height="13" rx="6.5" fill="url(#{gradient})"/>
  <rect x="460" y="{bar_y}" width="545" height="13" fill="url(#ticks)"/>
  <path d="M{460 + bar_width:.2f} {bar_y - 3}v19" stroke="{color}" stroke-width="2" opacity=".9"/>
  <text x="1135" y="{bar_y + 12}" text-anchor="end" fill="{color}" font-size="17" class="mono">{value:.2f}%</text>''')

svg.append('''
  <text x="51" y="855" fill="#A5C3C4" font-size="12" class="sans">Fuzzy is approximate similarity; exact and linked are shares of code bytes.</text>
  <text x="1149" y="855" text-anchor="end" fill="#8FAFB2" font-size="11" class="mono">2026-09-29  ·  3370b47b  ·  CONCEPT V1</text>
</svg>
''')
OUTPUT.write_text(''.join(svg))
print(OUTPUT)
