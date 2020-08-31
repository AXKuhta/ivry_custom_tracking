# ivry_custom_tracking
 This is a TCP interface for iVRy tracking that can be interacted with using `telnet` or `nc` (netcat).

See [Releases](https://github.com/AXKuhta/ivry_custom_tracking/releases)

Current problems:
- Can only connect once; can't reconnect after discorrecting.<br>
  This happens because the app gets stuck in a blocking recv() on a dead socket.<br>
  Need to implement something akin to SocketReadAvail() to get around that.
