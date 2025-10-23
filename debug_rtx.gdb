# GDB script for debugging RTX synchronization issues
# Load this with: gdb -x debug_rtx.gdb

# Set up environment
set environment VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
set environment VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT

# Set breakpoints on critical RTX functions
break RTX_DispatchRaysVK
commands
  echo \n=== RTX_DispatchRaysVK called ===\n
  print params->width
  print params->height
  print vkrt.fence
  backtrace 3
  continue
end

break vkQueueSubmit
commands
  echo \n=== vkQueueSubmit called ===\n
  backtrace 3
  continue
end

break vkWaitForFences
condition 2 fence == vkrt.fence
commands
  echo \n=== vkWaitForFences on RTX fence ===\n
  print *fence
  print timeout
  backtrace 3
  continue
end

break vkResetFences
commands
  echo \n=== vkResetFences called ===\n
  print *pFences
  backtrace 3
  continue
end

# Break on device lost
break vkGetFenceStatus
commands
  silent
  set $status = vkGetFenceStatus(device, fence)
  if $status == -4
    echo \n!!! DEVICE LOST DETECTED !!!\n
    backtrace
    info locals
    quit
  end
  continue
end

# Set pagination off for continuous output
set pagination off
set print pretty on
set print thread-events on

# Start the program
echo Starting Quake3e with RTX debugging...\n
run +set r_rtx 1 +set rtx_debug 1 +set rtx_enabled 1 +set developer 1 +map q3dm17 +timedemo 1 +demo four +set com_speeds 1