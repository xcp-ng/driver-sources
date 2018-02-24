set debugfs_list "bus idle_chk mcp_trace grc phy reg_fifo protection_override igu_fifo fw_asserts"
foreach val $debugfs_list { dict set target_files $val ""}
foreach val $debugfs_list { dict set target_fps $val ""}
set old_prompt 0
set old_prompt_exists 0

# proc for checking if we are in diag
proc is_diag {} {
        if {[info exist ::current(BUS)]} {
                set bus [expr $::current(BUS)]
                if {$bus != ""} {
                         return 1
                }
        }
}

# proc for setting tcl prompt to the target
proc debugfs_prompt {} {
	global target_files
	puts -nonewline [dict get $target_files "bus"]
	puts -nonewline "> "
}

# proc for setting the device to be configured for debugbus recording
proc set_target {prompt} {
	global target_files
	global target_fps
	global tcl_prompt1
	global old_prompt
	global old_prompt_exists

	dict for {key val} $target_files {
		dict set target_files $key "${prompt}/$key"
	}
	if {[info exist tcl_prompt1]} {
		set old_prompt_exists 1
		set old_prompt $tcl_prompt1
	}
	set tcl_prompt1 debugfs_prompt
}

# proc for setting the device to which script addresses commands
proc set_bdf {bdf} {
        set_target "/sys/kernel/debug/qed/${bdf}"
}

# proc for setting a target file for debugfs commands to be recorded to
proc set_file {filename} {
	global target_files
        dict for {key val} $target_files {
                dict set target_files $key "${filename}_${key}"
        }
}

#proc for restoring old tcl prompt if there was one
proc restore_prompt {} {
	global tcl_prompt1
	global old_prompt
	global old_prompt_exists
	if {[expr $old_prompt_exists == 1]} {
		set tcl_prompt1 $old_prompt
		flush stdout
	}
}

#proc for opening the file set by target_file. Make sure target_file was set.
proc open_target_file {mode} {
	global target_files
	global target_fps
	if ![string compare $target_files ""] {
		puts "target file not set. Please use set_file or set_pf"
		return 0
	} else {
		dict set target_fps $mode [open [dict get $target_files "$mode"] "w"]
                return 1
	}
}

# If in diag rename device to diagdevice
if {[is_diag] == 1} {
        rename device diagdevice
}

# Proc device calls proc dev
proc  device { {dev_num ""} {arg1 ""} {arg2 ""} } {
        dev $dev_num $arg1 $arg2
}

# Call diag's 'device' tcl extension and learn the new bdf.
# Used only in diag when user is changing device (and also on startup)
proc dev { {dev_num ""} {arg1 ""} {arg2 ""}} {

		if {$dev_num eq ""} {
			diagdevice
		} elseif {$arg1 eq ""} {
			diagdevice $dev_num
		} elseif {$arg2 eq ""} {
			diagdevice $dev_num $arg1
		} else {
			diagdevice $dev_num $arg1 $arg2
		}
		
        set bus [format %02x [expr $::current(BUS)]]
        set pci [format %01x [expr $::current(PCI_FUNC)]]
        set bdf "${bus}:00.${pci}"
        set_bdf $bdf
        restore_prompt
}

# create phy proc
# the proc receives phy feature and returns the result
proc phy { args } {
		global target_files

                set no_args [llength $args]
                
                if {$no_args > 0} {
                    set command [lindex $args 0]
                }

                if {$no_args > 1} {
                    set port [lindex $args 1]
                }

                # validate the command
                if {($no_args == 0) || ($command != "raw_read") && ($command != "raw_write") &&
                    ($command != "core_read") && ($command != "core_write") &&
                    ($command != "info") && ($command != "mac_stat")} {

                     puts "phy operations:"
                     puts "phy info"
                     puts "phy mac_stat <port>"
                     puts "phy raw_read <port> <lane> <reg>"
                     puts "phy raw_write <port> <lane> <reg> <data low>"
                     puts "phy core_read <port> <reg>"
                     puts "phy core_write <port> <reg> <data low> <data high>"
                     puts "              Port registers: 0x200-0x209 (port type)"
                     puts "              Port registers: 0x20A-0x22B (general type)"
                     puts "              MAC  registers: 0x600-0x62D"
                     return
                 }

                 if {$command == "raw_read"} {
                     if {$no_args != 4} {
                         puts "Wrong #args: phy raw_read <port> <lane> <addr>"
                         return
                     } else {
                         set lane [lindex $args 2]
                         set addr [lindex $args 3]
                     }

                     # instruct driver to obtain phy feature via debugfs
                     exec echo "$command $port $lane $addr" > [dict get $target_files "phy"]
                 }

                 if {$command == "raw_write"} {
                     if {$no_args != 5} {
                         puts "Wrong #args: phy raw_write <port> <lane> <addr> <data low>"
                         return
                     } else {
                         set lane [lindex $args 2]
                         set addr [lindex $args 3]
                         set data_lo [lindex $args 4]
                         set data_hi 0 

                     }

                     # instruct driver to obtain phy feature via debugfs
                     exec echo "$command $port $lane $addr $data_lo $data_hi" > [dict get $target_files "phy"]
                 }

                 if {$command == "core_read"} {
                     if {$no_args != 3} {
                         puts "Wrong #args: phy core_read <port> <reg>"
                         return
                     } else {
                         set reg [lindex $args 2]
                     }
                     
                     # instruct driver to obtain phy feature via debugfs
                     exec echo "$command $port $reg" > [dict get $target_files "phy"]
                 }

                 if {$command == "core_write"} {
                     if {$no_args != 5} {
                         puts "Wrong #args: phy core_write <port> <reg> <data low> <data high>"
                         return
                     } else {
                         set reg [lindex $args 2]
                         set data_lo [lindex $args 3]
                         set data_hi [lindex $args 4]
                     }
                     
                     # instruct driver to obtain phy feature via debugfs
                     exec echo "$command $port $reg $data_lo $data_hi" > [dict get $target_files "phy"]
                 }

                 if {$command == "mac_stat"} {
                     if {$no_args != 2} {
                         puts "Wrong #args: phy mac_stat <port>"
                         return
                     } 
                     # instruct driver to obtain phy feature via debugfs
                     exec echo "$command $port" > [dict get $target_files "phy"]
                 }

                 if {$command == "info"} {
                     # instruct driver to obtain phy feature via debugfs
                     exec echo "$command" > [dict get $target_files "phy"]
                 }
                
		 # display phy feature
		 set res [exec cat [dict get $target_files "phy"]]
		 return $res

}

if {[is_diag] == 1} {
	rename extphy diag_extphy
} else {
	proc diag_extphy {param} {
		puts "only supported on diag"
	}
}

# create extphy proc
# the proc receives extphy feature and returns the result
proc extphy { args } {
	global target_files

	set no_args [llength $args]

	if {$no_args > 0} {
		set command [lindex $args 0]
	}

	# validate the command
	if {($no_args == 0) || ($command != "read") && ($command != "write") &&
	    ($command != "fw_version") && ($command != "fw_upgrade")} {
		puts "extphy operations:"
		puts "extphy fw_upgrade <filename>"
		puts "extphy fw_version"
		puts "extphy read <port> <devad> <register>"
		puts "extphy write <port> <devad> <register> <value>"
		return
	}

	if {$command == "write"} {
		if {$no_args != 5} {
			puts "Wrong #args: extphy read <port> <devad> <register> <value>"
			return
		} else {
			set port [lindex $args 1]
			set devad [lindex $args 2]
			set reg [lindex $args 3]
			set val [lindex $args 4]
		}

		# instruct driver to obtain extphy feature via debugfs
		exec echo "extphy_$command $port $devad $reg $val" > [dict get $target_files "phy"]
		# display phy feature
		set res [exec cat [dict get $target_files "phy"]]
		return $res
	}
		 
	if {$command == "read"} {
		if {$no_args != 4} {
			puts "Wrong #args: extphy read <port> <devad> <register>"
			return
		} else {
			set port [lindex $args 1]
			set devad [lindex $args 2]
			set reg [lindex $args 3]
		}

		# instruct driver to obtain extphy feature via debugfs
		exec echo "extphy_$command $port $devad $reg" > [dict get $target_files "phy"]
		# display phy feature
		set res [exec cat [dict get $target_files "phy"]]
		return $res
	}
		
	if {$command == "fw_version"} {
		return [diag_extphy fw_version]
	}
		
	if {$command == "fw_upgrade"} {
		if {$no_args != 2} {
			puts "Wrong #args: extphy fw_upgrade <filename>"
			return
		}
		set filename [lindex $args 1]
		return [diag_extphy fw_upgrade $filename]
	}
}

# create sfp proc
# the proc receives phy feature and returns the result
proc sfp { args } {
                 global target_files

                 set no_args [llength $args]

                 if {$no_args > 0} {
			  set command [lindex $args 0]
                 }

                 # validate the command
                 if {($no_args == 0) || (($command != "read") && ($command != "write") &&
			  ($command != "decode") && ($command != "get") && ($command != "set"))} {

			  puts "Performs sfp operations:"
			  puts "sfp read <port> <I2C_addr> <offset> <size>"
			  puts "sfp write <port> <I2C_addr> <offset> <size> <value>"
			  puts "sfp decode <port>"
			  puts "sfp get inserted <port>"
			  puts "sfp get tx_disable <port>"
			  puts "sfp get tx_reset <port>"
			  puts "sfp get rx_los <port>"
			  puts "sfp get eeprom <port>"
			  puts "sfp set tx_disable <port> <value>"
			  return
                 }

		  if {$command == "read"} {
		      if {$no_args != 5} {
			  puts "Wrong #args: sfp read <port> <I2C_addr> <offset> <size>"
			  return
		      }
		  }

		  if {$command == "write"} {
		      if {$no_args != 6} {
			  puts "Wrong #args: sfp write <port> <I2C_addr> <offset> <size> <value>"
			  return
		      }
		  }

		  if {$command == "decode"} {
		      if {$no_args != 2} {
			  puts "Wrong #args: sfp decode <port>"
			  return
		      }
		  }

		  if {$command == "get"} {
		      if {$no_args != 3} {
			  puts "Wrong #args: sfp get <type> <port>"
			  return
                     } else {
			  set type [lindex $args 1]
			  if {($type == "inserted")} {
				  set args [lreplace $args 0 1 get_inserted]
			  } elseif {($type == "tx_disable")} {
				  set args [lreplace $args 0 1 get_txdisable]
			  } elseif {($type == "tx_reset")} {
				  set args [lreplace $args 0 1 get_txreset]
			  } elseif {($type == "rx_los")} {
				  set args [lreplace $args 0 1 get_rxlos]
			  } elseif {($type == "eeprom")} {
				  set args [lreplace $args 0 1 get_eeprom]
			  } else {
				  puts "Wrong type: must be inserted | tx_disable | tx_reset | rx_los | eeprom"
				  return
			  }
		      }
		  }

		  if {$command == "set"} {
		      if {$no_args != 4} {
			  puts "Wrong #args: sfp set tx_disable <port> <value>"
			  return
                     } else {
			  set type [lindex $args 1]
			  if {($type == "tx_disable")} {
				  set args [lreplace $args 0 1 set_txdisable]
			  } else {
				  puts "Wrong type: must be tx_disable"
				  return
			  }
		      }
		  }

		  # instruct driver to obtain sfp feature via debugfs
                 exec echo "sfp_$args" > [dict get $target_files "phy"]

                 # display phy feature
                 set res [exec cat [dict get $target_files "phy"]]
                 return $res
}

# create gpio proc
# the proc receives phy feature and returns the result
proc gpio { args } {
		  global target_files

		  set no_args [llength $args]

		  if {$no_args > 0} {
			  set command [lindex $args 0]
		  }

		  # validate the command
		  if {($no_args == 0) || (($command != "read") && ($command != "write") && ($command != "info"))} {

			  puts "Performs gpio operations:"
			  puts "gpio read <gpio>"
			  puts "gpio write <gpio> <value>"
			  puts "gpio info <gpio>"
			  return
		  }

		  if {$command == "read"} {
		      if {$no_args != 2} {
			  puts "Wrong #args: gpio read <gpio>"
			  return
		      }
		  }

		  if {$command == "write"} {
		      if {$no_args != 3} {
			  puts "Wrong #args: gpio write <gpio> <value>"
			  return
		      }
		  }

		  if {$command == "info"} {
		      if {$no_args != 2} {
			  puts "Wrong #args: gpio info <gpio>"
			  return
		      }
		  }

		  # instruct driver to obtain gpio feature via debugfs
		  puts [exec echo "gpio_$args" > [dict get $target_files "phy"]]

		  # display phy feature
		  set res [exec cat [dict get $target_files "phy"]]
		  return $res
}

# tcl proc for activating feature in diag via debugfs
proc dbgfeature {feature {fileName ""} } {
	open_target_file "$feature"
	if {$fileName == ""} {
		global target_files
		global target_fps

		# instruct driver to obtain feature via debugfs
		exec echo "dump" > [dict get $target_files "$feature"]

		# display feature
		puts [exec cat [dict get $target_files "$feature"]]
		close [dict get $target_fps "$feature"]

		# restore tcl prompt if there was one
		restore_prompt   
	} else {
		if {$feature eq "idle_chk"} {
			dbgIdleChkDump $fileName
		} elseif {$feature eq "mcp_trace"} {
			dbgMcpTraceDump $fileName
		} elseif {$feature eq "reg_fifo"} {
			dbgRegFifoDump $fileName
		} elseif {$feature eq "protection_override"} {
			dbgProtectionOverrideDump $fileName
		} elseif {$feature eq "igu_fifo"} {
			dbgIguFifoDump $fileName
		} elseif {$feature eq "fw_asserts"} {
			dbgfwAsserts $fileName
		}
	}
}


# tcl proc for activating idle_chk_dump in diag via debugfs
proc dbgIdleChk { {fileName ""} } {
	dbgfeature "idle_chk" $fileName
}

# tcl proc for activating mcp_trace_dump in diag via debugfs
proc dbgMcpTrace { {fileName ""} } {
    dbgfeature "mcp_trace" $fileName
}

# tcl proc for activating reg_fifo_dump in diag via debugfs
proc dbgRegFifo { {fileName ""} } {
	dbgfeature "reg_fifo" $fileName
}

# tcl proc for activating protection_override_dump in diag via debugfs
proc dbgProtectionOverride { {fileName ""} } {
	dbgfeature "protection_override" $fileName
}

# tcl proc for activating igu_fifo_dump in diag via debugfs
proc dbgIguFifo { {fileName ""} } {
	dbgfeature "igu_fifo" $fileName
}

# tcl proc for activating fw_asserts in diag via debugfs
proc dbgfwAsserts { {fileName ""} } {
	dbgfeature "fw_asserts" $fileName
}

# rename diag's mcp to diagmcp
# if not in diag, mcp <cmd> where cmd is not trace will output it is not supported
if {[is_diag] == 1} {
         rename mcp diagmcp
} else {
	proc diagmcp {param} {
		puts "only supported on diag"
	}
}

# create our own 'mcp' proc
# if param was trace we want to invoke dbgUtil's mcpTrace
# if an mcp command other than trace was given, invoke diag's mcp command
#
proc mcp {args} {
        if { [lindex $args 0] == "trace"} {
		if {[lindex $args 1] == "-offline"} {
			diagmcp trace -offline
		} else {
                	mcpTrace
		}
        } else {
                if {[llength $args] == 0} {
                        puts "Too few arguments."
                        diagmcp help
                } elseif {[llength $args] == 1 } {
                        diagmcp [lindex $args 0]
                } elseif { [llength $args] == 2 } {
                        diagmcp [lindex $args 0] [lindex $args 1]
                } elseif { [llength $args] == 3 } {
                        diagmcp [lindex $args 0] [lindex $args 1] [lindex $args 2]
                } elseif { [llength $args] == 4 } {
                        diagmcp [lindex $args 0] [lindex $args 1] [lindex $args 2] [lindex $args 3]
                } else {
                    puts "Too many input arguments."
            }
        }
}


proc dbgGrcReset {} {
	open_target_file "grc"
}

######## Auto Generated debug features procs section #########################
	
# tcl proc for activating bus_reset in debugfs
proc dbgBusReset {oneShotEn hwDwords unifyInputs grcInputEn} {
	global target_fps

	if [open_target_file "bus"] {
		puts [dict get $target_fps "bus"] "reset $oneShotEn $hwDwords $unifyInputs $grcInputEn"
		flush [dict get $target_fps "bus"]
	}
}

# tcl proc for activating bus_set_pci_output in debugfs
proc dbgBusSetPciOutput {bufSizeKb} {
	global target_fps
	puts [dict get $target_fps "bus"] "set_pci_output $bufSizeKb"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_set_nw_output in debugfs
proc dbgBusSetNwOutput {portId destAddrLo32 destAddrHi16 dataLimitSizeKb sendToOtherEngine rcvFromOtherEngine} {
	global target_fps
	puts [dict get $target_fps "bus"] "set_nw_output $portId $destAddrLo32 $destAddrHi16 $dataLimitSizeKb $sendToOtherEngine $rcvFromOtherEngine"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_enable_block in debugfs
proc dbgBusEnableBlock {block lineNum cycleEn rightShift forceValid forceFrame} {
	global target_fps
	puts [dict get $target_fps "bus"] "enable_block $block $lineNum $cycleEn $rightShift $forceValid $forceFrame"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_enable_storm in debugfs
proc dbgBusEnableStorm {storm stormMode} {
	global target_fps
	puts [dict get $target_fps "bus"] "enable_storm $storm $stormMode"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_enable_timestamp in debugfs
proc dbgBusEnableTimestamp {validEn frameEn tickLen} {
	global target_fps
	puts [dict get $target_fps "bus"] "enable_timestamp $validEn $frameEn $tickLen"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_add_eid_range_sem_filter in debugfs
proc dbgBusAddEidRangeSemFilter {storm minEid maxEid} {
	global target_fps
	puts [dict get $target_fps "bus"] "add_eid_range_sem_filter $storm $minEid $maxEid"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_add_eid_mask_sem_filter in debugfs
proc dbgBusAddEidMaskSemFilter {storm eidVal eidMask} {
	global target_fps
	puts [dict get $target_fps "bus"] "add_eid_mask_sem_filter $storm $eidVal $eidMask"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_add_cid_sem_filter in debugfs
proc dbgBusAddCidSemFilter {storm cid} {
	global target_fps
	puts [dict get $target_fps "bus"] "add_cid_sem_filter $storm $cid"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_enable_filter in debugfs
proc dbgBusEnableFilter {block msgLen} {
	global target_fps
	puts [dict get $target_fps "bus"] "enable_filter $block $msgLen"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_enable_trigger in debugfs
proc dbgBusEnableTrigger {recPreTrigger preChunks recPostTrigger postCycles filterPreTrigger filterPostTrigger} {
	global target_fps
	puts [dict get $target_fps "bus"] "enable_trigger $recPreTrigger $preChunks $recPostTrigger $postCycles $filterPreTrigger $filterPostTrigger"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_add_trigger_state in debugfs
proc dbgBusAddTriggerState {block constMsgLen countToNext} {
	global target_fps
	puts [dict get $target_fps "bus"] "add_trigger_state $block $constMsgLen $countToNext"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_add_constraint in debugfs
proc dbgBusAddConstraint {constraintOp data dataMask compareFrame frameBit cycleOffset dwordOffsetInCycle isMandatory} {
	global target_fps
	puts [dict get $target_fps "bus"] "add_constraint $constraintOp $data $dataMask $compareFrame $frameBit $cycleOffset $dwordOffsetInCycle $isMandatory"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_start in debugfs
proc dbgBusStart {} {
	global target_fps
	puts [dict get $target_fps "bus"] "start"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_stop in debugfs
proc dbgBusStop {} {
	global target_fps
	puts [dict get $target_fps "bus"] "stop"
	flush [dict get $target_fps "bus"]
}

# tcl proc for activating bus_dump in debugfs
proc dbgBusDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "bus"]

	# copy from target file to outfile
	exec cp [dict get $target_files "bus"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

# tcl proc for activating grc_config in debugfs
proc dbgGrcConfig {grcParam val} {
	global target_fps
	puts [dict get $target_fps "grc"] "config $grcParam $val"
	flush [dict get $target_fps "grc"]
}

# tcl proc for activating grc_dump in debugfs
proc dbgGrcDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "grc"]

	# copy from target file to outfile
	exec cp [dict get $target_files "grc"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

# tcl proc for activating idle_chk_dump in debugfs
proc dbgIdleChkDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "idle_chk"]

	# copy from target file to outfile
	exec cp [dict get $target_files "idle_chk"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

# tcl proc for activating mcp_trace_dump in debugfs
proc dbgMcpTraceDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "mcp_trace"]

	# copy from target file to outfile
	exec cp [dict get $target_files "mcp_trace"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

# tcl proc for activating reg_fifo_dump in debugfs
proc dbgRegFifoDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "reg_fifo"]

	# copy from target file to outfile
	exec cp [dict get $target_files "reg_fifo"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

# tcl proc for activating igu_fifo_dump in debugfs
proc dbgIguFifoDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "igu_fifo"]

	# copy from target file to outfile
	exec cp [dict get $target_files "igu_fifo"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

# tcl proc for activating protection_override_dump in debugfs
proc dbgProtectionOverrideDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "protection_override"]

	# copy from target file to outfile
	exec cp [dict get $target_files "protection_override"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

# tcl proc for activating fw_asserts_dump in debugfs
proc dbgFwAssertsDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "fw_asserts"]

	# copy from target file to outfile
	exec cp [dict get $target_files "fw_asserts"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

# tcl proc for activating ilt_dump in debugfs
proc dbgIltDump {filename} {
	global target_files
	exec echo "dump" >> [dict get $target_files "ilt"]

	# copy from target file to outfile
	exec cp [dict get $target_files "ilt"] $filename

	# restore tcl prompt if there was one
	restore_prompt
}

######## End Auto Generated debug features section ###########################

# when in diag we want to learn the BDF from the diag parameters
if {[is_diag] == 1} {
        dev $::current(DEV)
}
catch {exec mount -t debugfs nodev /sys/kernel/debug/} msg
if {![string match "*already mounted*" $msg]} {
	puts "Could not mount debugfs"
}
