# Utilities for XMPP

```text
 _   _  __  __  _ __ ___    _ __    _ __ 
| | | | \ \/ / | '_ ` _ \  | '_ \  | '_ \
| |_| |  >  <  | | | | | | | |_) | | |_) |
 \__,_| /_/\_\ |_| |_| |_| | .__/  | .__/
-XMPP Utilities *NIX style-|_|     |_|
==========================================
```

This project consists (or rather will consist) of a suite of tools for,
testing, configuring, troubleshooting, and generally using XMPP services
in a way that fits with other basic tools found on a given POSIX system.
FIFOs corresponding to various services, protocols, and features may
optionally be created to simplify the process of working with and testing
new implementations using other standard \*NIX tools.

The original motivation for this project was to investigate, and ideally
implement an effective independent/sovereign XMPP spam mitigation system
for federated XMPP servers, and thus that should be considered the
projects initial focus while keeping general purpose use in mind. 

### DEPENDS

- `clang (or GCC)`
- `libstrophe(-dev)`
- `(open|libressl)-dev` (build only)


## BUILDING

Tested on OpenBSD, and Linux (various). This is currently a very simple
tool, thus the "build system" is `make`, and a few shell scripts, if
required.

### If You're running...

### OpenBSD:

```shell
$ make && \
  doas -u root make install
```

### Linux:

```shell
$ make \
  && sudo make PREFIX=/usr MAN_PREFIX=/usr/share install
```

### Windows:

**Install BSD or Linux, be liberated, rejoice, then...**

```sh
$ make \
  && sudo make PREFIX=/usr MAN_PREFIX=/usr/share install
```

## AI Code(ing Agents)

> AI has not been used in the production of this software.

A tool like many others, I could say a great deal regarding AI and it's
implications, but that is not needed. I'm not judging anyone for the
responsible use of generative AI, rather simply:
this is not the place for it.

Regardless, I'd encourage anyone to check out 
[don't be a Meat Proxy](https://meatproxy.me "please...").


---

## CHANGELOG

Version 0.0.9

- Move/Update the README --> README.md for the GitHubs.
- Version Bump before starting on the spambot


Version 0.0.8

- Project added to GitHub
- Updated the applicable entity caps to point to (apparently, a few MUCs
  require that the entity caps node resolves to an endpoint?)


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
