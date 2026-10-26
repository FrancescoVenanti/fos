# FOs

FOs is a custom firmware that turns the Seeed reTerminal Sticky (ESP32-S3,
3.97" e-paper, 480×800 portrait, 4 grays, capacitive touch, microSD) into a
pocket device for everyday things: money, tasks, reading, learning, chess,
small utilities and a remote terminal.

- **Offline-first:** every app except Terminal works with no network. Wi-Fi is only used to sync.
- **Glanceable:** neo-brutalist design for e-paper — thick outlines, hard shadows, four flat grays.
- **Configured on the device:** Settings decides which apps appear, their order and icon size.

The product spec is in [docs/PRODUCT.md](docs/PRODUCT.md). Build, run and test
instructions are in [CLAUDE.md](CLAUDE.md); progress is tracked in
[PROGRESS.md](PROGRESS.md) and design choices in [docs/DECISIONS.md](docs/DECISIONS.md).

## Quick start (Windows)

```powershell
# Desktop simulator (needs MSYS2 UCRT64 gcc + SDL2)
pio run -e simulator; .\.pio\build\simulator\program.exe

# Device firmware
pio run -e sticky
```

## Credits

FOs is built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader),
open-source e-reader firmware by the CrossPoint Reader organization and
contributors, and on the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk).
The reader engine, fonts pipeline, HAL and SD caching come from CrossPoint.
The original CrossPoint README is kept at
[docs/CROSSPOINT_README.md](docs/CROSSPOINT_README.md).

The desktop simulator is adapted from
[crosspoint-simulator](https://github.com/uxjulia/crosspoint-simulator) by
Julia Nguyen (MIT), as used by [CrossInk](https://github.com/uxjulia/CrossInk).

## License

MIT, same as CrossPoint. See [LICENSE](LICENSE); CrossPoint's copyright notice
is preserved there.
