# 0.3 test 02: side-by-side comparison (original | Nitid)

**Question:** does the compare view show exactly the original on one side and exactly Nitid's picture on the other,
lined up, at no cost?

**Run:** `tests/v0.3/02-compare/run.sh`. Output: [output-2026-11-26.md](output-2026-11-26.md).

## Result: ✅

- **Exact:** with the line in the middle (540p in a 1920x1080 window), the right half of the window is
  bit-identical to the right half of Nitid's normal picture (VSR HIGH), and the left half bit-identical to the
  plainly scaled video (PSNR inf for both); the left half differs from Nitid's picture (38.9 dB), as it should.
- **Lined up:** the two halves are the same part of the same picture, so an object crossing the line continues
  across it ([compare-vsr.jpg](compare-vsr.jpg), [compare-vsr-crop.jpg](compare-vsr-crop.jpg); the test images
  are taken with the on-screen controls off, so the line and the labels, drawn by the UI, are not in them).
  With frame generation on, the left side shows the nearest real frame.
- **Cost:** none measurable (one extra copy of the source-size frame per real frame and a second blit); runs with
  compare on had 0–2 missed refreshes when the desktop was calm.

## Use

`X` or Settings → View → "Compare: original | Nitid"; drag on the picture to move the line; `--compare`,
`--compare-split 0..1`. Labels "Original" / "Nitid" sit at the top of the line.

## How it works

The pipeline keeps a copy of each decoded frame (source size) next to the enhanced one while compare is on; the
render-ahead pool carries it; the presenter blits the left part of the original and the right part of Nitid's
frame onto the same picture rectangle. The split is not a setting (moving it doesn't reconfigure anything);
`Stats::video_x/y/w/h` tell the app where the picture is, for the line and the dragging.

## Findings

- **The desktop was irregular during this test** (165 Hz display measured at up to 177 fps, 87–1045 missed
  refreshes with compare on *or* off). The 0.2.0 release on the same desktop at the same time also had 174 missed
  refreshes in one of two runs, so this is the compositor, not Nitid. Missed-refresh counts from this session are
  only compared within back-to-back runs.
