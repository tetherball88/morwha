# M.O.R.W.H.A — Showcase Image Style Guide & Prompt

Use this prompt and style spec to generate additional showcase cards (for Nexus Mods, GitHub, or release notes) that match `hero_banner.png` while keeping all in-game screenshots **100% authentic and unaltered**.

---

## Reusable Generation Prompt

Copy-paste the prompt below when giving an AI agent a folder of new in-game screenshots:

```text
Create crisp, authentic 2560×1440 (16:9) PNG showcase images from the unedited in-game screenshots in <FOLDER_PATH> using HTML/CSS/SVG rendered via headless Chrome (zero AI alteration of faces or textures).

Match the official M.O.R.W.H.A visual style:
- Canvas: 2560×1440, deep obsidian slate gradient (#0c0f15 -> #090b10 -> #07090d) with a subtle warm antique-gold radial top glow (rgba(196, 154, 78, 0.11)).
- Outer Frame: Double hairline inset border (22px outer + 5px inner) in antique gold (rgba(212, 175, 99, 0.28) and 0.10).
- Typography:
  - Titles / Names / Section Badges: Cinzel (fallback: Palatino Linotype, Book Antiqua, Georgia, serif), uppercase, wide tracking.
  - Main titles use vertical antique-gold text gradient (#f8e6b8 -> #d8b263 -> #a88038) with a centered fading gold hairline rule underneath.
  - Subtitles / Labels / Feature Pills: Inter (fallback: Segoe UI, sans-serif), parchment white (#f4efe6) and muted warm silver (#b7b0a2).
  - Top-right corner watermark: `M.O.R.W.H.A` in Cinzel, rgba(216, 178, 99, 0.72), letter-spacing 4px.
- Portrait Frames & Badges:
  - 1.5px antique gold border (rgba(212, 175, 99, 0.55)), soft dark drop shadow, subtle bottom gradient vignette for legibility.
  - Dark translucent obsidian pills/tags (rgba(10, 12, 17, 0.82)) with 1px gold border (rgba(212, 175, 99, 0.45)) and gold-highlighted keywords (#e8c77b).
- Framing Rules:
  - Keep 100% of real in-game pixels sharp and unedited (no AI redrawing of faces/skin).
  - Never clip horns, cat ears, elven ear tips, or beast tails; adjust object-position and subtle zoom per portrait so faces and silhouettes are centered and balanced.
  - Visually inspect every rendered PNG and refine any tight crops before finalizing.
```

---

## Established Layout Templates

1. **Hero Strip Banner (`hero_banner.png`)**
   - 5 vertical portrait strips (`1fr 1fr 1.16fr 1fr 1fr`) separated by 4px gold hairlines, small top race/type tags, bottom dark scrim, large `M.O.R.W.H.A` serif title, subtitle, and 4 bottom feature pills.
2. **Genetic Inheritance Pedigree Tree**
   - Header + parent portrait cards joined by a gold `+` medallion, connected via crisp SVG gold pedigree brackets (`#d4af63`) with downward arrows to larger offspring cards showing inherited trait subtitles.
3. **Tier-2 Beast Chimera Dual Panel**
   - Side-by-side **Female Hybrid** and **Male Hybrid** full-body panels, each with a top-left sex badge, bottom-left caption, and a gold-bordered square `Face Detail` close-up inset positioned so beast tails remain fully visible, plus 4 technical feature pills along the bottom.

---

## Quick Palette Reference

| Token | Hex / Value | Usage |
| :--- | :--- | :--- |
| Obsidian Base | `#090b10` | Canvas background & dark scrims |
| Antique Gold | `#d4af63` | Borders, pedigree lines, eyebrows |
| Gold Highlight | `#f8e6b8` → `#a88038` | Main serif heading gradient |
| Parchment White | `#f4efe6` | Primary names & subtitles |
| Warm Silver | `#b7b0a2` | Secondary captions & footer notes |
