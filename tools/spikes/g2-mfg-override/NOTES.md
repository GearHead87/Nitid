# G2: Does the Multi Frame Gen 4x override work? Which Proton builds?

- **Result:** ⏳ run by the user, result not reported yet
- **Date:** 2026-11-23
- **Driver:** 620.73.04
- **Game:** Cyberpunk 2077
- **How to run:** G1 launch options plus
  `PROTON_ENABLE_NGX_UPDATER=1 DXVK_NVAPI_DRS_NGX_DLSS_FG_OVERRIDE=on DXVK_NVAPI_DRS_NGX_DLSSG_MULTI_FRAME_COUNT=3`
  (`3` generated frames = 4x, `2` = 3x)
- **Pass if:** the indicator shows 4x.

## Report (fill in)

| Proton build | dxvk-nvapi | 4x shown? | FPS | Notes |
|---|---|---|---|---|
| Proton Experimental | 0.9.3 | | | |
| GE-Proton11-12 | 0.9.3 | | | |
| cachyos-11.0-20261015-slr | 0.9.2 | | | |
| Proton 11.0 | 0.9.2 | | | |

Afterwards clear the saved indicator once with `DXVK_NVAPI_SET_NGX_DEBUG_OPTIONS=DLSSIndicator=0,DLSSGIndicator=0`.
