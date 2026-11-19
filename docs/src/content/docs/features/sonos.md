---
title: Sonos now playing
description: The album art and track title of whatever a Sonos room is playing, read straight from the speakers on your network.
---

Switch **Display → Mode** to **Sonos now playing** and set it up in the **Sonos** tab. The screen shows the album cover of the track a Sonos room is playing, with the title under it and, if you want, the artist.

The device asks the speakers themselves, over your home network. There is no Sonos account to link, no cloud service, and nothing to install on your computer. Whatever starts the music doesn't matter: the Sonos app on a Mac or phone, Spotify Connect, AirPlay, or the buttons on the speaker. The screen shows what the room is playing.

## Pick a room

Type the room name exactly as the Sonos app shows it, for example `Living Room`. Upper and lower case don't matter. Once the device has found your speakers, the **Rooms found** line under the field lists them, and the field suggests them as you type.

If the room is grouped with others, the screen follows the group, so it shows the same track as the Sonos app. When you regroup rooms, the device follows the change, usually within seconds and at most within a minute.

Leave the room blank and the device follows the first group it finds.

## Finding the speakers

By default the device finds a speaker by itself with a network search (SSDP), the same way the Sonos app does. Some networks block that search, mostly guest networks, some mesh WiFi systems, and networks that keep the SmallTV on a separate VLAN from the speakers. If the **Now playing** card says *no speaker found*, enter the IP address of any one of your Sonos speakers in **Speaker IP**. The Sonos app lists each speaker's IP address in its system information (where that lives depends on the app version), and your router's list of connected devices shows it too. Give that speaker a fixed address (a DHCP reservation in your router) so it doesn't change.

## What it shows

- **Music:** the album art and the track title, with the artist under it when **Artist under the title** is ticked. If a track has no artist, the album name is shown instead.
- **Radio:** when the station sends the current song, the song and artist; otherwise the station name.
- **TV and line-in:** *TV* or *Line-In*, with a music-note icon.
- **Long titles:** a title or artist too wide for the screen scrolls: it pauses at the start, scrolls through, and starts over.
- **Paused:** the same screen with a pause badge in the top-right corner. Pausing or resuming does not reload the cover.
- **Nothing playing:** a music note, *Nothing playing*, and the room name.

## Where covers come from

The device tries, in this order:

1. **A logo you saved for the radio station playing** (see below).
2. **The cover itself**, from the speaker or the music service. The device decodes it as it downloads, scaled down to fit the screen, so a full-size cover never has to fit in memory. For radio, it is the station's logo from the speaker's station info.
3. **A copy relayed by your Mac.** Some covers the device cannot fetch on its own: images only served over HTTPS (a secure connection needs more memory than the ESP8266 can spare), PNG images, or any cover while memory is short. When the [Mac stats](/smalltv-mod/features/sysmon/) script runs, the device asks it for those: the Mac downloads the image, shrinks it to 160 px, converts it to BMP with the built-in `sips`, and sends it over; the device turns it into its own screen format as it arrives, so drawing it needs no decoder. The cover appears a few seconds later. (BMP rather than JPEG because `sips` keeps a progressive JPEG progressive, and the device's JPEG decoder reads baseline JPEG only.)

Covers from the Mac and saved logos are kept in the device's storage, not in memory. Without any of them, radio shows a radio icon and everything else a music note. The **Now playing** card's **Shown from**, **Album art** and **Art details** lines say which source was used, or why a cover did not show.

## Station logos

When a radio station is playing, the Sonos tab shows a **Station logo** card. Pick an image of the logo (PNG or JPEG, a screenshot works too) and press **Save as this station's logo**. Your browser scales it to 160×160 and converts it to JPEG; the device stores it and shows it whenever that station plays, ahead of any other source, with or without the Mac. **Remove saved logo** deletes it again, and **Remove all saved logos** clears every station at once (that button works whatever is playing).

Storage looks after itself otherwise: the device keeps only the newest cover the Mac sent, each new one replacing the last, so relayed covers never pile up. Saved logos stay until you remove them; each is a small JPEG of about 5 to 15 KB.

## In the carousel

Tick **Sonos now playing** in the carousel list and the page joins the rotation **only while music is playing**. While another page is on screen, the device checks the room every 10 seconds, so the Now Playing page shows up shortly after the music starts. It stays in the rotation for five minutes after you pause, then drops out until the music starts again.

## Refresh rate

**Refresh (s)** sets how often the device asks the room what is playing while the page is on screen. The default is 3 seconds. Each refresh is two short requests on your local network.
