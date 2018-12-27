# Blazefox exploits for Windows 10 RS5 64 bits

This the repository associated with the article [Exploiting Spidermonkey](TODO).

## Overview

`Blazefox` is an exploitation challenge written by [itszn](https://twitter.com/itszn13) for [Blaze CTF 2018](https://ctftime.org/event/591). The author added a `blaze` method to JavaScript Arrays that sets the size of the backing buffer to 420. This gives the attacker an out-of-bounds memory primitive.

![ifrit.js](exploits/gifs/ifrit.gif)

## Organization

* Three exploits are documented and available in [exploits](https://github.com/0vercl0k/blazefox/tree/master/exploits),
* A *WindDbg* JavaScript extension that allows to dump `js::Value` and `JSObject` objects in [sm](https://github.com/0vercl0k/blazefox/tree/master/sm),
* Various scripts built during the research in [scripts](https://github.com/0vercl0k/blazefox/tree/master/scripts),
* An x64 debug build of the JavaScript shell (along private symbol information) in [js-asserts](https://github.com/0vercl0k/blazefox/tree/master/js-asserts), and an x64 release build in [js-release](https://github.com/0vercl0k/blazefox/tree/master/js-release),
* Last but not least, the sources matching [js-release](https://github.com/0vercl0k/blazefox/tree/master/js-release) private symbol information.

