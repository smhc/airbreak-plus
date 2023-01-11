#!/usr/bin/env tclsh
proc translate_file {filename} {
  # Read input data
  set f_in [open $filename r]
  set data [read $f_in]
  close $f_in
  set lines [split $data \n]
  # Open output file
  set f_out [open "$filename\.parsed" w]

  foreach line $lines {
    set i 0
    set buffer ""
    foreach number $line {
      if {$i == 0} {
        append buffer $number " "
      } elseif {$i <= 126} {
        binary scan [binary format i $number] i var0;
        append buffer $var0 " "
      } else {
        binary scan [binary format i $number] f var0;
        append buffer $var0 " "
      }
      set i [expr {$i + 1}]
    }
    puts $f_out $buffer
  }

  flush $f_out
  close $f_out
}

if { $argc != 1 } {
  puts "Input filename argument necessary. Try again"
} else {
  translate_file [lindex $argv 0]
}