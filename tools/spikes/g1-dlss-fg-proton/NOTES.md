# G1: Do DLSS upscaling + Frame Gen 2x work in a Proton game?

- **Result:** ⏳ run by the user, result not reported yet
- **Date:** 2026-11-23
- **Driver / Proton:** 620.73.04 / Proton Experimental (dxvk-nvapi 0.9.3)
- **Game:** Cyberpunk 2077
- **How to run:** Steam launch options:
  `DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=1024,DLSSGIndicator=2 mangohud %command%`
  then turn on DLSS + Frame Generation in the game's graphics menu.
- **Pass if:** the DLSS indicator appears, DLSS-G shows as active, MangoHud FPS roughly doubles.

## Report (fill in)

- Indicator shown (SR / FG)?
- FPS without FG → with FG:
- Problems:

## What this means for PLAN.md

If this fails, fix driver/Proton before trusting S1–S3.

Afterwards clear the saved indicator once with `DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=0,DLSSGIndicator=0`.
