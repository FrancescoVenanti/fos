# Budgeteer on a Google Sheet

Budgeteer can read and write a Google Sheet directly, with no server of your
own. The sheet runs a small Apps Script ([Budgeteer.gs](Budgeteer.gs)) that
answers the device; the device never needs your Google password or an OAuth
login.

## Setup (once, about 5 minutes)

1. Open your budget spreadsheet (or a new one) in Google Sheets.
2. **Extensions > Apps Script**. Delete the sample code and paste the whole of
   `Budgeteer.gs`. Save.
3. In the function menu pick **setup** and press **Run**. Allow access when
   Google asks (it is your own script). This creates the `Transactions` and
   `Categories` sheets if they are missing and generates a key. The key is
   shown in **Execution log** as `FOS_KEY = ...`; you can change it in
   **Project Settings > Script properties**.
4. **Deploy > New deployment > Web app**:
   - Execute as: **Me**
   - Who has access: **Anyone**
   Press **Deploy** and copy the **Web app URL** (ends in `/exec`).
5. On the Sticky: **Settings > Accounts > Budgeteer: Google Sheet**, enter the
   web app URL and the key.

"Anyone" means anyone with the URL can call the script, so the key is what
protects the sheet: keep both private. The script only reads the two sheets and
appends rows to `Transactions`.

## Sheets

`Transactions`: `Date | Type | Category | Description | Amount | ClientId`

- Type: `expense` or `income`. Amount in euros, positive. Category: the name.
- Rows you add by hand on the computer show up on the device too.
- ClientId is written by the device; a resent entry is not added twice.

`Categories`: `Type | Id | Name | Budget | Exclude`

- Budget: monthly budget in euros (expense categories).
- Exclude: `TRUE` leaves the category out of the monthly balance (NoCat).

If your sheet already has its own layout, change `COLUMNS` (and the sheet
names) at the top of the script instead of moving your data.

## After editing the script

Apps Script serves the version you deployed. After a change use
**Deploy > Manage deployments > Edit > Version: New version** so the URL stays
the same.

## Testing without Google

`tools/mock-backend/server.py` mimics the web app at
`http://127.0.0.1:8787/gsheet/exec` (key `sheetkey`), including the redirect
Apps Script answers with.
