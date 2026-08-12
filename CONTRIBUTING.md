# Contributing

Thanks for looking. This is a small project with one maintainer and one test unit, which shapes what is most useful to receive.

## What would help most

**Reports from other firmware.** Everything here was found on a single SidStation running OS 1.11 R34. The most valuable thing anyone else can contribute is what their unit does differently, particularly if the SysEx Direct Program path works for you, or if patch loading behaves. Open an issue with your firmware version and what you saw.

**Windows and Linux testing against real hardware.** Those builds compile and package, and nobody has yet played a note through them. If you have a SidStation and a Windows or Linux machine, that is a gap only you can close.

**Bug reports with the conditions attached.** Timing bugs here depend on things that are easy to leave out: which host, whether the track was selected, how many instances were loaded, the Mod Rate, and whether the part was fast. A report that includes those is far more actionable than one that does not.

Before opening an issue about dropped notes, uneven modulation or silence, it is worth reading [troubleshooting](https://benjamindehli.github.io/sidstation-asid/troubleshooting/), which covers the cases that turn out to be the chip or the unit rather than the plugin.

## Getting set up

```sh
git clone https://github.com/benjamindehli/sidstation-asid.git
cd sidstation-asid
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

JUCE is fetched by the root `CMakeLists.txt`, so there is nothing to install first beyond CMake and a C++17 compiler. [ARCHITECTURE.md](ARCHITECTURE.md) explains how the pieces fit together and is the fastest way in.

## Before you open a pull request

Run the same checks CI runs:

```sh
make format        # rewrite everything in the house style
make format-check  # what CI runs, the same thing without writing
make test          # core protocol tests
make links-check   # every local link in the docs site and the Markdown resolves
make images-check  # the docs screenshots match their sources
```

`make format` installs the formatters on first run, pinned, into `node_modules` and `.venv`. clang-format handles the C++ in stock LLVM style, Prettier the docs site and Markdown and workflow YAML, and Ruff formats and lints the Python. Do not hand format around them. If the style is wrong, change the config rather than the code.

If your change touches `core/`, add a case to `core/tests/tests.cpp`. It is a dependency free harness and adding to it costs a few lines. Protocol behaviour that is not covered there tends to regress quietly.

If your change touches the docs site, remember it is served as static files with no build step, so generated output has to be committed. `make images` after replacing a screenshot source, and `make links-check` after moving a page between directories or changing the screenshot widths, which the README embeds too.

## Writing the docs

Say what is known and say what is not. Where a claim comes from testing on one unit, the text says so. This matters more than it sounds: several things here look like protocol facts and are really observations from a single machine, and writing them as facts would mislead the next person who reads them.

## Hardware, and the risk of it

Some of this talks to hardware that is old, out of production, and stores its patches in RAM. Back up your patches before experimenting with anything that writes to the unit. Sending a whole bank file clears patch memory before writing, so treat bank sends as destructive.

The plugin itself only streams register writes and does not store anything on the unit.

## Licence

The project is under the GNU GPL v3, and contributions are accepted under the same terms. By opening a pull request you are offering your changes under that licence.

## Conduct

Be decent to people. [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) has the longer version.
