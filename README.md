# ivry_custom_tracking
 This is a TCP interface for iVRy tracking that can be interacted with using `telnet` or `nc` (netcat).

Place the .exe in `\Steam\steamapps\common\iVRy\bin\win64`, set the tracking to "Custom" in the iVRy settings, restart SteamVR and connect to it using `telnet localhost 8021` or `nc localhost 8021`

The only available command is `Pos X Y Z`. It accepts both integers and floating point numbers: `Pos 1 10 1` or `Pos 0.1 0.75 -5` or `Pos .1 .1 .1` all work.

See [Releases](https://github.com/AXKuhta/ivry_custom_tracking/releases)

Future plans:
- Make an OpenTrack UDP bridge?
