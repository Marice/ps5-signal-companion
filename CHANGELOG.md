# Changelog

All notable changes to Signal Companion are documented here. The format is based
on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/). Versions are the
app's `contentVersion`.

## [Unreleased]

### Added
- **Beeper volume** on the Status screen: high, medium or low.
- **Temperature light** on the Colour screen: the light shows the console's temperature, blue when cool, white when
  normal, amber when warm, with the limits to adjust. Status shows the CPU and SoC temperatures.
- **Updates reach ps5-autoloader:** when the app opens, it replaces an older ps5-signal in ps5-autoloader's folder
  with its own and says so. A newer or the same version stays.
- **An older ps5-signal is pointed out:** when the running ps5-signal is older than the app's, Status says so and
  "Restart ps5-signal" starts the new one.

### Changed
- Carries ps5-signal 0.2.0: it no longer turns the console's beep setting off, leaves the light to the console in
  rest mode, and answers again after waking.

## [v00.001.000] - 2026-10-16

First release.

### Added
- **Status:** shows whether ps5-signal runs, and what the beeper and the light do
  now. Plays a test beep or blinks the light.
- **Start ps5-signal from the app:** the app carries ps5-signal.elf (release
  build) and sends it to etaHEN's ELF loader. When the service does not answer,
  a dialog offers to start it. "Restart ps5-signal" loads the app's version after
  an update.
- **Start with ps5-autoloader:** puts ps5-signal last in
  `/data/ps5_autoloader/autoload.txt`, 3 seconds after the payload before it, or
  takes it out. Shown only when etaHEN allows `/data` in the sandbox.
- **Colour:** mix blue, white and amber with sliders, see it live on the console,
  keep it, or give the light back to the system. Presets and six colours of your
  own.
- **Startup:** edit the tune (up to 24 beeps) and the light (up to 24 steps) that
  ps5-signal plays when it starts, each 10 seconds at most in all, on timelines
  with a cursor while they play.
- Own icon and backgrounds. No music on the home screen.

### Removed
- The demo designs, music and GitHub workflows of ps5-homebrew-ui, which the app
  is built on.
