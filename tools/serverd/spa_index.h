#pragma once

#if defined(OPENADS_WITH_HTTP)

#include <string>

namespace openads::studio {

// studio.web.0.2 — phpMyAdmin-style admin UI:
//
//   - Left tree: list of *.dbf files; click selects a table.
//   - Top tabs (right pane): Browse | Structure | Insert | SQL | Server.
//   - Browse: paginated row grid with edit/delete buttons per row.
//   - Structure: column metadata + record count.
//   - Insert: empty form for a new row.
//   - SQL: free-form editor + result grid.
//   - Server: engine version + listed tables.
//
// All UI state lives in the SPA; the server is stateless. Each
// REST round-trip opens a short-lived ABI connection.

// The SPA HTML is ~73 KB. MSVC caps a single raw-string literal at
// ~16 KB (and clang's -Woverlength-strings flags long literals), so the
// HTML lives in several sub-12 KB `kSpaPartN` literals joined into one
// std::string at static-init. The diagnostic pragma is kept as a guard.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
#endif

namespace detail {
inline constexpr const char kSpaPart1[] = R"OPENADS_SPA(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>OpenADS Studio</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
  * { box-sizing: border-box; }
  :root {
    --bg:        #1e1e1e;
    --fg:        #ddd;
    --panel:     #252526;
    --panel-2:   #2d2d30;
    --border:    #3c3c3c;
    --accent:    #0d6efd;
    --accent-2:  #094771;
    --danger:    #d9534f;
    --ok:        #6cc24a;
    --err:       #f48771;
  }
  body.light {
    --bg:        #fafafa;
    --fg:        #222;
    --panel:     #ffffff;
    --panel-2:   #f0f0f0;
    --border:    #d0d0d0;
    --accent:    #0d6efd;
    --accent-2:  #d6e6ff;
    --danger:    #d9534f;
    --ok:        #2e7d32;
    --err:       #b71c1c;
  }
  body { font-family: -apple-system, system-ui, Segoe UI, Roboto, sans-serif;
         margin: 0; height: 100vh; display: flex; flex-direction: column;
         background: var(--bg); color: var(--fg); font-size: 19px; }
  header { background: #0d6efd; color: white; padding: 14px 22px;
           display: flex; align-items: center; justify-content: space-between; }
  header h1 { font-size: 24px; margin: 0; font-weight: 500; }
  header .status { font-size: 17px; opacity: 0.85; }
  main { flex: 1; display: flex; min-height: 0; }
  aside { width: 320px; background: var(--panel); border-right: 1px solid var(--border);
          overflow-y: auto; flex-shrink: 0; }
  aside h2 { font-size: 15px; text-transform: uppercase; opacity: 0.6;
             margin: 16px 18px 10px; }
  aside ul { list-style: none; padding: 0; margin: 0; }
  aside li { padding: 10px 18px; cursor: pointer; font-size: 18px; }
  aside li:hover { background: var(--panel-2); }
  aside li.active { background: #094771; color: white; }
  section.work { flex: 1; display: flex; flex-direction: column; min-width: 0; }
  nav.tabs { background: var(--panel-2); padding: 0 12px; display: flex;
             gap: 1px; border-bottom: 1px solid #094771; }
  nav.tabs button { background: var(--panel-2); color: #aaa; border: 0;
                    padding: 12px 26px; cursor: pointer; font-size: 17px; }
  nav.tabs button:hover { background: #3a3a3d; color: white; }
  nav.tabs button.active { background: #094771; color: white; }
  .pane { flex: 1; overflow: auto; padding: 20px; min-height: 0; }
  .pane.hidden { display: none; }
  .toolbar { display: flex; gap: 12px; align-items: center;
             margin-bottom: 14px; flex-wrap: wrap; }
  .toolbar button, .btn { background: #0d6efd; color: white; border: 0;
                          padding: 9px 22px; cursor: pointer;
                          border-radius: 3px; font-size: 17px; }
  .toolbar button:hover, .btn:hover { background: #0b5ed7; }
  .btn-danger { background: #d9534f; }
  .btn-danger:hover { background: #c9302c; }
  .btn-secondary { background: #444; }
  .btn-secondary:hover { background: #555; }
  .err { color: #f48771; font-size: 17px; }
  .ok  { color: #6cc24a; font-size: 17px; }
  table { border-collapse: collapse; width: 100%; }
  th, td { padding: 8px 12px; border: 1px solid var(--border); text-align: left;
           white-space: nowrap; vertical-align: top; font-size: 17px; }
  th { background: var(--panel-2); font-weight: 500; position: sticky; top: 0;
       z-index: 1; }
  tr.deleted td { opacity: 0.4; text-decoration: line-through; }
  .editor textarea { background: #1e1e1e; color: #ddd; border: 1px solid var(--border);
                     outline: none; padding: 14px;
                     font: 18px Consolas, Monaco, monospace; resize: vertical;
                     width: 100%; min-height: 170px; }
  .form-row { display: flex; align-items: center; margin-bottom: 10px;
              gap: 14px; }
  .form-row label { width: 200px; font-size: 17px; opacity: 0.85; }
  .form-row input { flex: 1; background: var(--panel-2); color: #ddd;
                    border: 1px solid #444; padding: 8px 12px;
                    font: 17px Consolas, monospace; border-radius: 2px; }
  .empty { padding: 28px; opacity: 0.5; font-size: 18px; }
  .pager { margin-top: 14px; display: flex; gap: 12px; align-items: center; }
  .pager span { font-size: 17px; opacity: 0.85; }
  .kv { display: grid; grid-template-columns: 200px 1fr; gap: 8px 22px;
        font-size: 17px; max-width: 800px; }
  .kv > div:nth-child(odd) { opacity: 0.7; }
  .modal-bg { position: fixed; inset: 0; background: rgba(0,0,0,0.6);
              display: none; align-items: center; justify-content: center;
              z-index: 10; }
  .modal-bg.show { display: flex; }
  .modal { background: var(--panel); border: 1px solid #094771;
           padding: 22px; border-radius: 6px; min-width: 460px;
           max-width: 90vw; max-height: 90vh; overflow: auto; }
  .modal h3 { margin: 0 0 16px; font-size: 17px; }
  .col-row { display: grid; grid-template-columns: 1.3fr 0.6fr 0.5fr 0.5fr 0.4fr;
             gap: 6px; margin-bottom: 5px; align-items: center; }
  .col-row input, .col-row select { background: var(--panel-2); color: #ddd;
             border: 1px solid #444; padding: 5px 8px;
             font: 14px Consolas, monospace; border-radius: 2px; }
  .col-row button { background: #d9534f; color: white; border: 0;
             padding: 4px 8px; cursor: pointer; border-radius: 2px;
             font-size: 12px; }
  .aside-head { display: flex; align-items: center;
                justify-content: space-between; margin: 14px 16px 8px; }
  .aside-head h2 { margin: 0; }
  .aside-head button { background: #094771; color: white; border: 0;
                       width: 24px; height: 24px; cursor: pointer;
                       border-radius: 3px; font-size: 16px; line-height: 1; }
  /* Truncate big cells; click to expand in a modal. */
  td.cell { max-width: 380px; overflow: hidden;
            text-overflow: ellipsis; cursor: pointer; }
  td.cell:hover { background: var(--panel-2); }
  .modal pre { background: #1e1e1e; padding: 14px; border-radius: 3px;
               font: 14px Consolas, monospace; white-space: pre-wrap;
               word-break: break-all; max-height: 70vh; overflow: auto;
               border: 1px solid var(--border); }
  /* SQL editor syntax highlight overlay */
  .sql-kw     { color: #569cd6; font-weight: 600; }
  .sql-str    { color: #ce9178; }
  .sql-num    { color: #b5cea8; }
  .sql-cmt    { color: #6a9955; font-style: italic; }
  body.light .sql-kw  { color: #0d6efd; }
  body.light .sql-str { color: #a31515; }
  body.light .sql-num { color: #098658; }
  body.light .sql-cmt { color: #008000; }
  td.sel-col { width: 30px; text-align: center; }
  td.sel-col input { transform: scale(1.3); cursor: pointer; }

  /* ============================================================
     Responsive — phone & tablet. The table-list <aside> turns
     into a slide-in drawer toggled by the header hamburger; the
     work area, tabs, forms and modals reflow to one column.
     ============================================================ */
  html { -webkit-text-size-adjust: 100%; }
  #nav-toggle { display: none; background: transparent; color: white;
                border: 1px solid rgba(255,255,255,0.4); border-radius: 3px;
                font-size: 20px; line-height: 1; padding: 4px 13px;
                cursor: pointer; margin-right: 14px; }
  #nav-backdrop { display: none; position: fixed; inset: 0;
                  background: rgba(0,0,0,0.5); z-index: 8; }
  #nav-backdrop.show { display: block; }

  /* ---- Tablet and below ---- */
  @media (max-width: 768px) {
    body { font-size: 17px; }
    header { padding: 12px 16px; }
    header h1 { font-size: 20px; }
    header .status { display: none; }
    #nav-toggle { display: inline-block; }
    /* sidebar becomes an off-canvas drawer */
    aside { position: fixed; top: 0; left: 0; height: 100vh;
            width: min(300px, 82vw); z-index: 9;
            transform: translateX(-100%);
            transition: transform 0.22s ease;
            box-shadow: 2px 0 14px rgba(0,0,0,0.4); }
    aside.open { transform: translateX(0); }
    nav.tabs { overflow-x: auto; flex-wrap: nowrap;
               -webkit-overflow-scrolling: touch; }
    nav.tabs button { padding: 12px 18px; white-space: nowrap; }
    .pane { padding: 14px; }
    .kv { grid-template-columns: 160px 1fr; }
  }

  /* ---- Phone ---- */
  @media (max-width: 480px) {
    header { flex-wrap: wrap; row-gap: 8px; }
    header h1 { font-size: 18px; }
    header > div { gap: 10px !important; flex-wrap: wrap; }
    .pane { padding: 10px; }
    .toolbar { gap: 8px; }
    .form-row { flex-direction: column; align-items: stretch; gap: 4px; }
    .form-row label { width: auto; }
    .kv { grid-template-columns: 1fr; gap: 2px 0; max-width: none; }
    .kv > div:nth-child(even) { margin-bottom: 8px; }
    .modal { min-width: 0 !important; width: 94vw; padding: 16px; }
    .col-row { grid-template-columns: 1.2fr 0.7fr 0.5fr 0.5fr auto; gap: 4px; }
    .col-row input, .col-row select { font-size: 13px; padding: 5px; }
    /* larger touch targets */
    .toolbar button, .btn, nav.tabs button { min-height: 42px; }
    aside li { padding: 13px 18px; }
  }
</style>
</head>
<body>
<header>
  <button id="nav-toggle" aria-label="Toggle table list">&#9776;</button>
  <h1>OpenADS Studio</h1>
  <div style="display:flex;gap:18px;align-items:center">
    <select id="lang-pick"
       style="background:transparent;color:white;border:1px solid rgba(255,255,255,0.4);
              padding:5px 8px;border-radius:3px;cursor:pointer;font-size:14px">
      <option value="en">EN</option>
      <option value="es">ES</option>
      <option value="pt">PT</option>
    </select>
    <button id="theme-toggle"
       style="background:transparent;color:white;border:1px solid rgba(255,255,255,0.4);
              padding:5px 12px;border-radius:3px;cursor:pointer;font-size:15px">
      🌙
    </button>
    <a href="https://fivetechsoft.github.io/OpenADS/"
       target="_blank" rel="noopener"
       style="color:white;text-decoration:none;font-size:15px;
              opacity:0.9;border:1px solid rgba(255,255,255,0.4);
              padding:5px 12px;border-radius:3px">
      📖 Docs
    </a>
    <!-- studio.web.0.17 — deployment-mode badge. Filled in by
         applyUrlState() from /api/health: 🏠 LocalServer when
         the console is hosted in-process by ace64.dll, 🌐 Remote
         Server when hosted by openads_serverd. -->
    <div id="mode-badge"
         title="Studio deployment mode"
         style="font-size:13px;font-weight:600;letter-spacing:0.3px;
                padding:4px 10px;border-radius:3px;
                background:rgba(255,255,255,0.18);
                border:1px solid rgba(255,255,255,0.4);
                color:white;display:none"></div>
    <div class="status" id="status">…</div>
  </div>
</header>
<div id="nav-backdrop"></div>
<main>
  <aside>
    <div class="aside-head">
      <h2 data-i18n="tables">Tables</h2>
      <div style="display:flex;gap:4px">
        <button id="btn-refresh-tables" title="Refresh">↻</button>
        <button id="btn-upload-table"   title="Upload DBF">⇪</button>
        <button id="btn-new-table"      title="New table">+</button>
      </div>
    </div>
    <ul id="tables"></ul>
    <input type="file" id="upload-input" multiple
           style="display:none" accept=".dbf,.cdx,.ntx,.fpt,.dbt,.add">
    <h2 data-i18n="server">Server</h2>
    <ul><li id="server-link" data-i18n="info">Info</li></ul>
  </aside>
  <section class="work">
    <nav class="tabs" id="tabs">
      <button data-tab="browse"    data-i18n="browse"    class="active">Browse</button>
      <button data-tab="structure" data-i18n="structure">Structure</button>
      <button data-tab="insert"    data-i18n="insert">Insert</button>
      <button data-tab="sql"       data-i18n="sql">SQL</button>
      <button data-tab="server"    data-i18n="server">Server</button>
      <button data-tab="sessions"  data-i18n="sessions">Sessions</button>
      <button data-tab="dd"        data-i18n="dict">Dict</button>
    </nav>

    <div id="pane-browse" class="pane">
      <div class="toolbar">
        <span id="browse-title" class="err"></span>
        <input id="browse-filter" placeholder="filter rows…"
               style="background:var(--panel-2);color:var(--fg);
                      border:1px solid var(--border);
                      padding:6px 10px;font-size:15px;border-radius:2px;
                      flex:1;min-width:140px;max-width:340px">
        <button class="btn btn-danger" id="browse-bulk-del"
                style="display:none">Delete selected</button>
        <span id="browse-bulk-count"></span>
      </div>
      <!-- studio.web.0.18 — Rushmore-style AOF (Advantage Optimised
           Filters) toolbar. Type a cond like AGE >= 25 or
           NAME = 'SMITH', press Apply; Skip / GoTop walk only the
           matching records. The badge shows whether the filter was
           served by an index (🟢 FULL), partly (🟡 PART) or fell
           back to a full-record AST evaluation (⚪ NONE / no AOF). -->
      <div class="toolbar" style="margin-top:6px">
        <span style="font-size:13px;color:var(--muted);min-width:170px"
              data-i18n="aof_label">AOF (Rushmore) filter:</span>
        <input id="browse-aof" placeholder="e.g. AGE >= 25"
               style="background:var(--panel-2);color:var(--fg);
                      border:1px solid var(--border);
                      padding:6px 10px;font-size:15px;border-radius:2px;
                      flex:1;min-width:200px;max-width:520px;
                      font-family:ui-monospace,monospace">
        <button class="btn btn-primary" id="browse-aof-apply"
                data-i18n="apply">Apply</button>
        <button class="btn btn-secondary" id="browse-aof-clear"
                data-i18n="clear">Clear</button>
        <!-- studio.web.0.20 — guided demo button. One click runs
             the full AOF (Rushmore) story end-to-end against the
             active table: pick a real value from row 1, install
             the AOF (lands at NONE since no index yet), trigger
             the auto-suggested CREATE INDEX, re-apply, end at
             FULL. Status line below narrates each step. -->
        <button class="btn btn-secondary" id="browse-aof-demo"
                title="Run the full AOF demo end-to-end on the active table">▶ Demo</button>
        <span id="browse-aof-badge"
              title="AdsGetAOFOptLevel — shows whether the filter is served by an index"
              style="font-size:13px;font-weight:600;
                   )OPENADS_SPA";
inline constexpr const char kSpaPart2[] = R"OPENADS_SPA(  padding:4px 10px;border-radius:3px;
                     background:rgba(255,255,255,0.05);
                     border:1px solid var(--border);
                     display:none"></span>
        <!-- studio.web.0.19 — when the AOF didn't reach FULL, list
             the candidate character/memo fields that don't have a
             matching index yet. Each chip kicks off a one-click
             CREATE INDEX so the next Apply lifts OptLevel. -->
        <span id="browse-aof-hints"
              style="font-size:13px;color:var(--muted);
                     display:none"></span>
      </div>
      <!-- studio.web.0.20 — narration line for the guided Demo. -->
      <div id="browse-aof-demo-status"
           style="font-size:13px;color:var(--muted);
                  margin:2px 0 4px 0;display:none"></div>
      <div id="browse-grid" class="empty" data-i18n="browse_pick">Select a table on the left.</div>
      <div class="pager" id="browse-pager"></div>
    </div>

    <div id="pane-structure" class="pane hidden">
      <div id="structure-body" class="empty">Select a table.</div>
    </div>

    <div id="pane-insert" class="pane hidden">
      <div id="insert-body" class="empty">Select a table.</div>
    </div>

    <div id="pane-sql" class="pane hidden">
      <div class="editor" style="position:relative">
        <pre id="sql-hl" aria-hidden="true"
             style="position:absolute;inset:0;margin:0;padding:14px;
                    pointer-events:none;color:transparent;
                    background:transparent;border:1px solid transparent;
                    font:18px Consolas, Monaco, monospace;
                    white-space:pre-wrap;word-break:break-word;
                    overflow:hidden;min-height:170px"></pre>
        <textarea id="sql"
                  placeholder="SELECT * FROM yourtable.dbf"
                  style="position:relative;background:transparent;
                         caret-color:var(--fg)"></textarea>
      </div>
      <div class="toolbar">
        <button id="sql-run">Run (Ctrl+Enter)</button>
        <button id="sql-export"      class="btn-secondary">Export CSV</button>
        <button id="sql-export-json" class="btn-secondary">Export JSON</button>
        <button id="sql-save"        class="btn-secondary">Save query…</button>
        <select id="sql-saved" style="background:var(--panel-2);color:var(--fg);
                border:1px solid var(--border);padding:7px 10px;
                font-size:15px;border-radius:2px"></select>
        <button id="sql-saved-del" class="btn-danger" style="display:none">×</button>
        <span id="sql-status"></span>
      </div>
      <div id="sql-result" class="empty">Result will appear here.</div>
    </div>

    <div id="pane-server" class="pane hidden">
      <div class="toolbar"><button id="server-refresh" class="btn-secondary">Refresh server health</button></div>
      <div id="server-health" class="empty">Loading health…</div>
      <div id="server-body" class="empty">Loading…</div>
    </div>

    <div id="pane-sessions" class="pane hidden">
      <div class="toolbar">
        <button class="btn btn-secondary" id="sessions-refresh">Refresh</button>
        <label style="font-size:14px;opacity:0.85">
          <input type="checkbox" id="sessions-auto" checked>
          auto-refresh every 3 s
        </label>
        <span id="sessions-status"></span>
      </div>
      <div id="sessions-body" class="empty">Loading…</div>
    </div>

    <div id="pane-dd" class="pane hidden">
      <div class="toolbar">
        <select id="dd-pick" style="background:var(--panel-2);color:#ddd;
                border:1px solid #444;padding:6px 10px;font-size:15px;
                border-radius:2px"></select>
        <button class="btn" id="dd-new">New dict…</button>
        <button class="btn btn-danger" id="dd-drop">Drop</button>
        <span id="dd-status"></span>
      </div>
      <div id="dd-body" class="empty">No dictionary selected.</div>
    </div>
  </section>
</main>

<div class="modal-bg" id="modal-create">
  <div class="modal">
    <h3>New Table</h3>
    <div class="form-row">
      <label>Table file name</label>
      <input id="ct-name" type="text" placeholder="newtable.dbf">
    </div>
    <div class="col-row" style="font-weight:500;opacity:0.85;font-size:13px">
      <div>Column name</div><div>Type</div><div>Length</div>
      <div>Decimals</div><div></div>
    </div>
    <div id="ct-cols"></div>
    <div class="toolbar" style="margin-top:14px">
      <button class="btn btn-secondary" id="ct-add-col">+ column</button>
      <button class="btn" id="ct-create">Create</button>
      <button class="btn btn-secondary" id="ct-cancel">Cancel</button>
      <span id="ct-status"></span>
    </div>
  </div>
</div>

<div class="modal-bg" id="modal-cell">
  <div class="modal" style="min-width:600px">
    <h3 id="cell-title">Cell</h3>
    <div class="toolbar" style="margin-bottom:10px">
      <span style="opacity:0.85;font-size:14px">Bytes: <b id="cell-len">0</b></span>
      <button class="btn btn-secondary" id="cell-mode-text">Text</button>
      <button class="btn btn-secondary" id="cell-mode-hex">Hex</button>
    </div>
    <pre id="cell-body"></pre>
    <div class="toolbar" style="margin-top:14px">
      <button class="btn btn-secondary" id="cell-copy">Copy</button>
      <button class="btn btn-secondary" id="cell-close">Close</button>
    </div>
  </div>
</div>

<div class="modal-bg" id="modal-encrypt">
  <div class="modal">
    <h3>Encrypt table</h3>
    <p style="margin:0 0 14px;opacity:0.8">
      AES-256-CTR keyed off the password below. Schema stays plain
      text; record bodies are encrypted in place (M11.2). The
      password must be supplied to every connection that reopens
      this table afterwards.
    </p>
    <div class="form-row">
      <label>Password</label>
      <input id="enc-pw" type="password">
    </div>
    <div class="toolbar" style="margin-top:14px">
      <button class="btn" id="enc-go">Encrypt</button>
      <button class="btn btn-secondary" id="enc-cancel">Cancel</button>
      <span id="enc-status"></span>
    </div>
  </div>
</div>
<script>
const $ = id => document.getElementById(id);
const esc = s => (""+s)
  .replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;")
  .replace(/"/g,"&quot;");

// ---- studio.web.0.9 — i18n -----------------------------------------
const I18N = {
  en: { tables: "Tables", server: "Server", info: "Info",
        browse: "Browse", structure: "Structure", insert: "Insert",
        sql: "SQL", sessions: "Sessions", dict: "Dict",
        browse_pick: "Select a table on the left." },
  es: { tables: "Tablas", server: "Servidor", info: "Info",
        browse: "Explorar", structure: "Estructura", insert: "Insertar",
        sql: "SQL", sessions: "Sesiones", dict: "Dicc",
        browse_pick: "Elige una tabla a la izquierda." },
  pt: { tables: "Tabelas", server: "Servidor", info: "Info",
        browse: "Procurar", structure: "Estrutura", insert: "Inserir",
        sql: "SQL", sessions: "Sessões", dict: "Dic",
        browse_pick: "Escolha uma tabela à esquerda." }
};
const LANG_KEY = "openads-studio.lang";
function applyLang(l) {
  if (!I18N[l]) l = "en";
  document.documentElement.setAttribute("lang", l);
  document.querySelectorAll("[data-i18n]").forEach(el => {
    const k = el.dataset.i18n;
    if (I18N[l][k]) el.textContent = I18N[l][k];
  });
  $("lang-pick").value = l;
}

let state = { table: null, schema: null,
              browseOffset: 0, browseLimit: 50 };

function setStatus(s, klass) {
  const el = $("status"); el.textContent = s;
  el.className = "status" + (klass ? " " + klass : "");
}
async function api(url, opts) {
  const r = await fetch(url, opts);
  const txt = await r.text();
  let body = null;
  try { body = JSON.parse(txt); } catch {}
  if (!r.ok || (body && body.error)) {
    throw new Error((body && body.error) || `${r.status} ${r.statusText}`);
  }
  return body;
}

function showTab(tab) {
  ["browse","structure","insert","sql","server","sessions","dd"].forEach(t => {
    $("pane-" + t).classList.toggle("hidden", t !== tab);
  });
  document.querySelectorAll("nav.tabs button").forEach(b =>
    b.classList.toggle("active", b.dataset.tab === tab));
  if (tab === "structure" && state.table) loadStructure();
  if (tab === "insert"    && state.table) loadInsertForm();
  if (tab === "browse"    && state.table) loadBrowse();
  if (tab === "server")                   loadServerInfo();
  if (tab === "sessions") {
    loadSessions();
    startSessionsAutoRefresh();
  } else {
    stopSessionsAutoRefresh();
  }
  if (tab === "dd") loadDicts();
}

async function loadTables() {
  try {
    const data = await api("/api/tables");
    setStatus(`${data.tables.length} tables · ${data.data_dir}`);
    $("tables").innerHTML = data.tables.map(t =>
      `<li data-name="${esc(t)}">${esc(t)}</li>`).join("");
    document.querySelectorAll("aside li[data-name]").forEach(li =>
      li.addEventListener("click", () => selectTable(li.dataset.name)));
  } catch (e) { setStatus("offline: " + e.message); }
}

function selectTable(name) {
  state.table = name;
  state.browseOffset = 0;
  document.querySelectorAll("aside li").forEach(li =>
    li.classList.toggle("active", li.dataset.name === name));
  showTab("browse");
  closeNav();
}

async function loadBrowse() {
  const t = state.table;
  $("browse-title").textContent = `Table: ${t}`;
  try {
    let url = `/api/tables/${encodeURIComponent(t)}` +
      `/rows?offset=${state.browseOffset}&limit=${state.browseLimit}`;
    // studio.web.0.18 — forward the AOF cond on every paged fetch
    // so Next / Prev keep walking the same Rushmore-filtered set.
    if (state.aofCond && state.aofCond.length > 0) {
      url += `&aof=${encodeURIComponent(state.aofCond)}`;
    }
    const data = await api(url);
    state.lastBrowseData = data;
    state.browseSort = state.browseSort || { col: -1, dir: 1 };
    paintAofBadge(data);
    renderBrowseGrid();
    $("browse-pager").innerHTML = `
      <button class="btn btn-secondary" id="prev"
              ${state.browseOffset === 0 ? "disabled" : ""}>‹ Prev</button>
      <span>${state.browseOffset + 1}–${state.browseOffset + (data.rows||[]).length}
            of ${data.total}</span>
      <button class="btn btn-secondary" id="next"
              ${state.browseOffset + (data.rows||[]).length >= data.total ?
                "disabled" : ""}>Next ›</button>`;
    $("prev")?.addEventListener("click", () => {
      state.browseOffset = Math.max(0, state.browseOffset - state.browseLimit);
      loadBrowse();
    });
    $("next")?.addEventListener("click", () => {
      state.browseOffset += state.browseLimit; loadBrowse();
    });
  } catch (e) {
    $("browse-grid").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

function renderBrowseGrid() {
  const data = state.lastBrowseData;
  if (!data) return;
  if (!data.rows || data.rows.length === 0) {
    $("browse-grid").innerHTML = `<div class="empty">No rows.</div>`;
    return;
  }
  const cols = data.cols;
  // Apply filter (case-insensitive substring across all cells).
  const filterTxt = ($("browse-filter").value || "").toLowerCase().trim();
  let rows = data.rows.map((r, idx) => ({ orig: idx, r }));
  if (filterTxt) {
    rows = rows.filter(({ r }) =>
      r.slice(1).some(v => (""+v).toLowerCase().includes(filterTxt)));
  }
  // Apply sort.
  const sort = state.browseSort;
  if (sort.col >= 0) {
    const idx = sort.col;
    const numeric = rows.length > 0 &&
      !isNaN(parseFloat((rows[0].r.slice(1)[idx] || "").trim()));
    rows.sort((a, b) => {
      const av = a.r.slice(1)[idx], bv = b.r.slice(1)[idx];
      let c;
      if (numeric) c = parseFloat(av) - parseFloat(bv);
      else c = (""+av).localeCompare(""+bv);
      return sort.dir * (isNaN(c) ? 0 : c);
    });
  }
  state.browseSelection = state.browseSelection || new Set();
  const tbody = rows.map(({ r, orig }) => {
    const meta = r[0];
    const cells = r.slice(1);
    const cls = meta._deleted ? "deleted" : "";
    const checked = state.browseSelection.has(meta._recno) ? "checked" : "";
    return `<tr clas)OPENADS_SPA";
inline constexpr const char kSpaPart3[] = R"OPENADS_SPA(s="${cls}">
      <td class="sel-col"><input type="checkbox"
            data-sel="${meta._recno}" ${checked}></td>
      <td>${meta._recno}</td>
      ${cells.map((v, ci) => `<td class="cell"
          data-row="${orig}" data-col="${ci}">${esc(v)}</td>`).join("")}
      <td><button class="btn btn-secondary" data-edit="${meta._recno}">Edit</button>
          <button class="btn btn-danger" data-delete="${meta._recno}"
                  data-deleted="${meta._deleted}">${
            meta._deleted ? "Recall" : "Delete"
          }</button></td>
    </tr>`;
  }).join("");
  const arrow = (i) => sort.col === i
    ? (sort.dir > 0 ? " ▲" : " ▼") : "";
  $("browse-grid").innerHTML = `<table>
    <thead><tr>
      <th class="sel-col"><input type="checkbox" id="sel-all"></th>
      <th>recno</th>${
      cols.map((c, i) =>
        `<th class="sort" data-col="${i}" style="cursor:pointer">${esc(c)}${arrow(i)}</th>`)
        .join("")}<th>actions</th></tr></thead>
    <tbody>${tbody}</tbody></table>`;
  document.querySelectorAll("[data-sel]").forEach(cb =>
    cb.addEventListener("change", e => {
      const rn = +cb.dataset.sel;
      if (e.target.checked) state.browseSelection.add(rn);
      else                  state.browseSelection.delete(rn);
      updateBulkBar();
    }));
  $("sel-all").addEventListener("change", e => {
    document.querySelectorAll("[data-sel]").forEach(cb => {
      cb.checked = e.target.checked;
      const rn = +cb.dataset.sel;
      if (e.target.checked) state.browseSelection.add(rn);
      else                  state.browseSelection.delete(rn);
    });
    updateBulkBar();
  });
  updateBulkBar();
  document.querySelectorAll("th.sort").forEach(th =>
    th.addEventListener("click", () => {
      const c = +th.dataset.col;
      if (state.browseSort.col === c) state.browseSort.dir = -state.browseSort.dir;
      else state.browseSort = { col: c, dir: 1 };
      renderBrowseGrid();
    }));
  document.querySelectorAll("[data-edit]").forEach(b =>
    b.addEventListener("click", () => editRow(+b.dataset.edit)));
  document.querySelectorAll("[data-delete]").forEach(b =>
    b.addEventListener("click", () =>
      deleteRow(+b.dataset.delete, b.dataset.deleted === "true")));
  document.querySelectorAll("td.cell").forEach(td => {
    td.addEventListener("click", () => {
      const r = +td.dataset.row, c = +td.dataset.col;
      const meta = data.rows[r][0];
      const v = data.rows[r].slice(1)[c];
      openCellModal(`recno ${meta._recno} / ${cols[c]}`, v);
    });
  });
}

async function ensureSchema() {
  if (state.schema && state.schema.table === state.table) return state.schema;
  state.schema = await api(
    `/api/tables/${encodeURIComponent(state.table)}/schema`);
  return state.schema;
}

async function loadStructure() {
  try {
    const s = await ensureSchema();
    $("structure-body").innerHTML = `
      <div class="kv">
        <div>Table</div><div>${esc(s.table)}</div>
        <div>Records</div><div>${s.record_count}</div>
        <div>File size</div><div>${s.file_bytes} bytes</div>
      </div>
      <div class="toolbar" style="margin-top:14px">
        <button class="btn btn-secondary" id="btn-reindex">Reindex</button>
        <button class="btn btn-secondary" id="btn-pack">Pack</button>
        <button class="btn btn-danger"    id="btn-zap">Zap</button>
        <button class="btn btn-secondary" id="btn-download">Download</button>
        <button class="btn"               id="btn-encrypt">Encrypt…</button>
        <button class="btn btn-danger"    id="btn-drop">Drop table</button>
        <span id="struct-status"></span>
      </div>
      <h3 style="margin-top:18px;font-size:15px;opacity:0.85">Columns</h3>
      <table><thead><tr><th>#</th><th>Name</th><th>Type</th><th>Length</th><th>Decimals</th></tr></thead>
      <tbody>${s.columns.map((c,i) => `<tr>
        <td>${i+1}</td><td>${esc(c.name)}</td>
        <td>${esc(c.type_name||c.type)}</td>
        <td>${c.length}</td><td>${c.decimals}</td></tr>`).join("")}</tbody></table>

      <h3 style="margin-top:24px;font-size:15px;opacity:0.85">Create index</h3>
      <form id="ci-form" class="form-row" style="flex-wrap:wrap">
        <input name="tag"  placeholder="tag (e.g. NAME_TAG)" required>
        <input name="expr" placeholder="expression (e.g. UPPER(NAME))" required>
        <label style="font-size:14px;opacity:0.85">
          <input type="checkbox" name="descending"> DESC</label>
        <label style="font-size:14px;opacity:0.85">
          <input type="checkbox" name="unique"> UNIQUE</label>
        <button type="submit" class="btn">+ Index</button>
      </form>`;
    $("btn-drop").addEventListener("click", dropCurrentTable);
    $("btn-encrypt").addEventListener("click", openEncryptModal);
    $("btn-reindex").addEventListener("click", () => maintenanceOp("reindex"));
    $("btn-pack").addEventListener("click",    () => maintenanceOp("pack"));
    $("btn-zap").addEventListener("click",     () => maintenanceOp("zap"));
    $("btn-download").addEventListener("click", () => {
      const a = document.createElement("a");
      a.href = `/api/tables/${encodeURIComponent(state.table)}/download`;
      a.download = state.table;
      document.body.appendChild(a); a.click(); a.remove();
    });
    // studio.web.0.7 — render sidecar files for this table.
    try {
      const sc = await api(
        `/api/tables/${encodeURIComponent(state.table)}/sidecars`);
      if (sc.sidecars && sc.sidecars.length) {
        const rows = sc.sidecars.map(s =>
          `<tr><td>${esc(s.file)}</td><td>${esc(s.kind)}</td>
           <td>${s.bytes}</td></tr>`).join("");
        $("structure-body").insertAdjacentHTML("beforeend", `
          <h3 style="margin-top:24px;font-size:15px;opacity:0.85">Companion files</h3>
          <table><thead><tr><th>file</th><th>kind</th><th>bytes</th></tr></thead>
          <tbody>${rows}</tbody></table>`);
      }
    } catch (e) { /* non-fatal */ }
    $("ci-form").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      const body = {
        tag:  fd.get("tag"),
        expr: fd.get("expr"),
        descending: fd.get("descending") === "on",
        unique:     fd.get("unique")     === "on"
      };
      try {
        const r = await api(
          `/api/tables/${encodeURIComponent(state.table)}/index`,
          {method:"POST",
           headers:{"content-type":"application/json"},
           body: JSON.stringify(body)});
        $("struct-status").textContent = "ok: " + r.sql;
        $("struct-status").className = "ok";
        e.target.reset();
      } catch (err) {
        $("struct-status").textContent = err.message;
        $("struct-status").className = "err";
      }
    });
  } catch (e) {
    $("structure-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

async function maintenanceOp(op) {
  const t = state.table;
  if (op === "zap" && !confirm(
        `Zap ${t}?  All records are removed. Indexes are kept but emptied.`)) return;
  $("struct-status").textContent = op + "…"; $("struct-status").className = "";
  try {
    await api(`/api/tables/${encodeURIComponent(t)}/${op}`, {method:"POST"});
    $("struct-status").textContent = op + " ok";
    $("struct-status").className = "ok";
    state.schema = null;
    if (op === "zap" || op === "pack") loadStructure();
  } catch (e) {
    $("struct-status").textContent = e.message;
    $("struct-status").className = "err";
  }
}

async function dropCurrentTable() {
  const t = state.table;
  if (!confirm(`Drop table ${t}?  This deletes the .dbf + every sidecar (.cdx/.fpt/.dbt/.lck) on disk.`)) return;
  try {
    await api(`/api/tables/${encodeURIComponent(t)}`, {method:"DELETE"});
    state.table = null; state.schema = null;
    await loadTables();
    showTab("server");
  } catch (e) { alert("DROP failed: " + e.message); }
}

function openEncryptModal() {
  $("enc-pw").value = "";
  $("enc-status").textContent = "";
  $("modal-encrypt").classList.add("show");
}
async function runEncrypt() {
  const pw = $("enc-pw").value;
  if (!pw) { $("enc-status").textContent = "password required";
             $("enc-status").className = "err"; return; }
  try {
    await api(`/api/tables/${encodeURIComponent(state.table)}/encrypt`,
      { method: "POST",
        headers: {"content-type":"application/json"},
        body: JSON.stringify({password: pw}) });
    $("modal-encrypt").classList.remove("show");
    state.schema = null;
    loadStructure();
  } catch (e) {
    $("enc-status").textContent = e.message;
    $("enc-status").className = "err";
  }
}

// CREATE TABLE wizard
function openCreateModal() {
  $("ct-name").value = "";
  $("ct-cols").innerHTML = "";
  $("ct-status").textContent = "";
  addCreateCol(); addCreateCol();
  $("modal-create").classList.add("show");
}
function addCreateCol() {
  const div = document.createElement("div");
  div.className = "col-row";
  div.innerHTML = `
    <input class="ct-cn" type="text" placeholder="COL_NAME">
    <select class="ct-ct">
      <option value="C">C (char)</option>
      <option value="N">N (numeric)</option>
      <option value="L">L (logical)</option>
      <option value="D">D (date)</option>
      <option value="M">M (memo)</option>
    </select>
    <input class="ct-cl" type="number" value="10" min="1">
    <input class="ct-cd" type="number" value="0" min="0">
    <button title="remove">×</button>`;
  div.querySelector("button").addEventListener("click", () => div.remove());
  $("ct-cols").appendChild(div);
}
async function runCreateTable() {
  const name = $("ct-name").value.trim();
  if (!name) { $("ct-status").textContent = "table name required";
               $("ct-status").className = "err"; return; }
  const cols = [...$("ct-cols").children].map(div => ({
    name:     div.querySelector(".ct-cn").value.trim(),
    type:     div.querySelector(".ct-ct").value,
    length:   parseInt(div.querySelector(".ct-cl").value, 10) || 0,
    decimals: parseInt(div.querySelector(".ct-cd").value, 10) || 0
  })).filter(c => c.name);
  if (cols.length === 0) { $("ct-status").textContent = "at least one column";
                           $("ct-status").className = "err"; return; }
  try {
    await api("/api/tables", {
      method: "POST",
      headers: {"content-type":"application/json"},
      body: JSON.stringify({name, columns: cols})});
    $("modal-create").classList.remove("show");
    await loadTables();
    selectTable(name);
  } catch (e) {
    $("ct-status").textContent = e.message;
    $("ct-status").className = "err";
  }
}

// SQL history (persistent localStorage). Up/Down arrows recall.
const SQL_HIST_KEY = "openads-studio.sql.history";
function loadHistory() {
  try { return JSON.parse(localStorage.getItem(SQL_HIST_KEY) || "[]"); }
  catch { return []; }
}
function saveHistory(arr) {
  localStorage.setItem(SQL_HIST_KEY,
                       JSON.stringify(arr.slice(-100)));
}
let sqlHistIdx = -1;
function pushHistory(sql) {
  const h = loadHistory();
  if (h.length === 0 || h[h.length-1] !== sql) h.push(sql);
  saveHistory(h);
  sqlHistIdx = h.length;
}
function recallHistory(delta) {
  const h = loadHistory();
  if (h.length === 0) return;
  sqlHistIdx = Math.max(0, Math.min(h.length, sqlHistIdx + delta));
  $("sql").value = sqlHistIdx < h.length ? h[sqlHistIdx] : "";
}

async function loadInsertForm() {
  try {
    const s = await ensureSchema();
    const body = `<form id="insert-form">
      ${s.columns.map(c =>
        `<div class="form-row">
          <label>${esc(c.name)} <small>(${c.type}/${c.length})</small></label>
          <input name="${esc(c.name)}" type="text">
        </div>`).join("")}
      <div class="toolbar" style="margin-top:14px">
        <button type="submit" class="btn">Append record</button>
        <span id="insert-status"></span>
      </div></form>`;
    $("insert-body").innerHTML = body;
    $("insert-form").addEventListener("submit", async e => {
      e.pr)OPENADS_SPA";
inline constexpr const char kSpaPart4[] = R"OPENADS_SPA(eventDefault();
      const fd = new FormData(e.target);
      const obj = {};
      for (const [k,v] of fd.entries()) obj[k] = v;
      try {
        const r = await api(
          `/api/tables/${encodeURIComponent(state.table)}/insert`,
          { method: "POST",
            headers: {"content-type":"application/json"},
            body: JSON.stringify(obj) });
        $("insert-status").textContent = `appended recno ${r.recno}`;
        $("insert-status").className = "ok";
        e.target.reset();
        state.schema = null;
      } catch (err) {
        $("insert-status").textContent = err.message;
        $("insert-status").className = "err";
      }
    });
  } catch (e) {
    $("insert-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

async function editRow(recno) {
  try {
    const s = await ensureSchema();
    const data = await api(
      `/api/tables/${encodeURIComponent(state.table)}` +
      `/rows?offset=${recno - 1}&limit=1`);
    const row = data.rows[0];
    const cells = row.slice(1);
    const lines = s.columns.map((c, i) =>
      `<div class="form-row">
        <label>${esc(c.name)} <small>(${c.type}/${c.length})</small></label>
        <input name="${esc(c.name)}" type="text"
               value="${esc(cells[i] || "")}"></div>`).join("");
    const html = `<h3 style="font-size:13px;margin:0 0 10px">
      Edit recno ${recno} of ${esc(state.table)}</h3>
      <form id="edit-form">${lines}
      <div class="toolbar" style="margin-top:14px">
        <button type="submit" class="btn">Save</button>
        <button type="button" class="btn btn-secondary" id="edit-cancel">Cancel</button>
        <span id="edit-status"></span>
      </div></form>`;
    $("browse-grid").innerHTML = html;
    $("edit-cancel").addEventListener("click", loadBrowse);
    $("edit-form").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      const obj = {};
      for (const [k,v] of fd.entries()) obj[k] = v;
      try {
        await api(`/api/tables/${encodeURIComponent(state.table)}` +
          `/update?recno=${recno}`,
          { method: "POST",
            headers: {"content-type":"application/json"},
            body: JSON.stringify(obj) });
        loadBrowse();
      } catch (err) {
        $("edit-status").textContent = err.message;
        $("edit-status").className = "err";
      }
    });
  } catch (e) {
    $("browse-grid").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

async function deleteRow(recno, currentlyDeleted) {
  const verb = currentlyDeleted ? "recall" : "delete";
  if (!confirm(`${verb} recno ${recno}?`)) return;
  try {
    await api(`/api/tables/${encodeURIComponent(state.table)}` +
      `/delete?recno=${recno}&recall=${currentlyDeleted ? "1" : "0"}`,
      { method: "POST" });
    loadBrowse();
  } catch (e) { alert(e.message); }
}

async function runSql() {
  const sql = $("sql").value.trim();
  if (!sql) return;
  pushHistory(sql);
  $("sql-status").textContent = "running…"; $("sql-status").className = "";
  try {
    const data = await api("/api/sql", {
      method: "POST",
      headers: {"content-type": "application/json"},
      body: JSON.stringify({sql, limit: 500})});
    if (!data.rows || data.rows.length === 0) {
      $("sql-result").innerHTML = `<div class="empty">no rows / OK</div>`;
    } else {
      $("sql-result").innerHTML = `<table>
        <thead><tr>${data.cols.map(c => `<th>${esc(c)}</th>`).join("")}</tr></thead>
        <tbody>${data.rows.map(r => `<tr>${
          r.map(v => `<td>${esc(v)}</td>`).join("")}</tr>`).join("")}</tbody></table>`;
    }
    $("sql-status").textContent = `${data.rows_returned} rows`;
    $("sql-status").className = "ok";
    state.lastSqlResult = data;
  } catch (e) {
    $("sql-status").textContent = e.message;
    $("sql-status").className = "err";
  }
}

function exportCsv() {
  const data = state.lastSqlResult;
  if (!data || !data.rows) return alert("Run a query first.");
  const csv = [data.cols.join(","),
    ...data.rows.map(r => r.map(v => {
      const s = (""+v).replace(/"/g,'""');
      return /[",\n]/.test(s) ? `"${s}"` : s;
    }).join(","))].join("\n");
  downloadBlob(csv, "openads-result.csv", "text/csv");
}
function exportJson() {
  const data = state.lastSqlResult;
  if (!data || !data.rows) return alert("Run a query first.");
  const arr = data.rows.map(r => {
    const o = {};
    data.cols.forEach((c, i) => o[c] = r[i]);
    return o;
  });
  downloadBlob(JSON.stringify(arr, null, 2),
               "openads-result.json", "application/json");
}
function downloadBlob(content, name, mime) {
  const blob = new Blob([content], {type: mime});
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}

function fmtBytes(n) {
  if (n == null) return "";
  if (n < 1024) return n + " B";
  if (n < 1024*1024) return (n/1024).toFixed(1) + " KB";
  if (n < 1024*1024*1024) return (n/1024/1024).toFixed(2) + " MB";
  return (n/1024/1024/1024).toFixed(2) + " GB";
}
// Render every scalar/usage entry without treating unavailable as zero.
function renderServerHealth(h) {
  if (!h || h.schema_version !== 1) throw new Error("Unsupported health schema");
  const val = v => v == null ? "Unavailable / not measured" : esc(String(v));
  let html = `<h3>Server health</h3><div class="kv">`;
  for (const [key, value] of Object.entries(h)) {
    if (key === "semantics") continue;
    const label = key.replaceAll("_", " ");
    if (value && typeof value === "object") {
      html += `<div>${esc(label)}</div><div>Current: ${val(value.current)} | Max used: ${val(value.max_used)} | Rejected: ${val(value.rejected)}</div>`;
    } else html += `<div>${esc(label)}</div><div>${key === "max_sessions" && value === 0 ? "Unlimited (0)" : val(value)}</div>`;
  }
  html += `</div><details><summary>Count meanings and raw JSON</summary><pre style="white-space:pre-wrap;overflow-wrap:anywhere">${esc(JSON.stringify(h, null, 2))}</pre></details>`;
  return html;
}
$("server-refresh").addEventListener("click", loadServerInfo);
async function loadServerHealth() {
  try { $("server-health").innerHTML = renderServerHealth(await api("/api/server/health")); $("server-health").classList.remove("empty"); }
  catch (e) { $("server-health").innerHTML = `<div class="err">${esc(e.message)}</div>`; }
}

async function loadServerInfo() {
  loadServerHealth();
  try {
    const d = await api("/api/server/info");
    const osIcon = ({Windows:"🪟", Linux:"🐧", macOS:"🍎"}[d.os]
                     || "⚠");
    $("server-body").innerHTML = `<div class="kv">
      <div>Engine</div><div>${esc(d.engine)} ${esc(d.version || "")}</div>
      <div>Host OS</div><div>${osIcon} ${esc(d.os || "?")}
        ${d.arch ? "(" + esc(d.arch) + ")" : ""}</div>
      <div>Compiler</div><div>${esc(d.compiler || "")}</div>
      <div>HTTP module</div><div>${esc(d.http || "")}</div>
      <div>Server name</div><div>${esc(d.server_name || "")}</div>
      <div>Data dir</div><div>${esc(d.data_dir)}</div>
      <div>Tables</div><div>${d.tables.length}</div>
      <div>Dictionaries</div><div>${d.dict_count || 0}</div>
      <div>DBF total</div><div>${fmtBytes(d.dbf_bytes || 0)}</div>
      <div>Sidecar total</div><div>${fmtBytes(d.sidecar_bytes || 0)}</div>
      <div>On-disk total</div><div>${fmtBytes(d.total_bytes || 0)}</div>
      </div>
      <div class="toolbar" style="margin-top:18px">
        <a class="btn" href="/api/server/backup" download>
          ⇩ Download backup (.zip)
        </a>
      </div>`;
  } catch (e) {
    $("server-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

let sessionsTimer = null;
function startSessionsAutoRefresh() {
  stopSessionsAutoRefresh();
  if (!$("sessions-auto").checked) return;
  sessionsTimer = setInterval(loadSessions, 3000);
}
function stopSessionsAutoRefresh() {
  if (sessionsTimer) { clearInterval(sessionsTimer); sessionsTimer = null; }
}
function fmtDuration(s) {
  if (s < 60)    return s + "s";
  if (s < 3600)  return Math.floor(s/60) + "m " + (s%60) + "s";
  return Math.floor(s/3600) + "h " + Math.floor((s%3600)/60) + "m";
}
// ---- studio.web.0.5 — Data Dictionary tab ---------------------------
let ddState = { current: null };

async function loadDicts() {
  try {
    const data = await api("/api/dd");
    const sel = $("dd-pick");
    sel.innerHTML = '<option value="">— pick a dictionary —</option>'
      + data.dicts.map(d =>
          `<option value="${esc(d.name)}">${esc(d.name)}` +
          ` (${d.bytes} B)</option>`).join("");
    if (ddState.current && data.dicts.find(d => d.name === ddState.current)) {
      sel.value = ddState.current;
      loadDictView(ddState.current);
    } else {
      $("dd-body").innerHTML =
        `<div class="empty">${data.dicts.length} dictionary file(s) in `
        + `${esc(data.data_dir)}.</div>`;
    }
  } catch (e) {
    $("dd-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}
async function loadDictView(name) {
  ddState.current = name;
  $("dd-status").textContent = "loading…"; $("dd-status").className = "";
  try {
    const d = await api(`/api/dd/${encodeURIComponent(name)}`);
    const tblRows = (d.tables||[]).map(t =>
      `<tr><td>${esc(t.alias)}</td><td>${esc(t.path)}</td>
       <td><button class="btn btn-danger" data-rm-tbl="${esc(t.alias)}">Remove</button></td></tr>`).join("");
    const usrRows = (d.users||[]).map(u =>
      `<tr><td>${esc(u)}</td>
       <td><button class="btn btn-danger" data-rm-usr="${esc(u)}">Remove</button></td></tr>`).join("");
    const ixRows  = (d.indexes||[]).map(i =>
      `<tr><td>${esc(i.table)}</td><td>${esc(i.path)}</td><td>${esc(i.comment)}</td></tr>`).join("");
    const lkRows  = (d.links||[]).map(l =>
      `<tr><td>${esc(l.alias)}</td><td>${esc(l.path)}</td><td>${esc(l.user)}</td></tr>`).join("");
    const riRows  = (d.ri||[]).map(r =>
      `<tr><td>${esc(r.name)}</td><td>${esc(r.parent)}</td><td>${esc(r.child)}</td>
       <td>${esc(r.tag)}</td><td>${esc(r.update_opt)}</td><td>${esc(r.delete_opt)}</td>
       <td>${esc(r.fail_table)}</td></tr>`).join("");
    const propRows = (d.db_props||[]).map(p =>
      `<tr><td>${esc(p.key)}</td><td>${esc(p.value)}</td></tr>`).join("");

    $("dd-body").innerHTML = `
      <h3 style="font-size:17px;margin:0 0 8px">${esc(d.name)}</h3>
      <p style="opacity:0.7;font-size:14px;margin:0 0 16px">${esc(d.path)}</p>

      <h4 style="font-size:15px;margin:18px 0 6px">Tables</h4>
      <form id="dd-add-tbl" class="form-row" style="margin-bottom:8px">
        <input name="alias" placeholder="alias" required>
        <input name="path"  placeholder="relative.dbf" required>
        <button type="submit" class="btn">+ Add table</button>
      </form>
      ${tblRows ? `<table><thead><tr><th>alias</th><th>path</th><th></th></tr></thead><tbody>${tblRows}</tbody></table>`
                : `<div class="empty">No tables yet.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">Users</h4>
      <form id="dd-add-usr" class="form-row" style="margin-bottom:8px">
        <input name="user" placeholder="username" required>
        <button type="submit" class="btn">+ Add user</button>
      </form>
      ${usrRows ? `<table><thead><tr><th>name</th><th></th></tr></thead><tbody>${usrRows}</tbody></table>`
                : `<div class="empty">No users yet.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">Indexes</h4>
      ${ixRows  ? `<table><thead><tr><th>table</th><th>path</th><th>comment</th></tr></thead><tbody>${ixRows}</tbody></table>`
                : `<div class="empty">No indexes registered.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">Links</h4>
      ${lkRows  ? `<table><thead><tr><th>alias</th><th>path</th><th>user</th></tr></thead><tbody>${lkRows}</tbody></table>`
                : `<div class="empty">No links.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">RI rules</h4>
      ${riRows  ? `<table><thead><tr><th>name</th><th>parent</th><th>child</th><th>tag</th>
                   <th>upd</th><th>del</th><th>fail</th></tr></thead><tbody>${riRows}</tbody></table>`
                : `<div class="empty">No RI rules.</div>`}

      <h4 style="font-size:15px;margin:18px 0 6px">DB properties</h4>
      <form id="dd-add-prop" class="form-row" style="margin-bottom:8px">
        <input name="key"   placeholder="key" required>
        <input name="value" placeholder="value">
        <button type="submit" class="btn">Set</button>
      </form>
      ${propRows ? `<table><thead><tr><th>key</th><th>value</th></tr></thead><tbody>${propRows}</tbody></table>`
                 : `<div class="empty">No DB properties yet.</div>`}
    `;
    $("dd-status").textContent = ""; $("dd-status").className = "";

    $("dd-add-tbl").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      await ddPost(`/api/dd/${encodeURIComponent(name)}/tables`,
                   {alias: fd.get("alias"), path: fd.get("path")});
    });
    $("dd-add-usr").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      await ddPost(`/api/dd/${encodeURIComponent()OPENADS_SPA";
inline constexpr const char kSpaPart5[] = R"OPENADS_SPA(name)}/users`,
                   {user: fd.get("user")});
    });
    $("dd-add-prop").addEventListener("submit", async e => {
      e.preventDefault();
      const fd = new FormData(e.target);
      await ddPost(`/api/dd/${encodeURIComponent(name)}/dbprop`,
                   {key: fd.get("key"), value: fd.get("value") || ""});
    });
    document.querySelectorAll("[data-rm-tbl]").forEach(b =>
      b.addEventListener("click", () => ddDelete(
        `/api/dd/${encodeURIComponent(name)}/tables/${
          encodeURIComponent(b.dataset.rmTbl)}`)));
    document.querySelectorAll("[data-rm-usr]").forEach(b =>
      b.addEventListener("click", () => ddDelete(
        `/api/dd/${encodeURIComponent(name)}/users/${
          encodeURIComponent(b.dataset.rmUsr)}`)));
  } catch (e) {
    $("dd-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}
async function ddPost(url, body) {
  try {
    await api(url, {method:"POST",
                    headers:{"content-type":"application/json"},
                    body: JSON.stringify(body)});
    if (ddState.current) loadDictView(ddState.current);
  } catch (e) {
    $("dd-status").textContent = e.message;
    $("dd-status").className = "err";
  }
}
async function ddDelete(url) {
  if (!confirm("Remove?")) return;
  try {
    await api(url, {method:"DELETE"});
    if (ddState.current) loadDictView(ddState.current);
  } catch (e) {
    $("dd-status").textContent = e.message;
    $("dd-status").className = "err";
  }
}

async function loadSessions() {
  try {
    const data = await api("/api/server/sessions");
    $("sessions-status").textContent =
      `${data.count} active`;
    $("sessions-status").className = "ok";
    if (!data.sessions || data.sessions.length === 0) {
      $("sessions-body").innerHTML =
        `<div class="empty">No active wire sessions.</div>`;
      return;
    }
    const rows = data.sessions
      .sort((a,b) => a.connected_secs - b.connected_secs)
      .map(s => `<tr>
        <td>${s.id}</td>
        <td>${esc(s.peer_ip)}:${s.peer_port}</td>
        <td>${esc(s.user || "<i style='opacity:0.5'>(none)</i>")}</td>
        <td>${esc(s.data_dir)}</td>
        <td>${fmtDuration(s.connected_secs)}</td>
        <td>${fmtDuration(s.idle_secs)}</td>
        <td>${s.frames_in}</td>
        <td>${s.frames_out}</td>
        <td>${s.open_tables}</td>
        <td><button class="btn btn-danger" data-kill="${s.id}">Kill</button></td>
        </tr>`).join("");
    $("sessions-body").innerHTML = `<table>
      <thead><tr>
        <th>ID</th><th>Peer</th><th>User</th><th>Data dir</th>
        <th>Connected</th><th>Idle</th>
        <th>frames in</th><th>frames out</th><th>open</th>
        <th></th>
      </tr></thead><tbody>${rows}</tbody></table>`;
    document.querySelectorAll("[data-kill]").forEach(b =>
      b.addEventListener("click", async () => {
        if (!confirm(`Kill session ${b.dataset.kill}?`)) return;
        try {
          await api(`/api/server/sessions/${b.dataset.kill}/kill`,
                    {method:"POST"});
          loadSessions();
        } catch (e) { alert(e.message); }
      }));
  } catch (e) {
    $("sessions-body").innerHTML = `<div class="err">${esc(e.message)}</div>`;
  }
}

document.querySelectorAll("nav.tabs button").forEach(b =>
  b.addEventListener("click", () => showTab(b.dataset.tab)));
$("server-link").addEventListener("click", () => { showTab("server"); closeNav(); });

/* ---- mobile nav drawer (studio responsive) ---- */
function closeNav() {
  document.querySelector("aside").classList.remove("open");
  $("nav-backdrop").classList.remove("show");
}
function toggleNav() {
  const open = document.querySelector("aside").classList.toggle("open");
  $("nav-backdrop").classList.toggle("show", open);
}
$("nav-toggle").addEventListener("click", toggleNav);
$("nav-backdrop").addEventListener("click", closeNav);
$("sql-run").addEventListener("click", runSql);
$("sql-export").addEventListener("click", exportCsv);
$("sql-export-json").addEventListener("click", exportJson);
$("sql").addEventListener("keydown", e => {
  if ((e.ctrlKey || e.metaKey) && e.key === "Enter") { runSql(); return; }
  // History navigation only when caret is at first/last line so we
  // don't break in-textarea cursor movement.
  if (e.key === "ArrowUp" && e.ctrlKey)   { e.preventDefault(); recallHistory(-1); }
  if (e.key === "ArrowDown" && e.ctrlKey) { e.preventDefault(); recallHistory( 1); }
});
// studio.web.0.8 — theme toggle (dark default; light persists via
// localStorage).
const THEME_KEY = "openads-studio.theme";
function applyTheme(t) {
  document.body.classList.toggle("light", t === "light");
  $("theme-toggle").textContent = t === "light" ? "☀" : "🌙";
}
applyTheme(localStorage.getItem(THEME_KEY) || "dark");
$("theme-toggle").addEventListener("click", () => {
  const cur = document.body.classList.contains("light") ? "light" : "dark";
  const nxt = cur === "light" ? "dark" : "light";
  localStorage.setItem(THEME_KEY, nxt);
  applyTheme(nxt);
});

applyLang(localStorage.getItem(LANG_KEY) || "en");
$("lang-pick").addEventListener("change", e => {
  localStorage.setItem(LANG_KEY, e.target.value);
  applyLang(e.target.value);
});

$("browse-filter").addEventListener("input", () => renderBrowseGrid());

function updateBulkBar() {
  const n = state.browseSelection ? state.browseSelection.size : 0;
  $("browse-bulk-del").style.display = n > 0 ? "" : "none";
  $("browse-bulk-count").textContent = n > 0 ? `${n} selected` : "";
}
$("browse-bulk-del").addEventListener("click", async () => {
  const sel = [...(state.browseSelection || [])];
  if (sel.length === 0) return;
  if (!confirm(`Mark ${sel.length} record(s) deleted?`)) return;
  for (const rn of sel) {
    try {
      await api(`/api/tables/${encodeURIComponent(state.table)}` +
        `/delete?recno=${rn}`, {method:"POST"});
    } catch (e) { console.warn("delete", rn, e.message); }
  }
  state.browseSelection.clear();
  loadBrowse();
});

// ---- studio.web.0.10 — saved queries (localStorage) ----------------
const SQL_SAVED_KEY = "openads-studio.sql.saved";
function loadSavedQueries() {
  try { return JSON.parse(localStorage.getItem(SQL_SAVED_KEY) || "[]"); }
  catch { return []; }
}
function saveSavedQueries(arr) {
  localStorage.setItem(SQL_SAVED_KEY, JSON.stringify(arr));
}
function refreshSavedDropdown() {
  const arr = loadSavedQueries();
  $("sql-saved").innerHTML = '<option value="">— saved queries —</option>'
    + arr.map(q => `<option value="${esc(q.name)}">${esc(q.name)}</option>`).join("");
  $("sql-saved-del").style.display = "none";
}
$("sql-save").addEventListener("click", () => {
  const sql = $("sql").value.trim();
  if (!sql) return alert("Editor empty.");
  const name = prompt("Save query as:");
  if (!name) return;
  const arr = loadSavedQueries().filter(q => q.name !== name);
  arr.push({ name, sql });
  saveSavedQueries(arr);
  refreshSavedDropdown();
  $("sql-saved").value = name;
});
$("sql-saved").addEventListener("change", e => {
  const name = e.target.value;
  $("sql-saved-del").style.display = name ? "" : "none";
  if (!name) return;
  const q = loadSavedQueries().find(q => q.name === name);
  if (q) { $("sql").value = q.sql; renderSqlHl(); }
});
$("sql-saved-del").addEventListener("click", () => {
  const name = $("sql-saved").value;
  if (!name) return;
  if (!confirm(`Delete saved query "${name}"?`)) return;
  saveSavedQueries(loadSavedQueries().filter(q => q.name !== name));
  refreshSavedDropdown();
});
refreshSavedDropdown();

// ---- studio.web.0.10 — SQL syntax highlight overlay ----------------
const SQL_KEYWORDS = new RegExp(
  "\\b(SELECT|FROM|WHERE|AND|OR|NOT|IN|IS|NULL|LIKE|BETWEEN|" +
  "ORDER|BY|GROUP|HAVING|LIMIT|OFFSET|DISTINCT|UNION|ALL|" +
  "INSERT|INTO|VALUES|UPDATE|SET|DELETE|CREATE|DROP|ALTER|" +
  "TABLE|INDEX|VIEW|UNIQUE|DESCENDING|ASC|DESC|" +
  "INNER|LEFT|RIGHT|FULL|OUTER|JOIN|ON|AS|EXISTS|CASE|WHEN|" +
  "THEN|ELSE|END|COUNT|SUM|AVG|MIN|MAX|UPPER|LOWER|TRIM|" +
  "LTRIM|RTRIM|SUBSTR|CONCAT|REPLACE|DATEDIFF|DATEADD|" +
  "COALESCE|NULLIF|IFNULL|TOP|FETCH|FIRST|NEXT|ROWS|ONLY|" +
  "WITH|RECURSIVE|FILTER|OVER|PARTITION|ROW_NUMBER|RANK|" +
  "DENSE_RANK)\\b", "gi");
function renderSqlHl() {
  const txt = $("sql").value;
  let out = txt
    .replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;");
  // Comments first so we don't recolor inside them.
  out = out.replace(/--[^\n]*/g, m => `<span class="sql-cmt">${m}</span>`);
  // Strings (single + double).
  out = out.replace(/'([^']|'')*'/g, m => `<span class="sql-str">${m}</span>`)
           .replace(/"([^"]|"")*"/g, m => `<span class="sql-str">${m}</span>`);
  // Numeric literals.
  out = out.replace(/\b\d+(\.\d+)?\b/g,
                    m => `<span class="sql-num">${m}</span>`);
  // Keywords last (do not match inside our spans because we
  // already replaced their inner text — keywords live outside
  // string/comment spans).
  out = out.replace(SQL_KEYWORDS, m => `<span class="sql-kw">${m}</span>`);
  $("sql-hl").innerHTML = out + "\n";
}
$("sql").addEventListener("input", renderSqlHl);
$("sql").addEventListener("scroll", () => {
  $("sql-hl").scrollTop  = $("sql").scrollTop;
  $("sql-hl").scrollLeft = $("sql").scrollLeft;
});
renderSqlHl();

$("btn-new-table").addEventListener("click", openCreateModal);
$("btn-refresh-tables").addEventListener("click", loadTables);
$("btn-upload-table").addEventListener("click", () =>
  $("upload-input").click());
$("upload-input").addEventListener("change", async e => {
  const files = e.target.files;
  if (!files || !files.length) return;
  const fd = new FormData();
  for (const f of files) fd.append("file", f, f.name);
  try {
    const r = await fetch("/api/upload", {method:"POST", body: fd});
    const j = await r.json();
    if (!r.ok) throw new Error(j.error || `${r.status}`);
    await loadTables();
    setStatus(`uploaded ${j.count} file(s)`, "ok");
  } catch (err) { setStatus("upload failed: " + err.message, "err"); }
  e.target.value = "";
});
$("ct-add-col").addEventListener("click", addCreateCol);
$("ct-create").addEventListener("click", runCreateTable);
$("ct-cancel").addEventListener("click", () =>
  $("modal-create").classList.remove("show"));
$("enc-go").addEventListener("click", runEncrypt);
$("enc-cancel").addEventListener("click", () =>
  $("modal-encrypt").classList.remove("show"));
$("sessions-refresh").addEventListener("click", loadSessions);
$("sessions-auto").addEventListener("change", startSessionsAutoRefresh);

let cellState = { value: "" };
function renderCell(mode) {
  if (mode === "hex") {
    const v = cellState.value;
    let out = "";
    for (let i = 0; i < v.length; i += 16) {
      const chunk = v.slice(i, i + 16);
      const hex = [...chunk].map(c =>
        c.charCodeAt(0).toString(16).padStart(2, "0")).join(" ");
      const ascii = [...chunk].map(c => {
        const cc = c.charCodeAt(0);
        return (cc >= 32 && cc < 127) ? c : ".";
      }).join("");
      out += i.toString(16).padStart(8, "0") + "  " +
             hex.padEnd(48, " ") + "  " + ascii + "\n";
    }
    $("cell-body").textContent = out;
    $("cell-mode-hex").classList.add("active");
    $("cell-mode-text").classList.remove("active");
  } else {
    $("cell-body").textContent = cellState.value;
    $("cell-mode-text").classList.add("active");
    $("cell-mode-hex").classList.remove("active");
  }
}
function openCellModal(title, value) {
  cellState.value = "" + value;
  $("cell-title").textContent = title;
  $("cell-len").textContent = cellState.value.length;
  // Auto-pick hex if value contains many non-printable bytes
  // (typical of binary memo / picture FPT blocks).
  const nonPrint = [...cellState.value].filter(c => {
    const cc = c.charCodeAt(0);
    return cc < 9 || (cc > 13 && cc < 32) || cc === 127;
  }).length;
  const auto = nonPrint > cellState.value.length * 0.1 ? "hex" : "text";
  renderCell(auto);
  $("modal-cell").classList.add("show");
}
$("cell-mode-text").addEventListener("click", () => renderCell("text"));
$("cell-mode-hex").addEventListener("click",  () => renderCell("hex"));
$("cell-close").addEventListener("click", () =>
  $("modal-cell").classList.remove("show"));
$("cell-copy").addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText($("cell-body").textContent);
    $("cell-copy").textContent = "copied";
    setTimeout(() => $("cell-copy").textContent = "Copy", 900);
)OPENADS_SPA";
inline constexpr const char kSpaPart6[] = R"OPENADS_SPA(  } catch {}
});

$("dd-pick").addEventListener("change", e => {
  if (e.target.value) loadDictView(e.target.value);
});
$("dd-new").addEventListener("click", async () => {
  const n = prompt("New dictionary file name (e.g. orders.add):");
  if (!n) return;
  try {
    await api("/api/dd", {method:"POST",
       headers:{"content-type":"application/json"},
       body: JSON.stringify({name: n})});
    ddState.current = n; loadDicts();
  } catch (e) { alert(e.message); }
});
$("dd-drop").addEventListener("click", async () => {
  const n = ddState.current;
  if (!n) return alert("Pick a dictionary first.");
  if (!confirm(`Drop ${n}?  This deletes the .add file on disk.`)) return;
  try {
    await api(`/api/dd/${encodeURIComponent(n)}`, {method:"DELETE"});
    ddState.current = null; loadDicts();
  } catch (e) { alert(e.message); }
});

// studio.web.0.17 — paint the mode badge once /api/health responds.
// Works alongside loadTables() in applyUrlState() rather than as a
// separate boot path so the SPA only does one extra round-trip on
// startup.
async function paintModeBadge() {
  try {
    const r = await fetch("/api/health");
    if (!r.ok) return;
    const j = await r.json();
    const el = document.getElementById("mode-badge");
    if (!el) return;
    if (j.mode === "localserver") {
      el.textContent = "🏠 LocalServer";
      el.style.background = "rgba(34,197,94,0.30)";  // green tint
      el.style.borderColor = "rgba(34,197,94,0.65)";
      el.style.display = "inline-block";
      el.title = "Studio is running in-process inside ace64.dll / "
               + "ace32.dll (LocalServer). Data dir: "
               + (j.data_dir || ".");
    } else if (j.mode === "remote-server") {
      el.textContent = "🌐 Remote Server";
      el.style.background = "rgba(59,130,246,0.30)"; // blue tint
      el.style.borderColor = "rgba(59,130,246,0.65)";
      el.style.display = "inline-block";
      el.title = "Studio is hosted by openads_serverd alongside the "
               + "OpenADS TCP wire protocol. Data dir: "
               + (j.data_dir || ".");
    }
  } catch (e) { /* ignore — badge stays hidden on failure */ }
}

// studio.web.0.18 — Rushmore-style AOF (Advantage Optimised Filters)
// toolbar wiring. Keeps `state.aofCond` in sync with the input field
// and paints the optimisation-level badge from the response.
function paintAofBadge(data) {
  const el = document.getElementById("browse-aof-badge");
  if (!el) return;
  if (data && data.aof_active) {
    const lvl = data.aof_level || "NONE";
    let bg = "rgba(148,148,148,0.30)";
    let bd = "rgba(148,148,148,0.65)";
    let icon = "⚪";
    if (lvl === "FULL") {
      icon = "🟢"; bg = "rgba(34,197,94,0.30)"; bd = "rgba(34,197,94,0.65)";
    } else if (lvl === "PART") {
      icon = "🟡"; bg = "rgba(234,179,8,0.30)"; bd = "rgba(234,179,8,0.65)";
    }
    el.textContent = `${icon} OptLevel: ${lvl}`;
    el.style.background  = bg;
    el.style.borderColor = bd;
    el.style.display = "inline-block";
    el.title = "AdsGetAOFOptLevel — FULL = every leaf served by an "
             + "index range scan, PART = some leaves served + others "
             + "scanned, NONE = full-record AST evaluation.";
  } else if (data && data.aof_error) {
    el.textContent = "❌ " + data.aof_error;
    el.style.background  = "rgba(239,68,68,0.30)";
    el.style.borderColor = "rgba(239,68,68,0.65)";
    el.style.display = "inline-block";
    el.title = data.aof_error;
  } else {
    el.style.display = "none";
  }
  paintAofHints(data);
}

// studio.web.0.19 — when the AOF didn't reach FULL, suggest a
// CREATE INDEX for each character / memo field referenced by the
// cond that doesn't already have a matching index. V1's index-
// accelerated path only serves char / memo leaves; numeric / date /
// logical leaves stay on the per-record fallback so we don't bother
// suggesting indexes for them — that would be a downstream UX trap.
function paintAofHints(data) {
  const el = document.getElementById("browse-aof-hints");
  if (!el) return;
  el.innerHTML = "";
  if (!data || !data.aof_active || data.aof_level === "FULL") {
    el.style.display = "none";
    return;
  }
  const cond = (state.aofCond || "").toUpperCase();
  if (!cond) { el.style.display = "none"; return; }
  const KEYWORDS = new Set([
    "AND", "OR", "NOT", "BETWEEN", "IN",
    "T", "F", "TRUE", "FALSE", "Y", "N"
  ]);
  // Match bare identifiers; drop quoted strings + keywords.
  const stripped = cond.replace(/'[^']*'|"[^"]*"/g, " ");
  const tokens = stripped.match(/\b[A-Z_][A-Z0-9_]*\b/g) || [];
  const candidates = new Set();
  for (const tok of tokens) {
    if (KEYWORDS.has(tok)) continue;
    candidates.add(tok);
  }
  // ADS_STRING = 4, ADS_MEMO = 5 (V1-indexable char-key types).
  const cols = data.cols || [];
  const types = data.col_types || [];
  const indexable = [];
  cols.forEach((name, i) => {
    if (!candidates.has(name.toUpperCase())) return;
    const t = types[i];
    if (t === 4 || t === 5) indexable.push(name);
  });
  if (indexable.length === 0) {
    el.style.display = "none";
    return;
  }
  el.style.display = "inline-flex";
  el.style.gap = "6px";
  el.style.alignItems = "center";
  el.style.flexWrap = "wrap";
  const lead = document.createElement("span");
  lead.textContent = "💡 lift to FULL:";
  lead.style.color = "var(--muted)";
  el.appendChild(lead);
  for (const f of indexable) {
    const btn = document.createElement("button");
    btn.className = "btn btn-secondary";
    btn.textContent = "Create index on " + f;
    btn.title = "Run CREATE INDEX " + f + "_IDX ON " + state.table +
                " (" + f + ") and re-apply the AOF.";
    btn.style.fontSize = "12px";
    btn.style.padding = "3px 8px";
    btn.addEventListener("click", async () => {
      btn.disabled = true;
      btn.textContent = "Creating " + f + "_IDX…";
      try {
        const sql = "CREATE INDEX " + f + "_IDX ON " +
                    state.table + " (" + f + ")";
        const r = await fetch("/api/sql", {
          method: "POST",
          headers: {"content-type": "application/json"},
          body: JSON.stringify({sql, limit: 1})
        });
        if (!r.ok) {
          const j = await r.json().catch(() => ({}));
          alert("CREATE INDEX failed: " + (j.error || r.status));
          btn.disabled = false;
          btn.textContent = "Create index on " + f;
          return;
        }
        // Re-apply the AOF so the OptLevel badge refreshes.
        await loadBrowse();
      } catch (e) {
        alert("CREATE INDEX failed: " + e.message);
        btn.disabled = false;
        btn.textContent = "Create index on " + f;
      }
    });
    el.appendChild(btn);
  }
}

document.getElementById("browse-aof-apply")?.addEventListener("click", () => {
  const v = (document.getElementById("browse-aof").value || "").trim();
  state.aofCond = v;
  state.browseOffset = 0;
  if (state.table) loadBrowse();
});
document.getElementById("browse-aof-clear")?.addEventListener("click", () => {
  document.getElementById("browse-aof").value = "";
  state.aofCond = "";
  state.browseOffset = 0;
  if (state.table) loadBrowse();
});
document.getElementById("browse-aof")?.addEventListener("keydown", (e) => {
  if (e.key === "Enter") {
    document.getElementById("browse-aof-apply").click();
  }
});

// studio.web.0.20 — guided demo. Walks the AOF story end-to-end
// against whatever table is currently active in the Browse pane:
//   1. read row 1 to pick a real value the user will see match;
//   2. install the AOF — lands at NONE because no index exists;
//   3. find the auto-suggested "Create index on <field>" chip and
//      click it; on success the AOF is re-applied automatically
//      and the badge flips to FULL;
//   4. narrate every step in the status line so the user can read
//      what just happened without having to know the underlying
//      ABI calls.
async function runAofDemo() {
  const status = document.getElementById("browse-aof-demo-status");
  const setStatus = (msg) => {
    if (!status) return;
    status.style.display = "block";
    status.textContent = msg;
  };
  if (!state.table) { alert("Pick a table on the left first."); return; }

  setStatus("Step 1/4 — peeking at row 1 to find a real TAG value to filter on…");
  let demoCol = null, demoVal = null;
  try {
    const peek = await api(`/api/tables/${encodeURIComponent(state.table)}` +
                            `/rows?offset=0&limit=1`);
    const cols = peek.cols || [];
    const types = peek.col_types || [];
    const r0 = (peek.rows || [])[0];
    if (!r0) { setStatus("Demo aborted — table is empty."); return; }
    // Pick the first character/memo column (V1 indexable types).
    for (let i = 0; i < cols.length; ++i) {
      if (types[i] === 4 || types[i] === 5) {
        demoCol = cols[i];
        demoVal = ("" + r0[i + 1]).trim();   // r0[0] is meta
        break;
      }
    }
    if (!demoCol) {
      setStatus("Demo aborted — no character/memo column to filter on. " +
                "(V1 only accelerates char/memo leaves; numeric/date/" +
                "logical leaves stay on the per-record fallback.)");
      return;
    }
  } catch (e) { setStatus("Demo aborted: " + e.message); return; }

  const cond = `${demoCol} = '${demoVal}'`;
  document.getElementById("browse-aof").value = cond;
  state.aofCond = cond;
  state.browseOffset = 0;

  setStatus(`Step 2/4 — applying AOF: ${cond}. ` +
            `Skip / GoTop will now walk only matching records. ` +
            `(No index on ${demoCol} yet, so AdsGetAOFOptLevel reports NONE — bitmap built by full-scan.)`);
  await loadBrowse();
  await new Promise(r => setTimeout(r, 1500));

  const data = state.lastBrowseData || {};
  if (data.aof_level === "FULL") {
    setStatus(`Demo done — AOF for ${cond} already at OptLevel FULL ` +
              `(an index on ${demoCol} was already in place).`);
    return;
  }

  // Find the chip we just rendered and click it.
  setStatus(`Step 3/4 — clicking the suggested "Create index on ${demoCol}" chip. ` +
            `This runs CREATE INDEX ${demoCol}_IDX ON ${state.table} (${demoCol}) ` +
            `and re-applies the AOF.`);
  const hints = document.getElementById("browse-aof-hints");
  let chip = null;
  if (hints) {
    chip = Array.from(hints.querySelectorAll("button"))
                .find(b => (b.textContent || "").indexOf(demoCol) >= 0);
  }
  if (!chip) {
    setStatus("Demo aborted — could not find the auto-suggested " +
              `"Create index on ${demoCol}" chip.`);
    return;
  }
  chip.click();
  await new Promise(r => setTimeout(r, 1500));

  // After the chip's own loadBrowse() finishes, the badge should be FULL.
  const final = state.lastBrowseData || {};
  if (final.aof_level === "FULL") {
    setStatus(`Step 4/4 — done. AOF for ${cond} is now served by the new ` +
              `${demoCol}_IDX index range scan: OptLevel = FULL. The same ` +
              `Skip / GoTop walk now visits only the ~${(final.total||0)} ` +
              `matching records, bypassing the per-record AST evaluation. ` +
              `Try Clear to drop the AOF and walk the whole table again.`);
  } else if (final.aof_level === "PART") {
    setStatus(`Step 4/4 — partial. OptLevel = PART. Some leaves still fall ` +
              `back to the per-record evaluation; chips above remain for the ` +
              `unindexed fields.`);
  } else {
    setStatus(`Step 4/4 — index created but OptLevel still NONE. ` +
              `That can happen if the field type isn't index-accelerated ` +
              `in V1 (only char / memo qualify). See the docs for details.`);
  }
}

document.getElementById("browse-aof-demo")?.addEventListener("click", () => {
  runAofDemo();
});

// URL params let docs / scripts deep-link to a specific tab + table.
//   /?table=employees.dbf&tab=structure
async function applyUrlState() {
  paintModeBadge();
  await loadTables();
  const u = new URL(location.href);
  const t = u.se)OPENADS_SPA";
inline constexpr const char kSpaPart7[] = R"OPENADS_SPA(archParams.get("table");
  const tab = u.searchParams.get("tab");
  const q   = u.searchParams.get("q");
  const run = u.searchParams.get("autorun") === "1";
  if (t) {
    state.table = t;
    document.querySelectorAll("aside li").forEach(li =>
      li.classList.toggle("active", li.dataset.name === t));
  }
  if (tab) showTab(tab);
  if (q && tab === "sql") {
    $("sql").value = q;
    if (run) await runSql();
  }
}
applyUrlState();
</script>
</body>
</html>
)OPENADS_SPA";
}  // namespace detail

inline const std::string kSpaIndexHtml = []{
    std::string s;
    s.reserve(72706);
    s += detail::kSpaPart1;
    s += detail::kSpaPart2;
    s += detail::kSpaPart3;
    s += detail::kSpaPart4;
    s += detail::kSpaPart5;
    s += detail::kSpaPart6;
    s += detail::kSpaPart7;
    return s;
}();


#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace openads::studio

#endif // OPENADS_WITH_HTTP
