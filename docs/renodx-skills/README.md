# RenoDX workflow notes (reference)

Agent "skills" from the RenoDX project (https://github.com/clshortfuse/renodx, MIT), shared with
this fork as reference material for the HDR work. They describe RenoDX's HLSL/ReShade workflow;
here they are used for the method, ported to bbport's GLSL renderer.

| Folder | Use in this fork |
|---|---|
| `handle-sdr-tonemap-lut` | HDR step 3: replacing Bloodborne's tonemap while keeping the vanilla SDR look |
| `hdr-test-pattern-generation` | Test patterns to check the HDR10/PQ output |
| `bt2020-png-generation` | BT.2020 PQ PNGs for comparisons |
| `analysis-graphing` | Plots of tone curves (vanilla vs HDR) |
| `setup-game-mod-dev` | The ReShade + RenoDX addon route (not used: HDR is native here) |
