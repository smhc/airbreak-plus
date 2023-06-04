# TODO

See `WorkingDocs/Projects/MyASV.md`

Breakpoint not_called_during_cycle_either, check what param_1 is 

0x20010eac - param_1. IDK what lives here

## Big 

 - ASV mode
  - Write py_analysis code to analyse viability of different algorithms
    - Load-like target TV,
 - Breath-following EPS

## Other

 - Repurpose GUI options to enable configuration
 - Halt machine, dump entire memory, search for Identification.tgt file contents, do this twice, if identical addresses, watch where they're written from.
 - Analyse where fvars[0x20] is written from
 - Move jitter code elsewhere(graph/runs_during_operation_and_cooldown), so graphing works with other modes
 - Try converting rise to working on interpolation basis.

## Unlikely

 - Figure out how SD card code works, load code from the SD card


# Useful notes

`watch *0x2000e956`
On therapy start: `DMA_SetCurrDataCounter(addr, value)` - This sets 0xE fvar


https://stackoverflow.com/questions/5892104/how-were-the-weightings-in-the-linux-load-computation-chosen


Py:avg slope of flow until peak pres 
Resmed algo:
 - Target: 90-95% of weighted recent MV (6min contribute 86%)
 - Change: ∆IPS = `gain * (target_mv - mv) * time`, where `gain=0.3`
 - Calculation done every 0.02s (50Hz)
 - Better: Faster IPS drop when IPS is stable(>90s) but above minimum by progressively reducing time window(3->1m), limit of ∆IPS, 

`def interp(from, to, prop, exp=1.0): ( from**exp + to**exp) **1/exp`

Check how well outflow correlates to residual volume - `flow/test`, and pressure change-flow correlation.

Track recent peak exhale, peak inhale. 1/4 of IPS follows breathing, all of EPS=EPAP/4-0.5 follows, targets 90% recent ExFlow

py_analysis:
  - Per-breath analysis
  - (Premature trigger | start in rise of flow) vs (pressure at trigger | pressure gradient | TE)
  - Error signal using vs not using pre-trigger volume, at 50ms checkpoints
    - Error signal relative to total volume


RM.ASV - 2022.09.05 2
