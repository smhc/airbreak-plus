# Documentation

gdb: backtrace / up / down

## HOWTO

sudo openocd -f interface/stlink-v2.cfg -f 'tcl/airsense.cfg'
telnet localhost 4444

### TODO:

 0. Check if 

 1. Check if mem2array stops execution. If YES, abort.
 2. Write `dump_ivars` and `dump_state`
 3. Identify maximal ivars, fvars, state offsets used
 4. Continuously write ivars/fvars/state to CSV file, together with real timestamps
 5. Write Python script to record normal operation and backup rate engagement

## Overview info

**Memory addresses**:
 - state: 0x20001f00 (size: impossible to tell, maybe 4121 bytes)
    - might be `557*4 = 2228` bytes. After that it's just zeroes
 - ivars: 0x2000e750 (size: 126 (504b))
 - fvars: 0x2000e948 (size: 223 (892b))
 - There's 126 values(504 bytes) from ivars to fvars
 - 

**Modes**
```
 1. 1 = CPAP
 2. 2 = AutoSet
 3. 4 = (? APAP)
 4. 8 = S
 5. 16 = (? ST)
 6. 32 = (? T)
 7, 64 = VAuto
 8, 128 = ASV
 9, 256 = ASVAuto
10, 512 = (? iVAPS)
10,1024 = (? PAC)
11,2048 = (? AutoSet for Her)

49 00 = 0x01001001 = CPAP, VAuto, S
81 01 = 0x10000001 = CPAP, ASV, ASVAuto
Autoset = 03 00, For Her = 03 08

From patch-airsense script:
add more mode entries, set config 0x0 mask to all bits high
default is 0x3, which only enables mode 1 (CPAP) and 2 (AutoSet)
```

## Pressure computation funcs:

pressure_only_in_mode_2_and_3

## State

```
Base address: 0x20001f00

high_pressure -  0, 0  - 0x0  // !!! Probably different in original FW !!!
low_pressure  -  1, 4  - 0x4  // !!! Probably different in original FW !!!
high_time     -  2, 8  - 0x8  // !!! Probably different in original FW !!!
low_time      -  3, 12 - 0xc  // !!! Probably different in original FW !!!
smoothing     -  4, 16 - 0x10
magic         -  5, 20 - 0x14
last_change   -  6, 24 - 0x18
state         -  7, 28 - 0x1c (used by graph.c as timestamp(?))
sample        -  8, 32 - 0x20
target        -  9, 36 - 0x24 (it's a float)
last_value    - 10, 40 - 0x28

char          -     45 - 0x2d 
char          -     48 - 0x30 - if it's "\0", target is set to 0

27, 0x6c - "flow_write_target_cpap_mode" writes something to this 
state+8 - char, set to 1 in modes AutoSet/APAP/ASV(?Auto) by FUN_000b6b86, 0 otherwise - I assume this is automatic pressure adjusting

```

## ivars

```
ivars address - 0x2000e750

ivars[0x1b] - switched on in compute_pressure_stuff_replaced_with_breath (probably breath stage?)
ivars[0x6f] - "an active therapy mode" (is 0 when not running)
```
## fvars

```
fvars address - 0x2000e948

0x0  - patient flow
0x3  - raw flow (almost always positive)
0x25 - leak-compensated flow
0x26 - appears to be slightly delayed 0x25

// Ignore the below:

fvars[0x02] = actual current pressure ?
fvars[0x09] = AutoSet target high
fvars[0x0a] = AutoSet target low
fvars[0x2d] = smooth_target; / commanded_pressure
fvars[0x0d] = max pressure difference
```

## Unidentified

NOOP these to disable, respectively, ASV, and ASVAuto setting Max PS to a minimum of minPS+5
000ab524 b0 ee 60 0a     vmov.mi.   s0,s1
000ab5d4 b0 ee 60 0a     vmov.mi.   s0,s1

### ivars
### fvars
0xcf - ASV/ASVAuto PS (?)
0xce - ASV/ASVAuto something (EPAP?)
tm_mode_of_some_sort - could be responsible for backuprate in modes 9 and 5
110-2 - maybe dynamic ASV PS ?
196-2 - maybe dynamic ASV target  ?

### Memory to dump:

fvars:
    0-4
    34-42 (skip 40)
    53-55
    61-75
    80-86
    gap of only 2

    89-100 (+3)
      89-91
      93-94
      96-97
      99-100
    104-114 (+2)
      skip 105, 112
    118-124 (+1)
      skip 123
    129-140 (+1)
      skip 133
    143
    166
    184-187
    190-196
    206-212 (+2)
      skip 209, 211
    220-222
ivars:
    

fvars: SAVED: 115 reads / 223
state: SAVED: 560 reads / 560
ivars: SAVED: 100 reads / 126

# Proper stuff

0xE - S IPAP ?
0xF - S EPAP ?

0x28 - EPAP (target)
0x29 - PS
0x2A - EPAP+PS - the real target
0x2D - EPAP (current)

0x20 - counts up since breath start, stops at 0.50 for a while, after which inhale ends

pressure_work - seems like this is setting PS with EasyBreathe
  Based on 0x90 variable. Seems like the pressures here are +1 relative to displayed values
pressure_check_something - one of two options from pressure_work ?
pressure_only_in_mode_2_and_3
tm_write_fvars_29_lookup_table - also sets PS, based on a loopup table?

target remote localhost:3333

EasyBreathe:
  0x90 is written at pressure_check_something at 0x080bc2e4, with results of pressure_scale_not_called
SquareWave:
  0x90 unused
  not_called_during_cycle:29


c1: immediately
default: also
c2: at peak 
c3: also at peak?


not_called_during_loop:18 - I think this is the down-slope of squarewave
if (FLOAT_080b0650 <= *local_14) {

TODO: Try replacing with a call to pressure_scale_not_called

-------

# How to debug 

-2. Open the binary in Ghidra, setting the architecture to ARM Cortex little-endian

-1.5. Open it in debugger view

-1. In the Window->Memory Map, create maps for the flash, its mirror, and ram based on datasheet - https://github.com/NationalSecurityAgency/ghidra/issues/2578#issuecomment-749561510

-0.5. Analyse

0. Create a new debugger(gdb-multiarch) connection in "Debugger Targets" pane

1. target remote localhost:3333

2. Click Record on the process in inferiors in Objects panel/tab, arch: ARM Cortex little

3. Resume execution

4. In modules click the button "Map the current trace toblabla"

### Runs always
 - runs_during_operation_and_cooldown - 0x0808291c
 - therapy_variable_get - 0x080a0840, therapy_variable_get_float


### Do not run during ASV:
 - compute_pressure_stuff_replaced_with_breath - used by CPAP, APAP, S, VAuto, but not ASV
 - pressure_target_normal_operation - 0x080bbcec

### Ugh idk:

HIT IN S: pressure_only_in_mode_2_and_3

tm_mode_of_some_sor - hit in CPAP, S, ASV, presumable every mode

Probably irrelevant:
  compute_pressure_stuff_replaced_with_breath - not where target is calculated
  pressure_only_in_mode_2_and_3_once - runs once only
Irrelevant (does not run in S):
  flow_computation_modes_maybe - does not run in S mode
  flow_computation_methods
  pressure_functions
  pressure_computation_method

compute_pressure_stuff_replaced_with_breath:
  In S mode:
    case 0: immediately
    case default: mid-inhale-start

  In VAuto mode, during ramp ONLY:
    case 1: 

  Doesn't seem to be hit:
    pressure_target_normal_operation
    case 3,4,5


tm_mode_of_some_sort 

*local_48 - in compute_pressure is 0x2d

UndefinedFunction_080bb1a2 - hits 0x2d in ASV





// ivars:
// 27/0x1b - switched on in compute_pressure_stuff_replaced_with_breath

// State:
// high_pressure -  0, 0  - 0x0
// low_pressure  -  1, 4  - 0x4
// high_time     -  2, 8  - 0x8
// low_time      -  3, 12 - 0xc
// smoothing     -  4, 16 - 0x10
// magic         -  5, 20 - 0x14
// last_change   -  6, 24 - 0x18
// state         -  7, 28 - 0x1c
// sample        -  8, 32 - 0x20
// target        -  9, 36 - 0x24 (it's a float)
// last_value    - 10, 40 - 0x28

// char          -     45 - 0x2d 
// char          -     48 - 0x30 - compared to "\0"

// Different states: 0=none, 1=start of inhale, 5=end of inhale, 6=inhale timeout. Each signal lasts 5 ticks(200ms)



fvars[0xbe] - last breath's TV

b8-c3 change on breath end

35, 36 are also somehow related (FY, FZ), be (LF)

b9 (LA) - also somehow related but usually zero

gui_fill_rect_set_colors:
  Colors:
  DAT_08067e3c - #8bc53f (green) OR #3fc58b (63,197,139)  (cyan-green?)
  DAT_08067e40 -  #323232 (50,50,50)- dark gray

  #A4B30F (164,179,15)  - shitty yellow, OR  #0FB3A4 - ocean blue
Colors in 

080dde38 writes fvars 0xe S_IPAP

GUI_DispString - used to display the "Therapy" header, and others I presume