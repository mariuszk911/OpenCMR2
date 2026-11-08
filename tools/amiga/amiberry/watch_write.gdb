# Catch the instruction that writes one byte: Amiberry + m68k-amigaos-gdb.
#
# Start Amiberry with the GDB server:
#   amiberry --config OpenCMR2_os32.uae -s debugging_features=gdbserver -s gdb_port=2345 \
#            -s serial_port=TCP://127.0.0.1:1299 ...      (keep the serial port off 1234)
# Find the byte's address first (it changes every boot; a program that pauses after
# Open and prints a marker address is the usual way), then substitute it below:
#   docker run --rm --network host -v "$PWD":/w opencmr2/amiga-toolchain:latest \
#       sh -c 'timeout 600 m68k-amigaos-gdb -batch -x /w/watch_write.gdb'
set pagination off
set confirm off
set architecture m68k:68040
target remote 127.0.0.1:2345
watch *(unsigned char*)0xADDRESS
commands
silent
printf "WRITE value=%d pc=0x%08x a7=0x%08x\n", *(unsigned char*)0xADDRESS, $pc, $sp
info registers
x/6i $pc-12
continue
end
continue
