<?php
/**
 * DA-Web — Data Architect for OpenADS
 * Main shell: renders the full-page application frame.
 */
?>
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>DA-Web — OpenADS Data Architect</title>

  <!-- Local vendor: jQuery (required by jsTree) -->
  <script src="vendor/jquery/jquery.min.js"></script>

  <!-- Local vendor: jsTree -->
  <link rel="stylesheet" href="vendor/jstree/themes/default/style.min.css">
  <script src="vendor/jstree/jstree.min.js"></script>

  <!-- Local vendor: Tabulator -->
  <link rel="stylesheet" href="vendor/tabulator/css/tabulator.min.css">
  <script src="vendor/tabulator/js/tabulator.min.js"></script>

  <!-- Local vendor: Split.js -->
  <script src="vendor/split.js/split.min.js"></script>

  <!-- Local vendor: Ace editor -->
  <script src="vendor/ace/ace.js"></script>
  <script src="vendor/ace/mode-sql.js"></script>
  <script src="vendor/ace/theme-dracula.js"></script>
  <script src="vendor/ace/ext-language_tools.js"></script>

  <!-- ADS SQL custom mode + HeidiSQL-like theme -->
  <script src="js/ads-sql-mode.js?v=<?= filemtime(__DIR__ . '/js/ads-sql-mode.js') ?>"></script>

  <!-- Application styles -->
  <link rel="stylesheet" href="css/app.css?v=<?= filemtime(__DIR__ . '/css/app.css') ?>">
</head>
<body>
<div id="app">

  <!-- ── Menu bar ─────────────────────────────────────────────────────────── -->
  <div id="menubar">

    <div class="menu-item" data-menu="file">
      File
      <div class="menu-dropdown">
        <div class="drop-item" data-action="open-sql">New SQL Tab</div>
        <div class="drop-separator"></div>
        <div class="drop-item" data-action="exit">Exit</div>
      </div>
    </div>

    <div class="menu-item" data-menu="connection">
      Connection
      <div class="menu-dropdown" id="connection-submenu">
        <div class="drop-item" data-action="create-dd">New DD…</div>
        <div class="drop-item" data-action="open-dd">Open DD…</div>
        <div class="drop-item" data-action="free-tables">Free Tables…</div>
        <div class="drop-separator"></div>
        <div class="drop-item disabled">Loading…</div>
      </div>
    </div>

    <div class="menu-item" data-menu="tools">
      Tools
      <div class="menu-dropdown">
        <div class="drop-item" data-action="import-sap-dd">Import SAP DD…</div>
        <div class="drop-item" data-action="server-info">Server Info…</div>
        <div class="drop-separator"></div>
        <div class="drop-item" data-action="refresh-tree">Refresh Tree</div>
      </div>
    </div>

    <div class="menu-item" data-menu="sql">
      SQL
      <div class="menu-dropdown">
        <div class="drop-item" data-action="open-sql">Open SQL Editor</div>
      </div>
    </div>

    <div class="menu-item" data-menu="window">
      Window
      <div class="menu-dropdown">
        <div class="drop-item disabled" id="window-list-placeholder">(no open tabs)</div>
      </div>
    </div>

    <div class="menu-item" data-menu="help">
      Help
      <div class="menu-dropdown">
        <div class="drop-item" data-action="about">About DA-Web</div>
      </div>
    </div>

  </div><!-- #menubar -->

  <!-- ── Workspace ────────────────────────────────────────────────────────── -->
  <div id="workspace">

    <!-- Tree pane -->
    <div id="tree-pane">
      <div class="pane-header">Data Dictionaries</div>
      <div id="tree-container"></div>
    </div>

    <!-- Content pane -->
    <div id="content-pane">

      <!-- Tab bar -->
      <div id="tab-bar">
        <div class="new-tab-btn" id="new-tab-btn" title="New SQL Tab">+</div>
      </div>

      <!-- Tab panels -->
      <div id="tab-content">
        <!-- Panels are injected dynamically by app.js -->
        <div style="display:flex;align-items:center;justify-content:center;height:100%;color:#45475a;font-size:20px;">
          Select a table from the tree to open it, or press + for a SQL editor.
        </div>
      </div>

    </div><!-- #content-pane -->

  </div><!-- #workspace -->

  <!-- ── Status bar ───────────────────────────────────────────────────────── -->
  <div id="statusbar">
    <span id="status-msg">Ready</span>
    <span style="flex:1"></span>
    <span id="status-connections">0 connection(s) open</span>
  </div>

</div><!-- #app -->

<!-- ── Context menu ──────────────────────────────────────────────────────── -->
<div id="ctx-menu"></div>

<!-- ── Modal: New DD (create) ─────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-create-dd" role="dialog" aria-modal="true">
  <div class="modal">
    <div class="modal-header">
      <span>New Data Dictionary</span>
      <span class="modal-close" onclick="document.getElementById('modal-create-dd').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="cdd-err" class="modal-err"></div>
      <div class="form-group">
        <label for="cdd-name">Name <span class="req">*</span></label>
        <input type="text" id="cdd-name" placeholder="e.g. Northwind" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="cdd-path">Path for new .add file <span class="req">*</span></label>
        <input type="text" id="cdd-path" placeholder="C:\Data\Northwind.add" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="cdd-password">Admin password <span class="opt">(optional)</span></label>
        <input type="password" id="cdd-password" autocomplete="new-password">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="cdd-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
      <div class="form-row" id="cdd-remote-fields" style="display:none">
        <div class="form-group">
          <label for="cdd-host">Server host <span class="opt">(blank = default)</span></label>
          <input type="text" id="cdd-host" placeholder="127.0.0.1" autocomplete="off">
        </div>
        <div class="form-group narrow">
          <label for="cdd-port">Port</label>
          <input type="text" id="cdd-port" placeholder="16262" autocomplete="off" inputmode="numeric">
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="cdd-cancel">Cancel</button>
      <button class="btn btn-primary" id="cdd-save">Create</button>
    </div>
  </div>
</div>

<!-- ── Modal: Open DD (add existing) ─────────────────────────────────────── -->
<div class="modal-overlay" id="modal-open-dd" role="dialog" aria-modal="true">
  <div class="modal">
    <div class="modal-header">
      <span>Open Data Dictionary</span>
      <span class="modal-close" onclick="document.getElementById('modal-open-dd').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="odd-err" class="modal-err"></div>
      <div class="form-group">
        <label for="odd-name">Name <span class="req">*</span></label>
        <input type="text" id="odd-name" placeholder="e.g. Northwind" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="odd-path">Path to .add file <span class="req">*</span></label>
        <input type="text" id="odd-path" placeholder="C:\Data\Northwind.add" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="odd-user">Default username</label>
        <input type="text" id="odd-user" placeholder="AdsSys" autocomplete="off">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="odd-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
      <div class="form-row" id="odd-remote-fields" style="display:none">
        <div class="form-group">
          <label for="odd-host">Server host <span class="opt">(blank = default)</span></label>
          <input type="text" id="odd-host" placeholder="127.0.0.1" autocomplete="off">
        </div>
        <div class="form-group narrow">
          <label for="odd-port">Port</label>
          <input type="text" id="odd-port" placeholder="16262" autocomplete="off" inputmode="numeric">
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="odd-cancel">Cancel</button>
      <button class="btn btn-primary" id="odd-save">Add</button>
    </div>
  </div>
</div>

<!-- ── Modal: Free Tables directory ──────────────────────────────────────── -->
<div class="modal-overlay" id="modal-free-tables" role="dialog" aria-modal="true">
  <div class="modal">
    <div class="modal-header">
      <span>Add Free Tables Directory</span>
      <span class="modal-close" onclick="document.getElementById('modal-free-tables').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="ft-err" class="modal-err"></div>
      <div class="form-group">
        <label for="ft-name">Name <span class="req">*</span></label>
        <input type="text" id="ft-name" placeholder="e.g. Legacy Tables" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="ft-path">Directory path <span class="req">*</span></label>
        <input type="text" id="ft-path" placeholder="C:\Data\tables\" autocomplete="off">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="ft-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
      <div class="form-row" id="ft-remote-fields" style="display:none">
        <div class="form-group">
          <label for="ft-host">Server host <span class="opt">(blank = default)</span></label>
          <input type="text" id="ft-host" placeholder="127.0.0.1" autocomplete="off">
        </div>
        <div class="form-group narrow">
          <label for="ft-port">Port</label>
          <input type="text" id="ft-port" placeholder="16262" autocomplete="off" inputmode="numeric">
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="ft-cancel">Cancel</button>
      <button class="btn btn-primary" id="ft-save">Add</button>
    </div>
  </div>
</div>

<!-- ── Modal: Connect ────────────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-connect" role="dialog" aria-modal="true" aria-labelledby="connect-title">
  <div class="modal">
    <div class="modal-header">
      <span id="connect-title">Connect to <span id="connect-dd-name"></span></span>
      <span class="modal-close" onclick="document.getElementById('modal-connect').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="connect-err" style="color:#f38ba8;font-size:12px;min-height:16px;margin-bottom:6px;"></div>
      <div class="form-group">
        <label for="connect-username">Username</label>
        <input type="text" id="connect-username" value="AdsSys" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="connect-password">Password</label>
        <input type="password" id="connect-password" autocomplete="current-password">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="connect-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
      <div class="form-row" id="connect-remote-fields" style="display:none">
        <div class="form-group">
          <label for="connect-host">Server host <span class="opt">(blank = default)</span></label>
          <input type="text" id="connect-host" placeholder="127.0.0.1" autocomplete="off">
        </div>
        <div class="form-group narrow">
          <label for="connect-port">Port</label>
          <input type="text" id="connect-port" placeholder="16262" autocomplete="off" inputmode="numeric">
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="connect-cancel">Cancel</button>
      <button class="btn btn-primary" id="connect-submit">Connect</button>
    </div>
  </div>
</div>

<!-- ── Modal: About ──────────────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-about" role="dialog" aria-modal="true">
  <div class="modal">
    <div class="modal-header">
      <span>About DA-Web</span>
      <span class="modal-close" id="about-close">&times;</span>
    </div>
    <div class="modal-body" style="line-height:1.7;font-size:13px;">
      <p><strong>DA-Web</strong> — OpenADS Data Architect</p>
      <p style="margin-top:8px;color:#a6adc8;">Web-based replacement for SAP Data Architect.<br>
      Backend: PHP + php_openads native extension.<br>
      Frontend: jsTree · Tabulator · Split.js · jQuery · Ace Editor.</p>
      <p style="margin-top:12px;color:#45475a;font-size:11px;">
        Part of the <a href="https://github.com/FiveTechSoft/OpenADS" style="color:#89b4fa">OpenADS</a> project.
      </p>
    </div>
    <div class="modal-footer">
      <button class="btn btn-primary" id="about-ok" onclick="document.getElementById('modal-about').classList.remove('open')">OK</button>
    </div>
  </div>
</div>

<!-- ── Modal: Delete row confirmation ────────────────────────────────────── -->
<div class="modal-overlay" id="modal-del-confirm" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:360px">
    <div class="modal-header">
      <span>Delete Row</span>
      <span class="modal-close" onclick="document.getElementById('modal-del-confirm').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <p style="margin:0;line-height:1.6;">Are you sure you want to delete the selected row?<br>
      <span style="color:#f38ba8;font-size:12px;">This action cannot be undone.</span></p>
    </div>
    <div class="modal-footer">
      <button class="btn" id="del-confirm-cancel">Cancel</button>
      <button class="btn btn-danger" id="del-confirm-ok">Delete</button>
    </div>
  </div>
</div>

<!-- ── Modal: SQL Save script ─────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-sql-save" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:400px">
    <div class="modal-header">
      <span>Save SQL Script</span>
      <span class="modal-close" onclick="document.getElementById('modal-sql-save').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="sql-save-err" class="modal-err"></div>
      <div class="form-group">
        <label for="sql-save-name">Script name <span class="req">*</span></label>
        <input type="text" id="sql-save-name" placeholder="e.g. Active leases" autocomplete="off">
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="sql-save-cancel">Cancel</button>
      <button class="btn btn-primary" id="sql-save-ok">Save</button>
    </div>
  </div>
</div>

<!-- ── Modal: SQL Open script ─────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-sql-open" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:520px">
    <div class="modal-header" style="display:flex;align-items:center;gap:8px;">
      <span style="flex:1">Open SQL Script</span>
      <button class="btn btn-sm" id="sql-open-all" title="Open every saved script as a separate tab">Open All</button>
      <span class="modal-close" onclick="document.getElementById('modal-sql-open').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body" style="padding:0">
      <div id="sql-scripts-list" style="max-height:360px;overflow-y:auto;"></div>
    </div>
  </div>
</div>

<!-- ── Modal: Properties (edit DD / free-tables connection) ──────────────── -->
<div class="modal-overlay" id="modal-props" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:460px">
    <div class="modal-header">
      <span>Properties — <span id="props-title"></span></span>
      <span class="modal-close" onclick="document.getElementById('modal-props').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="props-err" class="modal-err"></div>
      <input type="hidden" id="props-name">
      <input type="hidden" id="props-entry-type">
      <div class="form-group">
        <label>Name</label>
        <input type="text" id="props-name-display" disabled style="opacity:.6;cursor:default">
      </div>
      <div class="form-group">
        <label for="props-path">Path <span class="req">*</span></label>
        <input type="text" id="props-path" autocomplete="off">
      </div>
      <div class="form-group" id="props-user-row">
        <label for="props-username">Default username</label>
        <input type="text" id="props-username" autocomplete="off">
      </div>
      <div class="form-group">
        <label>Connection type</label>
        <div class="toggle-group" id="props-conn-type">
          <button type="button" class="toggle-btn active" data-value="local">Local</button>
          <button type="button" class="toggle-btn" data-value="remote">Remote</button>
        </div>
      </div>
      <div class="form-row" id="props-remote-fields" style="display:none">
        <div class="form-group">
          <label for="props-host">Server host <span class="opt">(blank = default)</span></label>
          <input type="text" id="props-host" placeholder="127.0.0.1" autocomplete="off">
        </div>
        <div class="form-group narrow">
          <label for="props-port">Port</label>
          <input type="text" id="props-port" placeholder="16262" autocomplete="off" inputmode="numeric">
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="props-cancel">Cancel</button>
      <button class="btn btn-primary" id="props-save">Save</button>
    </div>
  </div>
</div>

<!-- ── Modal: Import SAP DD ───────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-import-sap-dd" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:520px">
    <div class="modal-header">
      <span>Import SAP Data Dictionary</span>
      <span class="modal-close" onclick="document.getElementById('modal-import-sap-dd').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="isdd-err" class="modal-err"></div>
      <p style="margin:0 0 12px;color:#cdd6f4;font-size:12px;line-height:1.5;">
        Copies a SAP ADS <code>.add</code> file and imports its group memberships
        and permissions into the OpenADS copy using the native import tool.
        The original file is never modified.
      </p>
      <div class="form-group">
        <label for="isdd-name">Display name for imported DD <span class="req">*</span></label>
        <input type="text" id="isdd-name" placeholder="e.g. MyApp (imported)" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="isdd-source">Source SAP .add file <span class="req">*</span></label>
        <input type="text" id="isdd-source" placeholder="C:\Data\myapp.add" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="isdd-dest">Destination OpenADS .add file <span class="req">*</span></label>
        <input type="text" id="isdd-dest" placeholder="C:\Data\myapp_openads.add" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="isdd-user">SAP administrator username <span class="req">*</span></label>
        <input type="text" id="isdd-user" placeholder="AdsSys" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="isdd-password">SAP password</label>
        <input type="password" id="isdd-password" autocomplete="current-password">
      </div>
      <div class="form-group">
        <label for="isdd-saplib">SAP library path <span class="opt">(optional — leave blank for defaults; directory or ace64.dll path)</span></label>
        <input type="text" id="isdd-saplib" placeholder="e.g. C:\php\ace64.dll  or  C:\ADS  (blank = auto-detect)" autocomplete="off">
      </div>
      <div id="isdd-result" style="display:none;margin-top:10px;padding:10px;border-radius:6px;font-size:12px;line-height:1.6;"></div>
      <div id="isdd-history" class="import-history"></div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="isdd-cancel">Cancel</button>
      <button class="btn btn-primary" id="isdd-run">Import</button>
    </div>
  </div>
</div>

<!-- ── Modal: New User ─────────────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-new-user" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:400px">
    <div class="modal-header">
      <span>New User — <span id="new-user-dd-label"></span></span>
      <span class="modal-close" onclick="document.getElementById('modal-new-user').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="new-user-err" class="modal-err"></div>
      <input type="hidden" id="new-user-dd">
      <div class="form-group">
        <label for="new-user-name">Username <span class="req">*</span></label>
        <input type="text" id="new-user-name" autocomplete="off">
      </div>
      <div class="form-group">
        <label for="new-user-pw">Password</label>
        <input type="password" id="new-user-pw" autocomplete="new-password">
      </div>
      <div class="form-group">
        <label for="new-user-pw2">Confirm password</label>
        <input type="password" id="new-user-pw2" autocomplete="new-password">
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="new-user-cancel">Cancel</button>
      <button class="btn btn-primary" id="new-user-ok">Create User</button>
    </div>
  </div>
</div>

<!-- ── Modal: New Group ────────────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-new-group" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:380px">
    <div class="modal-header">
      <span>New Group — <span id="new-group-dd-label"></span></span>
      <span class="modal-close" onclick="document.getElementById('modal-new-group').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="new-group-err" class="modal-err"></div>
      <input type="hidden" id="new-group-dd">
      <div class="form-group">
        <label for="new-group-name">Group name <span class="req">*</span></label>
        <input type="text" id="new-group-name" autocomplete="off">
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="new-group-cancel">Cancel</button>
      <button class="btn btn-primary" id="new-group-ok">Create Group</button>
    </div>
  </div>
</div>

<!-- ── Modal: New RI ───────────────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-new-ri" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:380px">
    <div class="modal-header">
      <span>New RI Rule — <span id="new-ri-dd-label"></span></span>
      <span class="modal-close" onclick="document.getElementById('modal-new-ri').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="new-ri-err" class="modal-err"></div>
      <input type="hidden" id="new-ri-dd">
      <div class="form-group">
        <label for="new-ri-name">RI rule name <span class="req">*</span></label>
        <input type="text" id="new-ri-name" autocomplete="off" placeholder="e.g. Orders2Customers">
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="new-ri-cancel">Cancel</button>
      <button class="btn btn-primary" id="new-ri-ok">Open RI Form</button>
    </div>
  </div>
</div>

<!-- ── Modal: New Table ───────────────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-new-table" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:420px">
    <div class="modal-header">
      <span>New Table — <span id="new-table-dd-label"></span></span>
      <span class="modal-close" onclick="document.getElementById('modal-new-table').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body">
      <div id="new-table-err" class="modal-err"></div>
      <input type="hidden" id="new-table-dd">
      <div class="form-group">
        <label for="new-table-name">Table name <span class="req">*</span></label>
        <input type="text" id="new-table-name" autocomplete="off" placeholder="e.g. Customers">
      </div>
      <div class="form-group">
        <label>Table format</label>
        <div class="toggle-group" id="new-table-type">
          <button type="button" class="toggle-btn active" data-value="ADT">ADT (native)</button>
          <button type="button" class="toggle-btn" data-value="DBF">DBF (dBASE)</button>
        </div>
      </div>
      <div class="form-group">
        <label>Character set</label>
        <div class="toggle-group" id="new-table-char">
          <button type="button" class="toggle-btn active" data-value="ANSI">ANSI</button>
          <button type="button" class="toggle-btn" data-value="OEM">OEM</button>
        </div>
      </div>
    </div>
    <div class="modal-footer">
      <button class="btn" id="new-table-cancel">Cancel</button>
      <button class="btn btn-primary" id="new-table-ok">Create Table</button>
    </div>
  </div>
</div>

<!-- ── Modal: Blob / Memo viewer ──────────────────────────────────────────── -->
<div class="modal-overlay" id="modal-blob-viewer" role="dialog" aria-modal="true">
  <div class="modal" style="max-width:640px">
    <div class="modal-header">
      <span id="blob-viewer-title">Field content</span>
      <span class="modal-close" onclick="document.getElementById('modal-blob-viewer').classList.remove('open')">&times;</span>
    </div>
    <div class="modal-body" id="blob-viewer-body" style="padding:12px;"></div>
    <div class="modal-footer">
      <button class="btn btn-primary" id="blob-viewer-download" style="display:none;">&#11015; Download</button>
      <button class="btn" onclick="document.getElementById('modal-blob-viewer').classList.remove('open')">Close</button>
    </div>
  </div>
</div>

<!-- Application JS (loaded last so DOM is ready) -->
<script src="js/app.js?v=<?= filemtime(__DIR__ . '/js/app.js') ?>"></script>
</body>
</html>
