# Using Podcast Forge 8 with OBS, Discord, Zoom and other apps

Podcast Forge 8 does not install a virtual audio driver. It sends its stream buses to any Windows
playback device, and a virtual cable turns that into a "microphone" other apps can pick up. The
free [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) is the usual choice (one cable per bus;
VB-Audio also offers A+B / C+D packs for more cables).

## The three stream buses

| Bus | Contents | Typical use |
|---|---|---|
| **Stream: Main** | The full show: every channel, music, carts, reverb — after the master limiter | OBS live stream / recording, YouTube, Twitch |
| **Stream: Clean feed** | Main **without** music and carts (mix-minus style) | Sending the show to a remote guest in Discord / Zoom / Teams, so they don't hear the music twice or with a delay |
| **Stream: Music** | Music + carts only | A separate music track in OBS, or a DJ feed |

Talkback never reaches any of them (unless *Settings → Routing → Talkback to the recording* is on).

## OBS Studio

1. Install VB-Cable. Windows now has a playback device **CABLE Input** and a recording device
   **CABLE Output**.
2. Podcast Forge 8 → **DEVICE MATRIX** → drag *CABLE Input* onto **Stream: Main**.
3. OBS → *Sources* → **+** → *Audio Input Capture* → device **CABLE Output**.
4. OBS → *Settings → Audio*: set *Desktop Audio* and *Mic/Auxiliary Audio* to **Disabled** (or
   make sure your headset mics are not also captured by OBS), otherwise voices are heard twice.
5. Set the OBS audio source's *Sync Offset* if your video needs it — the Podcast Forge path adds
   the latency shown on the Diagnostics page (typically 20–70 ms, see LIMITATIONS.md).

Keep the Windows volume of **CABLE Input** and **CABLE Output** at 100 % (Windows applies it to the
stream). Podcast Forge bypasses Windows' audio *effects* on its own streams (raw mode), but OBS's
capture of CABLE Output is OBS's own stream.

## Remote guests (Discord, Zoom, Teams, Riverside in a browser…)

1. Assign a second cable (e.g. *CABLE-A Input*) to **Stream: Clean feed**; in the call app select
   *CABLE-A Output* as the **microphone**. The guest hears every local person, without music.
2. In the call app select another cable as the **speaker** (e.g. *CABLE-B Input*), and in Podcast
   Forge assign *CABLE-B Output* as the mic of a free channel (e.g. CH7 "Remote guest"). The guest
   now has a fader, DSP, a headphone send and an isolated recording track like everyone else.
3. **Mix-minus:** on the ROUTING tab, switch off that channel's cell in the **Clean** column.
   Otherwise the guest hears their own voice back with the call's delay (echo). Their voice stays
   on Main, in the headphones and on their isolated track.

## Checking the levels

The DIAGNOSTICS tab shows every stream's sync state and xruns; the meters on the master section
show what goes to Main. `PodcastForge8.exe --e2e` verifies a real playback → capture loop through
VB-Cable automatically (TESTING.md §5).
