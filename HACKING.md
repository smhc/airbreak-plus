HACKING.md


# How to debug 

1. Open the binary in Ghidra, setting the architecture to ARM Cortex little-endian

2. Open it in debugger view

3. In the Window->Memory Map tool, create maps for the flash, its mirror, and ram based on datasheet - https://github.com/NationalSecurityAgency/ghidra/issues/2578#issuecomment-749561510

4. Analyse code

5. Install gdb-multiarch, openocd, and connect to the running device as normal

6. In Ghidra, create a new debugger(gdb-multiarch) connection in "Debugger Targets" pane

7. target remote localhost:3333

8. Click Record on the process in inferiors in Objects panel/tab, arch: ARM Cortex little

9. Resume execution

10. In the Modules panel, click the button "Map the current trace to blabla".

11. Now you can set breakpoints, watchpoints, and whenever they're hit, Ghidra will show you where in the binary it happened
