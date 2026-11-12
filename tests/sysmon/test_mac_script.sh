#!/bin/bash
# The parsers of tools/mac/smalltv-stats.sh, fed recorded macOS command output.
# Runs anywhere bash and awk do (CI runs it on Linux).
set -u
cd "$(dirname "$0")/../.."
SMALLTV_STATS_LIB=1 . tools/mac/smalltv-stats.sh
fail=0
check() {   # NAME GOT WANT
  if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2' want '$3'"; fail=1; fi
}

# top -l 2 -n 0 -s 1: the second sample counts.
top_out='Processes: 612 total, 3 running, 609 sleeping, 3051 threads
2026/11/12 10:00:00
Load Avg: 2.01, 2.20, 2.31
CPU usage: 3.10% user, 5.20% sys, 91.70% idle
SharedLibs: 512M resident, 98M data, 61M linkedit.
Processes: 612 total, 3 running, 609 sleeping, 3051 threads
2026/11/12 10:00:01
Load Avg: 2.01, 2.20, 2.31
CPU usage: 30.12% user, 12.38% sys, 57.50% idle
SharedLibs: 512M resident, 98M data, 61M linkedit.'
check cpu "$(printf '%s\n' "$top_out" | parse_cpu)" "42.5"

# vm_stat on Apple silicon (16 KB pages), 16 GB of memory.
vm_out='Mach Virtual Memory Statistics: (page size of 16384 bytes)
Pages free:                                3462.
Pages active:                            316215.
Pages inactive:                          314010.
Pages speculative:                         2287.
Pages throttled:                              0.
Pages wired down:                        135080.
Pages purgeable:                           9711.
"Translation faults":                 963842611.
Pages copy-on-write:                   29440315.
Pages zero filled:                    471102551.
Pages reactivated:                     17010385.
Pages purged:                           4062271.
File-backed pages:                       222418.
Anonymous pages:                         410094.
Pages stored in compressor:              563217.
Pages occupied by compressor:            185204.
Decompressions:                        24203458.
Compressions:                          33212440.
Pageins:                               11702193.
Pageouts:                                184066.
Swapins:                                1213451.
Swapouts:                               1503411.'
# (410094 - 9711 + 135080 + 185204) * 16384 / 17179869184 = 68.7 %
check mem "$(printf '%s\n' "$vm_out" | parse_mem 17179869184)" "68.7"
# Older macOS without "Anonymous pages": active pages stand in for app memory.
old_vm=$(printf '%s\n' "$vm_out" | grep -v '^Anonymous')
check mem-old "$(printf '%s\n' "$old_vm" | parse_mem 17179869184)" "60.7"
check mem-no-total "$(printf '%s\n' "$vm_out" | parse_mem 0 || echo err)" "err"

# df -k /System/Volumes/Data on APFS: Used counts this volume only, so the
# share comes from size minus available (all volumes of the container).
df_out='Filesystem     1024-blocks      Used Available Capacity iused      ifree %iused  Mounted on
/dev/disk3s5     971350180 590234112 340117632    64%  2934117 3401176320    0%   /System/Volumes/Data'
check disk "$(printf '%s\n' "$df_out" | parse_disk)" "65.0"

# netstat -ibn -I en0: the <Link#> row, and IP rows (no MAC) are skipped.
ns_out='Name       Mtu   Network       Address            Ipkts Ierrs     Ibytes    Opkts Oerrs     Obytes  Coll
en0        1500  <Link#11>   a4:83:e7:12:34:56 41231771     0 52103994412 18602318     0 3502719245     0
en0        1500  fe80::1c2f: fe80:b::1c2f:9fd9 41231771     - 52103994412 18602318     - 3502719245     -
en0        1500  192.168.1     192.168.1.31     41231771     - 52103994412 18602318     - 3502719245     -'
check net "$(printf '%s\n' "$ns_out" | parse_net)" "52103994412 3502719245"
# A link row with no address column (some virtual interfaces).
ns_noaddr='Name  Mtu   Network       Address            Ipkts Ierrs     Ibytes    Opkts Oerrs     Obytes  Coll
utun3 1380  <Link#20>                          1200     0     345678     1100     0     123456     0'
check net-noaddr "$(printf '%s\n' "$ns_noaddr" | parse_net)" "345678 123456"

# 30 MB in 5 s = 48 Mbit/s; a counter that went backwards reads as 0.
check rate "$(rate_mbps 1000000000 1030000000 5)" "48.00"
check rate-reset "$(rate_mbps 900 100 5)" "0.00"

[ $fail -eq 0 ] && echo "mac script tests: all passed"
exit $fail
