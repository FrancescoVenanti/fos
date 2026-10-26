# FOs mock backend

A stand-in for the real sync backend (out of scope for the firmware), written
with the Python standard library only.

```powershell
python tools\mock-backend\server.py                    # http://127.0.0.1:8787
python tools\mock-backend\server.py --host 0.0.0.0     # reachable from the Sticky on your LAN
python tools\mock-backend\server.py --key devkey       # require "Authorization: Bearer devkey"
python tools\mock-backend\server.py --state mock.json  # keep received data across restarts
```

Set the address (and key, if you use one) on the device in
**Settings > Accounts**. The simulator's default backend address is
`http://127.0.0.1:8787`, so starting the server is enough to go online; stop it
to test offline behaviour. `http://127.0.0.1:8787/` shows what the device sent.

## API (v1)

All bodies are JSON. Amounts are integer cents. Dates are ISO 8601, local time.

| Area | Device receives | Device sends |
| --- | --- | --- |
| Health | `GET /api/v1/health` | — |
| Budgeteer | `GET /api/v1/budgeteer/categories`, `GET /api/v1/budgeteer/month?month=YYYY-MM` | `POST /api/v1/budgeteer/transactions` |
| Calendar | `GET /api/v1/calendar/agenda?days=7` | — |
| Read-later | `GET /api/v1/readlater/articles` | `POST /api/v1/readlater/articles/<id>/read` |
| Flashcards | `GET /api/v1/flashcards/decks` | — |
| Puzzles | `GET /api/v1/puzzles/daily` | — |
| Calculator | `GET /api/v1/calculator/rates` | — |
| Wallet | `GET /api/v1/wallet/codes` | — |
| Lock screen | `GET /api/v1/lockscreen/weather` | — |
| Terminal | `GET /api/v1/terminal/session?after=<seq>` (polled live) | `POST /api/v1/terminal/prompt`, `POST /api/v1/terminal/approval` |

`POST /api/v1/budgeteer/transactions` takes
`{"clientId", "type": "expense"|"income", "amount", "category", "description", "date"}`.
A repeated `clientId` returns `{"duplicate": true}` and is not stored twice, so a
device that lost a confirmation can resend safely. Categories with
`"excludeFromBalance": true` (NoCat) are left out of the month balance.

## Lichess mock

FOs talks to Lichess directly, not through the backend. For offline testing
the server also mimics the few Lichess endpoints FOs uses under `/lichess`:
`GET /lichess/api/account`, `GET /lichess/api/account/playing`,
`GET /lichess/api/puzzle/daily` and
`POST /lichess/api/board/game/<id>/move/<uci>`. Point the device at it with the
Lichess server field in Settings > Accounts.
