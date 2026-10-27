/**
 * FOs Budgeteer <-> Google Sheet bridge (Google Apps Script web app).
 *
 * The Sticky reads categories and months from this sheet and appends new
 * transactions to it, without any server of your own. See README.md for
 * setup: paste this into Extensions > Apps Script of your budget sheet, run
 * setup() once, set the FOS_KEY script property, deploy as a web app.
 *
 * Sheets (created by setup() if missing; adjust COLUMNS to match yours):
 *   Transactions: Date | Type | Category | Description | Amount | ClientId
 *     Type is "expense" or "income"; Amount in euros, positive; Category is
 *     the category name (or id). ClientId is filled by the device and stops
 *     an entry from being added twice.
 *   Categories: Type | Id | Name | Budget | Exclude
 *     Budget is the monthly budget in euros (expenses); Exclude = TRUE keeps
 *     the category out of the monthly balance (NoCat).
 */

var TRANSACTIONS = 'Transactions';
var CATEGORIES = 'Categories';
// 1-based column numbers in the Transactions sheet.
var COLUMNS = {date: 1, type: 2, category: 3, description: 4, amount: 5, clientId: 6};

var DEFAULT_CATEGORIES = [
  ['income', 'stipendio', 'Stipendio', '', false],
  ['income', 'introito2', 'Introito2', '', false],
  ['expense', 'equitalia', 'Equitalia', '', false],
  ['expense', 'bollette', 'Bollette', '', false],
  ['expense', 'trasporti', 'Trasporti', '', false],
  ['expense', 'spesa', 'Spesa', '', false],
  ['expense', 'svago', 'Svago', '', false],
  ['expense', 'ristoranti', 'Ristoranti', '', false],
  ['expense', 'risparmio', 'Risparmio', '', false],
  ['expense', 'abbonamenti', 'Abbonamenti', '', false],
  ['expense', 'salute', 'Salute', '', false],
  ['expense', 'cura', 'Cura', '', false],
  ['expense', 'nocat', 'NoCat', '', true],
];

/** Run once from the editor: creates the two sheets with headers. */
function setup() {
  var book = SpreadsheetApp.getActiveSpreadsheet();
  if (!book.getSheetByName(TRANSACTIONS)) {
    var t = book.insertSheet(TRANSACTIONS);
    t.appendRow(['Date', 'Type', 'Category', 'Description', 'Amount', 'ClientId']);
    t.setFrozenRows(1);
  }
  if (!book.getSheetByName(CATEGORIES)) {
    var c = book.insertSheet(CATEGORIES);
    c.appendRow(['Type', 'Id', 'Name', 'Budget', 'Exclude']);
    DEFAULT_CATEGORIES.forEach(function (row) { c.appendRow(row); });
    c.setFrozenRows(1);
  }
  if (!PropertiesService.getScriptProperties().getProperty('FOS_KEY')) {
    PropertiesService.getScriptProperties().setProperty('FOS_KEY', Utilities.getUuid().slice(0, 8));
  }
  Logger.log('FOS_KEY = ' + PropertiesService.getScriptProperties().getProperty('FOS_KEY'));
}

function doGet(e) {
  var p = (e && e.parameter) || {};
  if (!authorized(p.key)) return json({error: 'unauthorized'});
  if (p.action === 'health') return json({ok: true, server: 'fos-sheet'});
  if (p.action === 'categories') return json(categories());
  if (p.action === 'month') return json(month(p.month || Utilities.formatDate(new Date(), tz(), 'yyyy-MM')));
  return json({error: 'unknown action'});
}

function doPost(e) {
  var body;
  try {
    body = JSON.parse(e.postData.contents);
  } catch (err) {
    return json({error: 'invalid json'});
  }
  if (!authorized(body.key)) return json({error: 'unauthorized'});
  if (body.action !== 'add') return json({error: 'unknown action'});
  var missing = ['clientId', 'type', 'amount', 'category', 'date'].filter(function (k) { return body[k] === undefined; });
  if (missing.length) return json({error: 'missing ' + missing.join(', ')});

  var lock = LockService.getScriptLock();
  lock.waitLock(20000);
  try {
    var sheet = sheetOrFail(TRANSACTIONS);
    // A resent entry (lost confirmation) is confirmed, not added again.
    var found = sheet.getRange(1, COLUMNS.clientId, Math.max(1, sheet.getLastRow()), 1)
                    .createTextFinder(String(body.clientId)).matchEntireCell(true).findNext();
    if (found) return json({ok: true, duplicate: true, clientId: body.clientId});
    var row = [];
    row[COLUMNS.date - 1] = body.date;
    row[COLUMNS.type - 1] = body.type;
    row[COLUMNS.category - 1] = categoryName(body.category);
    row[COLUMNS.description - 1] = body.description || '';
    row[COLUMNS.amount - 1] = Number(body.amount) / 100;
    row[COLUMNS.clientId - 1] = body.clientId;
    for (var i = 0; i < row.length; i++) if (row[i] === undefined) row[i] = '';
    sheet.appendRow(row);
    return json({ok: true, duplicate: false, clientId: body.clientId});
  } finally {
    lock.releaseLock();
  }
}

// --- helpers ------------------------------------------------------------------

function authorized(key) {
  var expected = PropertiesService.getScriptProperties().getProperty('FOS_KEY');
  return !expected || key === expected;
}

function json(value) {
  return ContentService.createTextOutput(JSON.stringify(value)).setMimeType(ContentService.MimeType.JSON);
}

function tz() { return Session.getScriptTimeZone(); }

function sheetOrFail(name) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheetByName(name);
  if (!sheet) throw new Error('Missing sheet "' + name + '": run setup()');
  return sheet;
}

function slug(text) {
  return String(text).toLowerCase().replace(/[^a-z0-9]+/g, '');
}

function categoryRows() {
  var rows = sheetOrFail(CATEGORIES).getDataRange().getValues().slice(1);
  return rows.filter(function (r) { return r[0] && (r[2] || r[1]); }).map(function (r) {
    return {
      type: String(r[0]).toLowerCase() === 'income' ? 'income' : 'expense',
      id: r[1] ? String(r[1]) : slug(r[2]),
      name: r[2] ? String(r[2]) : String(r[1]),
      budget: Math.round(Number(r[3] || 0) * 100),
      exclude: r[4] === true || String(r[4]).toUpperCase() === 'TRUE',
    };
  });
}

function categories() {
  var out = {income: [], expense: []};
  categoryRows().forEach(function (c) {
    out[c.type].push({id: c.id, name: c.name, excludeFromBalance: c.exclude});
  });
  return out;
}

// Category id -> the name written in the sheet.
function categoryName(id) {
  var match = categoryRows().filter(function (c) { return c.id === id; })[0];
  return match ? match.name : id;
}

function dateText(value) {
  if (value instanceof Date) return Utilities.formatDate(value, tz(), 'yyyy-MM-dd');
  return String(value).slice(0, 10);
}

function month(ym) {
  var cats = categoryRows();
  var byKey = {};
  cats.forEach(function (c) { byKey[slug(c.name)] = c; byKey[slug(c.id)] = c; });
  var rows = sheetOrFail(TRANSACTIONS).getDataRange().getValues().slice(1);
  var txs = [];
  rows.forEach(function (r, i) {
    var date = dateText(r[COLUMNS.date - 1]);
    if (date.slice(0, 7) !== ym) return;
    var cat = byKey[slug(r[COLUMNS.category - 1])];
    var type = String(r[COLUMNS.type - 1]).toLowerCase() === 'income' ? 'income' : 'expense';
    txs.push({
      id: String(r[COLUMNS.clientId - 1] || 'row' + (i + 2)),
      date: date,
      type: type,
      category: cat ? cat.id : slug(r[COLUMNS.category - 1]),
      amount: Math.round(Math.abs(Number(r[COLUMNS.amount - 1] || 0)) * 100),
      description: String(r[COLUMNS.description - 1] || ''),
    });
  });
  txs.sort(function (a, b) { return a.date < b.date ? 1 : a.date > b.date ? -1 : 0; });

  var excluded = {};
  cats.forEach(function (c) { if (c.exclude) excluded[c.id] = true; });
  var income = 0, expense = 0, spent = {};
  txs.forEach(function (t) {
    if (t.type === 'income') income += t.amount;
    if (t.type === 'expense') {
      spent[t.category] = (spent[t.category] || 0) + t.amount;
      if (!excluded[t.category]) expense += t.amount;
    }
  });
  var budgets = cats.filter(function (c) { return c.type === 'expense' && (c.budget > 0 || spent[c.id]); })
                    .map(function (c) { return {category: c.id, budget: c.budget, spent: spent[c.id] || 0}; });
  return {
    month: ym,
    totals: {income: income, expense: expense, balance: income - expense},
    budgets: budgets,
    transactions: txs,
  };
}
