# Security

## What this software does, in security terms

SidStation ASID is an audio plugin that sends MIDI to a device attached to your machine. It opens a MIDI output port, writes SysEx and note messages to it, and reads and writes preset files in your user application data folder. That is the whole surface.

It makes no network connections. It has no telemetry, no update check, and no analytics. It does not load code at runtime, and it parses nothing that arrives from a network.

The one place it parses untrusted input is SysEx and .syx patch bank files, if you point it at one. Those parsers live in `core/` and are the part of the codebase where a malformed input is worth thinking about.

## Reporting a vulnerability

Please report privately rather than opening a public issue.

Use GitHub's private reporting on the [Security tab](https://github.com/benjamindehli/sidstation-asid/security) of the repository, which goes straight to the maintainer and stays hidden until it is resolved. If that is unavailable to you, get in touch through [dehlimusikk.no](https://www.dehlimusikk.no/#contact) instead and say that it is a security report, without details in the first message.

What to expect: an acknowledgement within a week, and an honest answer about whether and when it will be fixed. This is a small project maintained by one person around other work, so the useful thing to promise is a reply rather than a deadline.

If you would like credit in the release notes, say so and it will be there. If you would rather not be named, that is fine too.

## Supported versions

The most recent release is the supported one. Fixes go into a new release rather than being backported, and releases are listed in [CHANGELOG.md](CHANGELOG.md).

## Build and dependency notes

The macOS build is signed and notarized. The Windows and Linux builds are not signed, so your system will warn about them, and you should verify you got them from the [releases page](https://github.com/benjamindehli/sidstation-asid/releases) rather than from somewhere else.

The plugin's only build dependency is JUCE, pinned by tag in `CMakeLists.txt`. Dependabot cannot see that, since it does not read CMake, so it is checked by hand.

Everything in `requirements-dev.txt` and `package.json` is development tooling: formatters, linters and the scripts that generate the docs site. None of it ships in the plugin or runs on a user's machine. Those are covered by Dependabot alerts, and a vulnerability in one of them is a matter for contributors rather than for anyone running the plugin.
