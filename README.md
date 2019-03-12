libGLOV fork of SDL
========================

Herein lies the version of SDL used by libGLOV for all of [Dashing Strike](http://www.dashingstrike.com)'s PC releases.

Primary changes from vanilla SDL (some waiting on SDL bugs to be merged/resolved):
* [Support more than 4 XInput devices by default](https://github.com/spurious/SDL/compare/9675b51...Jimbly:updates2)
  * Because we can, and should.  Uses RawInput correlated with XInput.
* [Expose IME candidate list on Windows](https://github.com/Jimbly/SDL/commit/2c5c6824a5d9717dff080e4926a36580a4136aa8)
  * Useful if you want nice, in-game IME
* [Map all Xbox-like DInput controllers to an appropriate GameController mapping](https://github.com/Jimbly/SDL/commit/6808b6f33a41e45927efbbce6ad90837378138aa)
  * When XINPUT is disabled (*highly* recommended if you wish to trivially support more than 4 devices without the major change above), this allows all Xbox-y controllers to get reasonable binds
* [Fix polling left trigger reporting right trigger's values](https://github.com/Jimbly/SDL/commit/5d8ec3cc2a36b4a8336e484f61bedb1e9599b366) - [[SDL Bugtracker](https://bugzilla.libsdl.org/show_bug.cgi?id=4547)]
  * Makes the polling joystick API more consistent with the evented API
* [Filter out IME-handled key messages when IME is active](https://github.com/Jimbly/SDL/commit/2d9ba13ce1f3e91a8afac42f074bdb5bc7f7e222)
  * Otherwise you get double key events when the user is using IME
* [Fix SetDIerror using unsupported format string](https://github.com/Jimbly/SDL/commit/0ba104ecd123d792f411415e30d8c69a4144bc15)
  * Don't silently discard DirectInput errors
* [Safer Math defines](https://github.com/Jimbly/SDL/commit/2e42806dbca55a22b5b15a4a5becc779f2118735)
  * Avoid compiler errors if you include both SDL and math.h with _USE_MATH_DEFINES set
* [Allow setting wndproc even if SDL creates the window](https://github.com/Jimbly/SDL/commit/a28601f1075b1a3b79a757baef755bc17367a8ed)
  * Useful for tricky stuff and debuggery
* [Allow building static libs](https://github.com/Jimbly/SDL/commit/cd35fe34d8f3aba726de37b445f3c0a55a68e4a5)
  * This allows for much smaller binaries, with fewer dependencies
