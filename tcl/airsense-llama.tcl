# tcl/airsense-llama.tcl
# Include it in `airsense.cfg`

global addr_ivars; set addr_ivars 0x2000e750
global addr_fvars; set addr_fvars 0x2000e948
global addr_state; set addr_state 0x20001f00

proc get_memory_all { {fname ""} {numsamples 1} {delay 25} } {
  if {$fname == ""} {
    set channel stdout 
  } else { set channel [open $fname a] }

  set buffer ""
  set timer_start [clock milliseconds]
  for {set j 0} {$j < $numsamples} {incr j} {
    if {$fname != ""} { puts -nonewline stdout "." }
    append buffer [clock milliseconds] " " [read_memory 0x2000e750 32 349] "\n"
    set timer [expr {($delay * ($j+1)) - ([clock milliseconds] - $timer_start)}]
    if {$timer > 0} { after $timer }
  }

  if {$fname != ""} { puts stdout "done" }
  puts $channel $buffer

  if {$channel != "stdout"} { flush $channel; close $channel }
}

proc old_get_memory {which {channel "stdout"} {toffset 0} {tcount 0}} {
  global addr_ivars; global addr_fvars; global addr_state;
  if {$which == "all"} {
    old_get_memory ivars $channel; old_get_memory fvars $channel;
  } else {
    if {$which == "ivars"} { set addr [expr {$addr_ivars + $toffset}]; set width 32; set count 126; }
    if {$which == "fvars"} { set addr [expr {$addr_fvars + $toffset}]; set width 32; set count 223; }
    if {$tcount != 0 } { set count $tcount; }

    set buffer ""
    if {$which == "fvars"} {
      append buffer [clock milliseconds] " " $which " "
      foreach x [read_memory $addr $width $count] {
        binary scan [binary format i $x] f var0;
        append buffer $var0 " "
      }
    } elseif {$which == "ivars"} {
      append buffer [clock milliseconds] " " $which " "
      foreach x [read_memory $addr $width $count] {
        binary scan [binary format i $x] i var0;
        append buffer $var0 " "
      }
    } else {
      append buffer [clock milliseconds] " " $which " " [read_memory $addr $width $count]
    }
    puts $channel $buffer
  }
}

proc dvars {{fname ""}} {
  # if {$fname == ""} {
  #   set channel stdout 
  # } else { set channel [open $fname a] }
  get_memory_all $fname 1 100
  # if {$channel != "stdout"} { flush $channel; close $channel }
}

proc old_get_memory_repeat {which {fname ""} {numsamples 1} {delay 25}} {
  global addr_ivars; global addr_fvars; global addr_state;

  if {$fname == ""} {
    set channel stdout 
  } else {
    set channel [open $fname a]
  }
  for {set j 0} {$j < $numsamples} {incr j} {
    #set timer [clock milliseconds]
    get_memory $which $channel
    puts $channel ""
    #set timer [expr {[clock milliseconds] - $timer}]
    #after [expr { $delay - $timer } ]
    after $delay
  }

  if {$channel != "stdout"} { flush $channel }
}

proc reload { } { source [find tcl/airsense-llama.tcl]; }

# format: 
# TIMESTAMP ivars [list]
# TIMESTAMP fvars [list]
# TIMESTAMP state [list]

# Works with big-endian:
# binary scan $val($i) f n 
# append buffer $n 

# DEPRECATED mem2array version:

# mem2array values $width $addr $count
# set buffer ""
# append buffer [clock milliseconds] " " $which_table " "
# for {set i 0} {$i < $count} {incr i} {
#   #binary scan $values($i) h* val_hex
#   # append buffer $val_hex " "
#   append buffer [format "%08x" $values($i)] " "
# }
# puts $channel $buffer