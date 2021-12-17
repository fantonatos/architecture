# Caches and Branch Predictors

## [cache/](cache/)

Simulation of [direct access](cache/cache-sim.c#L307), [set associative](cache/cache-sim.c#L104), and [fully associative](cache/cache-sim.c#L152) caches with write-on-miss and next-line-prefetch features.

Direct access is implemented as 1-way set associative cache and use the same code.

### Tracefile Format
```
S 0x0022f5b4
S 0x0022f7d4
L 0x006328dc
L 0x7703d9c0
S 0x0022f4c0
```

## [predictors/](predictors/)
 
Simulation of various branch prediction algorithms ([always take](predictors/predictors.c#L64), [never take](predictors/predictors.c#L64), [bimodal](predictors/predictors.c#L81), [gshare](predictors/predictors.c#L128), [tournament](predictors/predictors.c#L154)).

### Tracefile Format
```
0x7f4072aa223f NT 0x7f4072aa2280
0x7f4072aa224c NT 0x7f4072aa2280
0x7f4072aa225d T 0x7f4072aa2266
0x408839 NT 0x408e1c
0x40859b NT 0x408730
0x4085c1 T 0x4085cc
```

Both programs are multithreaded and fast. They simulate their respective hardware on the input provided from tracefiles. Tracefiles should be using UNIX line endings.