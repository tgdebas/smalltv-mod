---
title: Mac stats
description: Your Mac's CPU, memory, disk and network use, sent by a small script on the Mac and drawn on the SmallTV.
---

Switch **Display → Mode** to **Mac stats**, or tick it in the carousel list. The screen shows four numbers from your Mac: CPU and memory as rings across the top, disk use and network speed as large numbers with a bar underneath. The look follows the "system monitor" dashboard of [geekmagic-hacs](https://github.com/adrienbrault/geekmagic-hacs).

The SmallTV cannot measure another computer, so a small script on the Mac does that. Every 3 seconds it reads the numbers with tools that come with macOS (`top`, `vm_stat`, `df`, `netstat`) and sends them to the device over your network. Nothing else is installed, and the numbers go only to the SmallTV.

## Set up the Mac

1. Copy `tools/mac/smalltv-stats.sh` from this repository to the Mac, for example to your home folder.
2. Open Terminal and run, with your device's address (the **Mac stats** tab shows the exact line):

   ```bash
   bash smalltv-stats.sh install smalltv-1a2b.local
   ```

This starts sending at once, and again after every login (it installs a LaunchAgent). To try it first without installing, run `bash smalltv-stats.sh run smalltv-1a2b.local` and stop it with Ctrl+C. To see what the script reads without sending anything, run `bash smalltv-stats.sh print`.

To remove it: `bash smalltv-stats.sh uninstall`.

The same script also relays Sonos covers the device cannot fetch on its own (HTTPS-only images, PNG): see [Where covers come from](/smalltv-mod/features/sonos/#where-covers-come-from). Set `SMALLTV_STATS_COVERS=0` in its environment to turn that off.

The script needs no password, even when the web page has one: like the Claude usage push, `/api/sysmon` only sets the numbers on the screen.

## What the numbers mean

- **CPU:** how busy all cores were over the last second, as `top` measures it.
- **Memory:** "Memory Used" as Activity Monitor counts it: app memory, wired memory and compressed memory, out of the Mac's total.
- **Disk:** how full the startup disk is. On APFS all volumes share one container, so this matches the "used" figure Finder shows for Macintosh HD.
- **Network:** traffic in and out together, on the interface the Mac uses for the internet (Wi-Fi or Ethernet), in Mb/s, or Gb/s above 1000. The bar is scaled to the recent peak, so it stays readable on a quiet connection and after a big download alike.

## When the Mac is away

With no numbers for 20 seconds (the Mac asleep, the script stopped, or a different network) the screen shows **Waiting for the Mac** and when it last heard from it. In the carousel, the page joins the rotation only while numbers are arriving.
