# Utilities for XMPP

```text
 _   _  __  __  _ __ ___    _ __    _ __ 
| | | | \ \/ / | '_ ` _ \  | '_ \  | '_ \
| |_| |  >  <  | | | | | | | |_) | | |_) |
 \__,_| /_/\_\ |_| |_| |_| | .__/  | .__/
        Utilities for XMPP |_|     |_|
==========================================
```

## Description

This project consists (or rather will consist) of a set of tools for,
testing, configuring, troubleshooting, and generally using XMPP services
in a way that fits with other basic tools found on a given POSIX system.
FIFOs corresponding to various services, protocols, and features may
optionally be created to simplify the process of working with and testing
new implementations using other standard \*NIX tools.

The original motivation for this project was to investigate, and ideally
implement an effective independent/sovereign XMPP spam mitigation system
for federated XMPP servers while keeping general purpose use in mind. 

## Project Goals

Apart from the overall direction, the following general goals apply:

- **Code is clear, easy to reason about, and maintain.**
- Utilities and functions should be applicable to general use cases.
- Learn, test, and contribute to the wider XMPP protocol.
- Provide feature tests and reference implementations for various XEPs

## Building and Installation

Tested on OpenBSD, and Linux (various). This is currently a very simple
tool, thus the "build system" is `make`, and a few shell scripts, if
required.

### Dependencies

- `clang or GCC`
- `BSD make or GNU make`
- `libstrophe(-dev)`

### OpenBSD:

```shell
$ ./configure && \
  make && \
  make install # As root
```

### Linux:

```shell
$ ./configure && \
  make && \
  sudo make install
```

### Windows:

**Install BSD or Linux, be liberated, rejoice, then...**

```shell
$ ./configure && \
  make && \
  sudo make install
```

## AI Code(ing Agents)

> AI has not been used in the production of this software.

A tool like many others, I could say a great deal regarding AI and it's
implications, but that's not needed. I'm not judging anyone for the
responsible use of generative AI, rather simply:
this is not the place for it.

[Don't be a Meat Proxy](https://meatproxy.me "Hilarious, and accurate.").

---

## Changes

Version 0.1.3

- Version bump to trigger other clients recalc of entity caps version hash
- Operators MUC join is now working... Not sure if it's because I added
  additional version information, corrected the node/vers string value, or
  changed the client type to console?
- Time to stop procrastinating, and configure the bot to join a series of
  busy MUCs, pull entity caps/metadata, message frequency etc. for the
  purposes of identifying potential spam across applicable federated svcs.
- Update bot.c, give up and use malloc when allocating strings for fifos
  and other bot metadata.


Version 0.1.2

- Added software version data to the initial entity caps string, and
  updated xep-0115-ver-gen.c accordingly.
- The operators MUC is not required for this bot, but I'm very curious
  what is going on there... I could ask, but where's the fun in that?


Version 0.1.1

- Drop #bot from the applicable XEP_0115 node value, as the local part
  must be used to delineate the version hash.
- Still cannot connect to the operators MUC. Not sure what protocol
  support is required.


Version 0.1.0

- Fix issue with the XEP_0115 response in bot.c, added a def for the
  applicable node value so that if I need to change it again, this doesn't
  happen...
- Tidy up some snprintf statements for clarity.


Version 0.0.13

- Fine! I don't want to but it looks like I need to use malloc here
  instead of trying to shoehorn a static singleton instance of a disco
  context object, now a new request allocates a context that is kept
  until the request is resolved by libstrophe.
  + TODO: implement cleanup timer when request goes out, if server/strophe
    don't complete within the allotted time, return an error..
- Add lmemtest and bmemtest for memory testing on Linux, and BSD
  respectively.


Version 0.0.12

- Refactor bot.c --> The way the bot was launched didn't clearly
  facilitate multiple independent instances.
- use sigaction instead of signal.
- Trap/ignore SIGPIPE to facilitate profiling with valgrind --> valgrind
  uses a pipe to communicate with the running program, as a result
  valgrind will report memory errors caused by itself.


Version 0.0.11

- Added support to query version from both the main utility, and from the
  in-band bot interface.
- Updated sendraw.c/h


Version 0.0.10

- Update disco.c add display_disco_info_addrs() helper to tidy up the
  recursive loop.
- Update README.md
- Add support (sendraw.h/c) to send arbitrary stanzas over an existing
  connection.
- Remove `/bin/sh` from the unveil call, popen() is not being used to call
  fortune any longer.


Version 0.0.9

- Move/Update the README --> README.md for the GitHubs.
- Version Bump before starting on the spambot


Version 0.0.8

- Project added to GitHub
- Updated the applicable entity capabilities presence node attribute to
  a FQDN (apparently, a few MUCs require it). Until I get around to a
  home page on sinelabs.ca, the node attribute corresponds to the
  github project page.


Version 0.0.7

- Move bot.sh to the scripts/ directory, and update it to start uxmpp, and
  immediately begin reading the applicable fifos
- Code cleaning, and memory verification pass
- Many small changes, bot is now functional, and can perform a couple of
  useful tasks.


Version 0.0.6

- Streamline the process of generating the XEP-0115 version string that
  corresponds to the disco#info response.  NOTE: if the disco#info
  response in bot.c is changed, xep-0115-ver-gen.c needs to be updated
  such that the canonicalized feature string corresponds accordingly.
- Move source files into src directory.
- Reinstate support to automatically generate applicable FIFOs when only
  the -f option is provided.


Version 0.0.5

- Change default mucbot behaviour so that by default fifo's are not
  created.
- Many small changes, refactors etc. as the project takes shape
- Added xmpp ping support (basic)
- Updated bot.c to support xmpp ping, version, and disco.


Version 0.0.2

- Add basic xmpp "bot" currently via mucbot command.  FIFOs are created
  and can be used to interact with a given MUC.
- Many other small changes.


Version 0.0.1 - Initial Commit

- Frame in project, and implement a basic disco request
