# Terminal relay

The Sticky's Terminal app is a window onto a Claude Code session running on
your computer. This relay (standard-library Python) is the bridge: the device
polls it over your network, and it runs `claude` for each prompt.

```
python tools/terminal-relay/relay.py --cwd C:\path\to\your\project
```

It prints a **Relay address** and **Relay key**. Enter both on the device in
**Settings > Accounts > Terminal relay**. The computer and the Sticky must be
on the same network (home Wi-Fi or the phone hotspot the Sticky joins), and
Windows may ask to let Python accept connections on private networks.

## How it works

- Each prompt runs `claude -p "<prompt>" --output-format stream-json --verbose`
  in the `--cwd` folder, resuming the same session every time.
- Claude's text, tool calls and short tool results become transcript lines.
- In print mode Claude Code cannot ask for permission, so a tool that needs it
  is denied and reported. The relay shows the last denial as a permission
  request; **Approve** runs again with that tool allowed (`--allowedTools`)
  and asks Claude to continue. **Deny** leaves it denied.
- Your Claude Code settings (allow rules, model, CLAUDE.md) apply as usual.

## Options

| Option | Default | |
| --- | --- | --- |
| `--cwd` | current folder | Where Claude works |
| `--port` | 8790 | |
| `--key` | random, printed | Also `FOS_RELAY_KEY` |
| `--name` | computer name | Shown in the device's title |
| `--claude` | `claude` on PATH | The executable |
| `--fake` | | A scripted session without Claude (testing) |

## Tests

```
python -m unittest discover tools/terminal-relay
```

The tests cover parsing of the stream events and the HTTP API with the fake
session. The run against a real `claude` was not exercised by the tests.
