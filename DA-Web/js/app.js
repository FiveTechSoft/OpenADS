/* DA-Web — main application JS */
(function () {
  'use strict';

  // ── SQL keyword set for auto-capitalize ───────────────────────────────────
  const SQL_KEYWORDS = new Set((
    'SELECT FROM WHERE AND OR NOT IN IS NULL LIKE BETWEEN EXISTS UNION ALL ANY SOME ' +
    'EXCEPT INTERSECT INSERT INTO VALUES UPDATE SET DELETE TRUNCATE MERGE MATCHED ' +
    'CREATE ALTER DROP TABLE VIEW INDEX PROCEDURE PROC FUNCTION TRIGGER DATABASE SCHEMA ' +
    'COLUMN CONSTRAINT PRIMARY FOREIGN KEY REFERENCES UNIQUE CHECK DEFAULT ADD MODIFY RENAME TO ' +
    'INNER LEFT RIGHT FULL OUTER JOIN ON USING CROSS NATURAL ' +
    'GROUP BY ORDER HAVING DISTINCT AS WITH CASE WHEN THEN ELSE END ' +
    'IF ELSE BEGIN END COMMIT ROLLBACK TRANSACTION SAVEPOINT ' +
    'GRANT REVOKE EXECUTE EXEC RETURN RETURNS ' +
    'DECLARE CURSOR OPEN FETCH NEXT CLOSE DEALLOCATE ' +
    'TOP LIMIT OFFSET ROWS ONLY PERCENT TIES ROWNUM ' +
    'ASC DESC NULLS FIRST LAST ' +
    'COUNT SUM AVG MIN MAX CAST CONVERT COALESCE NULLIF IIF ISNULL NVL ' +
    'OVER PARTITION WINDOW RANGE UNBOUNDED PRECEDING FOLLOWING CURRENT ROW ' +
    'OUTPUT INSERTED DELETED INTO ' +
    'CHAR VARCHAR NVARCHAR INT INTEGER BIGINT SMALLINT TINYINT ' +
    'FLOAT DOUBLE REAL DECIMAL NUMERIC MONEY SMALLMONEY ' +
    'DATE TIME DATETIME DATETIME2 SMALLDATETIME TIMESTAMP ' +
    'BIT BOOLEAN BINARY VARBINARY IMAGE TEXT NTEXT CLOB BLOB ' +
    'IDENTITY AUTOINCREMENT SEQUENCE ROWID ROWGUIDCOL ' +
    'NOLOCK READPAST UPDLOCK HOLDLOCK TABLOCKX XLOCK WITH ' +
    'EXISTS UNIQUE CLUSTERED NONCLUSTERED COLUMNSTORE INCLUDE ' +
    'REPLACE IGNORE DELAYED STRAIGHT_JOIN SQL_CALC_FOUND_ROWS'
  ).split(' ').filter(Boolean));

  // ── State ──────────────────────────────────────────────────────────────────
  const state = {
    openConnections: new Set(),
    tabs: [],          // { id, title, type, dd, table }
    activeTabId: null,
    nextTabId: 1,
    selectedDD: null,  // DD of the currently highlighted tree node
    aceEditors: {},    // tabId → Ace editor instance
  };

  // Per-table-tab state: tabId → { tbl, dd, table, rowIdx, pendingRow, confirmBtn }
  const tblState = {};

  // Tabulator instances for proc/function parameter grids
  const procParamGrids = {};

  // ADS supported parameter/return data types
  const ADS_TYPES = [
    'CHAR','VARCHAR','MEMO',
    'SHORT','INTEGER','LONGINT','LARGEINT',
    'FLOAT','DOUBLE','NUMERIC','MONEY',
    'DATE','TIME','TIMESTAMP',
    'LOGICAL','RAW','BINARY',
  ];

  // Context holders for async modals
  let pendingDeleteTabId = null;
  let pendingSqlSaveTabId = null;

  // ── Helpers ────────────────────────────────────────────────────────────────
  async function apiFetch(url, options = {}) {
    const res = await fetch(url, options);
    const data = await res.json();
    if (!res.ok) {
      const err = new Error(data.error || `HTTP ${res.status}`);
      err.data = data;
      throw err;
    }
    return data;
  }

  function setStatus(msg, type = 'info') {
    const bar = document.getElementById('status-msg');
    if (bar) bar.textContent = msg;
  }

  function showAlert(container, msg, type = 'error') {
    const el = document.createElement('div');
    el.className = `alert alert-${type}`;
    el.textContent = msg;
    container.prepend(el);
    setTimeout(() => el.remove(), 5000);
  }

  // ── Menu bar ───────────────────────────────────────────────────────────────
  document.querySelectorAll('#menubar .menu-item').forEach(item => {
    item.addEventListener('click', e => {
      // If the click came from a drop-item, close the menu and let it bubble
      // to the document-level action dispatcher — do NOT stopPropagation.
      if (e.target.closest('.drop-item')) {
        document.querySelectorAll('#menubar .menu-item').forEach(i => i.classList.remove('active'));
        return;
      }
      const wasActive = item.classList.contains('active');
      document.querySelectorAll('#menubar .menu-item').forEach(i => i.classList.remove('active'));
      if (!wasActive) item.classList.add('active');
      e.stopPropagation();
    });
  });

  document.addEventListener('click', () => {
    document.querySelectorAll('#menubar .menu-item').forEach(i => i.classList.remove('active'));
  });

  // Menu action dispatcher — fires for drop-item clicks that bubbled to document
  document.addEventListener('click', e => {
    const item = e.target.closest('.drop-item[data-action]');
    if (!item) return;
    handleMenuAction(item.dataset.action, item);
  });

  function handleMenuAction(action, el) {
    switch (action) {
      case 'create-dd':    openCreateDDModal();            break;
      case 'open-dd':      openOpenDDModal();              break;
      case 'free-tables':  openFreeTablesModal();          break;
      case 'connect':      openConnectModal(el.dataset.dd); break;
      case 'disconnect':   disconnectDD(el.dataset.dd);   break;
      case 'open-sql':     openSqlTab();                  break;
      case 'import-sap-dd': openImportSapDDModal();         break;
      case 'refresh-tree': refreshTree();                  break;
      case 'about':        openAboutModal();               break;
    }
  }

  // ── Toggle-group wiring (called once after DOMContentLoaded) ──────────────
  function initToggleGroups() {
    document.querySelectorAll('.toggle-group').forEach(group => {
      group.querySelectorAll('.toggle-btn').forEach(btn => {
        btn.addEventListener('click', () => {
          group.querySelectorAll('.toggle-btn').forEach(b => b.classList.remove('active'));
          btn.classList.add('active');
        });
      });
    });
  }

  function toggleGroupValue(groupId) {
    return document.querySelector(`#${groupId} .toggle-btn.active`)?.dataset.value ?? 'local';
  }

  function resetToggleGroup(groupId) {
    const group = document.getElementById(groupId);
    if (!group) return;
    group.querySelectorAll('.toggle-btn').forEach((b, i) => b.classList.toggle('active', i === 0));
  }

  // ── Split.js ───────────────────────────────────────────────────────────────
  if (typeof Split !== 'undefined') {
    Split(['#tree-pane', '#content-pane'], {
      sizes: [22, 78],
      minSize: [120, 300],
      gutterSize: 4,
      cursor: 'col-resize',
    });
  }

  // ── jsTree ─────────────────────────────────────────────────────────────────
  function initTree() {
    $('#tree-container').jstree({
      core: {
        themes: { name: 'default', dots: true, icons: true },
        data: function (node, cb) {
          const isRoot = node.id === '#';
          if (isRoot) {
            fetch('api/tree.php?action=roots')
              .then(r => r.json()).then(cb).catch(() => cb([]));
            return;
          }
          const a = node.a_attr || {};
          const dd   = a['data-dd']    || '';
          const type = a['data-type']  || '';
          const cat  = a['data-cat']   || '';
          const tbl  = a['data-table'] || '';

          if (type === 'dd') {
            fetch(`api/tree.php?action=dd_children&dd=${encodeURIComponent(dd)}`)
              .then(r => r.json()).then(cb).catch(() => cb([]));
          } else if (type === 'category') {
            fetch(`api/tree.php?action=category_children&dd=${encodeURIComponent(dd)}&cat=${encodeURIComponent(cat)}`)
              .then(r => r.json()).then(cb).catch(() => cb([]));
          } else if (type === 'table') {
            const entry = a['data-entry'] || '';
            fetch(`api/tree.php?action=table_children&dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(tbl)}&entry=${encodeURIComponent(entry)}`)
              .then(r => r.json()).then(cb).catch(() => cb([]));
          } else {
            cb([]);
          }
        },
      },
      plugins: ['wholerow', 'types'],
    });

    $('#tree-container').on('select_node.jstree', function (e, data) {
      const a    = data.node.a_attr || {};
      const type = a['data-type']   || '';
      const dd   = a['data-dd']     || '';
      const tbl  = a['data-table']  || '';

      // Track which DD is currently highlighted so SQL editor can pre-select it
      if (dd) state.selectedDD = dd;

      if (type === 'dd') {
        const connected = a['data-connected'] === 'true';
        if (!connected) openConnectModal(dd);
      } else if (type === 'table') {
        openTableTab(dd, tbl);
      } else if (type === 'fields') {
        openMetaTab(dd, tbl, 'fields');
      } else if (type === 'indexes') {
        openMetaTab(dd, tbl, 'indexes');
      } else if (type === 'table_triggers') {
        openTableTriggersTab(dd, tbl);
      } else if (type === 'gen_sql') {
        openGenSqlTab(dd, tbl);
      } else if (type === 'function' || type === 'proc') {
        openObjectSourceTab(dd, type, a['data-name'] || data.node.text || '');
      } else if (type === 'group') {
        openGroupTab(dd, a['data-name'] || data.node.text || '');
      } else if (type === 'user') {
        openUserTab(dd, a['data-name'] || data.node.text || '');
      } else if (type === 'ri') {
        openRiTab(dd, a['data-name'] || data.node.text || '');
      }
    });

    $('#tree-container').on('dblclick.jstree', function (e) {
      const node = $(e.target).closest('li');
      if (!node.length) return;
      const treeNode = $.jstree.reference(node);
      if (!treeNode) return;
      const sel = treeNode.get_node(node);
      if (!sel) return;
      const a    = sel.a_attr || {};
      const type = a['data-type'] || '';
      const dd   = a['data-dd']   || '';
      const tbl  = a['data-table'] || '';
      if (type === 'table') openTableTab(dd, tbl);
    });
  }

  function refreshTree() {
    $('#tree-container').jstree(true).refresh();
  }

  // ── Tabs ───────────────────────────────────────────────────────────────────
  function openTableTab(dd, table) {
    const existingId = state.tabs.findIndex(t => t.type === 'table' && t.dd === dd && t.table === table);
    if (existingId !== -1) {
      activateTab(state.tabs[existingId].id);
      return;
    }
    const id = 'tab-' + (state.nextTabId++);
    state.tabs.push({ id, title: `${dd}.${table}`, type: 'table', dd, table });
    renderTabs();
    activateTab(id);
    loadTableData(id, dd, table);
  }

  // ── Parameter string helpers ──────────────────────────────────────────────

  // Parse a datatype string into base / size / decimals.
  // Handles both compact "CHAR(25)" and spaced "CHAR ( 25 )" forms.
  function parseDatatype(dt) {
    const m = (dt || '').match(/^(\w+)\s*(?:\(\s*(\d+)(?:\s*,\s*(\d+))?\s*\))?$/i);
    return {
      base:     m ? m[1].toUpperCase() : (dt || '').toUpperCase(),
      size:     m?.[2] ? parseInt(m[2]) : 0,
      decimals: m?.[3] ? parseInt(m[3]) : 0,
    };
  }

  function serializeDatatype(base, size, decimals) {
    if (size > 0) return `${base}(${size}${decimals > 0 ? ',' + decimals : ''})`;
    return base || '';
  }

  // Parse an input or output params string returned by proc_body.php.
  //
  // Stored-proc format:  "Name,TYPE;Name,TYPE;"        (e.g. "Month,SHORTINT;Year,Integer;")
  //   TYPE may include a size: "Name,TYPE,SIZE;"        (e.g. "cField,CHAR,20;")
  // Function format:     "name TYPE, name TYPE"          (space-separated, comma between)
  function parseParamStr(str, objType, paramType = 'Input') {
    if (!str || !str.trim()) return [];
    if (objType === 'function') {
      return str.replace(/\s+/g, ' ').trim().split(',')
        .filter(s => s.trim()).map((s, i) => {
          s = s.trim();
          const sp = s.indexOf(' ');
          const nm = sp >= 0 ? s.slice(0, sp).trim() : s;
          const dt = sp >= 0 ? s.slice(sp + 1).trim() : '';
          const { base, size, decimals } = parseDatatype(dt);
          return { _id: 'p' + i, paramType, name: nm, baseType: base, size, decimals };
        });
    } else {
      // Stored proc: "Name,TYPE[,SIZE];Name,TYPE[,SIZE];"
      return str.split(';').filter(s => s.trim()).map((s, i) => {
        const parts = s.split(',').map(p => p.trim());
        const nm = parts[0] || '';
        // parts[1] = base type, parts[2] = optional size
        const dt = parts.slice(1).join(','); // rejoin in case type had commas
        // But size is actually the 3rd comma-separated segment
        const baseType = parts[1] || '';
        const sizeVal  = parts[2] ? parseInt(parts[2], 10) || 0 : 0;
        const { base, size: parsedSize, decimals } = parseDatatype(baseType);
        const size = sizeVal > 0 ? sizeVal : parsedSize;
        return { _id: paramType[0].toLowerCase() + i, paramType, name: nm, baseType: base, size, decimals };
      });
    }
  }

  // Serialize rows back to "Name,TYPE;" (stored proc) or "name TYPE, …" (function).
  function serializeParams(rows, objType, paramType = 'Input') {
    const filtered = rows.filter(r => r.paramType === paramType && r.name && r.baseType);
    if (objType === 'function') {
      return filtered.map(r =>
        `${r.name} ${serializeDatatype(r.baseType, r.size || 0, r.decimals || 0)}`
      ).join(', ');
    } else {
      // "Name,TYPE;Name,TYPE;" — include size as 3rd segment when > 0
      return filtered.map(r => {
        const base = r.baseType || '';
        const sz   = r.size || 0;
        return sz > 0 ? `${r.name},${base},${sz}` : `${r.name},${base}`;
      }).join(';') + (filtered.length ? ';' : '');
    }
  }

  async function openObjectSourceTab(dd, type, name) {
    if (!name || !dd) return;
    const existing = state.tabs.find(t => t.type === 'sql' && t.title === name);
    if (existing) { activateTab(existing.id); return; }

    const isFunc = type === 'function';
    let body         = `-- ${name}: source not available`;
    let inputParams  = [];
    let outputParams = [];
    let returnType   = '';

    try {
      const resp = await apiFetch('api/proc_body.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ dd, type, name }),
      });
      body         = resp.body?.trim() || body;
      inputParams  = parseParamStr(resp.input_params  || '', type, 'Input');
      outputParams = parseParamStr(resp.output_params || '', type, 'Output');
      returnType   = resp.return_type || '';
    } catch (err) {
      body = `-- Error loading ${name}: ${err.message}`;
    }

    openSqlTab(dd, body, name, { objType: type, objName: name, inputParams, outputParams, returnType });
  }

  function openMetaTab(dd, table, kind) {
    const metaKey = `${dd}.${table}.${kind}`;
    const existing = state.tabs.find(t => t.type === 'meta' && t.metaKey === metaKey);
    if (existing) { activateTab(existing.id); return; }
    const id = 'tab-' + (state.nextTabId++);
    const kindTitle = kind === 'fields' ? 'Fields' : kind === 'indexes' ? 'Indexes' : 'Triggers';
    const title = `${table} ${kindTitle}`;
    state.tabs.push({ id, title, type: 'meta', dd, table, kind, metaKey });
    renderTabs();
    activateTab(id);
    loadMetaData(id, dd, table, kind);
  }

  function openTableTriggersTab(dd, table) {
    const metaKey = `${dd}.${table}.triggers`;
    const existing = state.tabs.find(t => t.type === 'table_triggers' && t.metaKey === metaKey);
    if (existing) { activateTab(existing.id); return; }
    const id = 'tab-' + (state.nextTabId++);
    state.tabs.push({ id, title: `${table} Triggers`, type: 'table_triggers', dd, table, metaKey });
    renderTabs();
    activateTab(id);
    loadTriggerData(id, dd, table);
  }

  async function openGenSqlTab(dd, table) {
    try {
      const resp = await fetch(`api/gen_sql.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}`);
      const data = await resp.json();
      if (data.error) { setStatus(`gen_sql error: ${data.error}`); return; }
      openSqlTab(dd, data.sql || '', `${table} DDL`);
    } catch (err) {
      setStatus(`gen_sql: ${err.message}`);
    }
  }

  // ── Group permissions tab ───────────────────────────────────────────────────
  function openGroupTab(dd, groupName) {
    const key = `${dd}.group.${groupName}`;
    const existing = state.tabs.find(t => t.type === 'group' && t.metaKey === key);
    if (existing) { activateTab(existing.id); return; }
    const id = 'tab-' + (state.nextTabId++);
    state.tabs.push({ id, title: `Group: ${groupName}`, type: 'group', dd, groupName, metaKey: key });
    renderTabs();
    activateTab(id);
    loadGroupData(id, dd, groupName);
  }

  function loadGroupData(tabId, dd, groupName) {
    const container = document.getElementById('group-' + tabId);
    if (!container) return;
    apiFetch(`api/group_meta.php?dd=${encodeURIComponent(dd)}&group=${encodeURIComponent(groupName)}`)
      .then(resp => {
        if (resp.error) {
          container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(resp.error)}</div>`;
          return;
        }

        const activeStyle   = 'background:#313244;color:#cdd6f4;border:1px solid #45475a;';
        const inactiveStyle = 'color:#a6adc8;border:1px solid transparent;background:transparent;';

        container.innerHTML =
          `<div style="padding:4px 6px;display:flex;gap:8px;align-items:center;background:#1e1e2e;border-bottom:1px solid #313244;">
             <button class="btn btn-sm" id="gtab-perm-${tabId}"    style="${activeStyle}">Permissions</button>
             <button class="btn btn-sm" id="gtab-members-${tabId}" style="${inactiveStyle}">Members</button>
             <button class="btn btn-sm btn-primary" id="save-group-${tabId}" style="margin-left:auto;">&#128190; Save Changes</button>
             <span id="save-group-msg-${tabId}" style="font-size:11px;color:#a6adc8;"></span>
             <span id="grp-perm-note-${tabId}" style="font-size:11px;color:#585b70;">Field rows reflect table-level permissions</span>
           </div>
           <div id="group-tbl-${tabId}"     style="flex:1;min-height:0;overflow:hidden;"></div>
           <div id="group-members-${tabId}" style="flex:1;min-height:0;overflow:hidden;display:none;"></div>`;

        // ── Permissions grid ───────────────────────────────────────────────────
        const TYPE_LABEL = { 1:'Table', 4:'Field', 6:'View', 10:'Stored Proc', 18:'Function' };
        const isExecType   = t => t === 10 || t === 18;
        const isFieldType  = t => t === 4;
        const isSchemaType = t => t === 1 || t === 6;

        const DASH = '<span style="color:#45475a;font-size:13px;display:block;text-align:center;">—</span>';
        const check = (on, editable) => on
          ? `<span style="color:${editable?'#a6e3a1':'#6a9f7e'};font-size:16px;display:block;text-align:center;${editable?'cursor:pointer':'opacity:0.6'};">✓</span>`
          : `<span style="color:${editable?'#f38ba8':'#9a6f72'};font-size:16px;display:block;text-align:center;${editable?'cursor:pointer':'opacity:0.6'};">✗</span>`;

        const permCol = (title, field, readonly = false) => ({
          title, field, width: 72, headerSort: false,
          formatter: (cell) => {
            const t = cell.getRow().getData().type;
            if (field === 'canExec')   return isExecType(t)   ? check(cell.getValue()==='Yes', !readonly) : DASH;
            if (field === 'canDelete') return (isFieldType(t) || isExecType(t)) ? DASH : check(cell.getValue()==='Yes', !readonly);
            if (field === 'canAlter' || field === 'canDrop') return isSchemaType(t) ? check(cell.getValue()==='Yes', false) : DASH;
            if (isExecType(t)) return DASH;
            return check(cell.getValue()==='Yes', !(isFieldType(t) || readonly));
          },
          cellClick: (_e, cell) => {
            if (readonly) return;
            const t = cell.getRow().getData().type;
            if (isFieldType(t)) return;
            if (field === 'canExec'   && !isExecType(t)) return;
            if (field !== 'canExec'   &&  isExecType(t)) return;
            if (field === 'canDelete' &&  isExecType(t)) return;
            if (field === 'canAlter'  || field === 'canDrop') return;
            cell.setValue(cell.getValue() === 'Yes' ? 'No' : 'Yes');
          },
        });

        const permGrid = new Tabulator('#group-tbl-' + tabId, { /* global Tabulator */
          data: resp.data,
          layout: 'fitDataFill',
          pagination: 'local', paginationSize: 500, height: '100%',
          placeholder: '(no objects)',
          rowFormatter: row => {
            if (row.getData().type === 4) row.getElement().style.background = '#181825';
          },
          columns: [
            { title: 'Object', field: 'object', widthGrow: 3, headerSort: true },
            { title: 'Parent', field: 'parent', width: 130, headerSort: true,
              formatter: cell => cell.getValue() ? escHtml(cell.getValue()) : '' },
            { title: 'Type', field: 'type', width: 100, headerSort: false,
              formatter: cell => TYPE_LABEL[cell.getValue()] || `Type ${cell.getValue()}` },
            permCol('Select',  'canSelect'),
            permCol('Insert',  'canInsert'),
            permCol('Update',  'canUpdate'),
            permCol('Delete',  'canDelete'),
            permCol('Execute', 'canExec'),
            permCol('Alter',   'canAlter', true),
            permCol('Drop',    'canDrop',  true),
          ],
        });

        const saveBtn = document.getElementById('save-group-' + tabId);
        const msgEl   = document.getElementById('save-group-msg-' + tabId);
        saveBtn?.addEventListener('click', async () => {
          if (msgEl) msgEl.textContent = 'Saving…';
          try {
            const r = await apiFetch('api/save_group_meta.php', {
              method: 'POST', headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ dd, group: groupName, rows: permGrid.getData() }),
            });
            if (msgEl) msgEl.textContent = r.error ? `Error: ${r.error}` : `Saved ${r.saved} permission(s)`;
          } catch (err) {
            if (msgEl) msgEl.textContent = `Error: ${err.message}`;
          }
        });

        // ── Sub-tab switching ─────────────────────────────────────────────────
        const permPanel    = document.getElementById('group-tbl-'     + tabId);
        const membersPanel = document.getElementById('group-members-' + tabId);
        const btnPerm      = document.getElementById('gtab-perm-'     + tabId);
        const btnMembers   = document.getElementById('gtab-members-'  + tabId);
        const permNote     = document.getElementById('grp-perm-note-' + tabId);
        let membersLoaded  = false;

        const switchGroupTab = (which) => {
          const isPerm = which === 'perm';
          if (permPanel)    permPanel.style.display    = isPerm  ? '' : 'none';
          if (membersPanel) membersPanel.style.display = !isPerm ? '' : 'none';
          if (saveBtn)      saveBtn.style.display      = isPerm  ? '' : 'none';
          if (permNote)     permNote.style.display     = isPerm  ? '' : 'none';
          if (msgEl)        msgEl.style.display        = isPerm  ? '' : 'none';
          if (btnPerm)      btnPerm.style.cssText      = isPerm  ? activeStyle : inactiveStyle;
          if (btnMembers)   btnMembers.style.cssText   = !isPerm ? activeStyle : inactiveStyle;

          if (!isPerm && !membersLoaded) {
            membersLoaded = true;
            loadGroupMembers(tabId, dd, groupName);
          }
        };

        btnPerm?.addEventListener('click',    () => switchGroupTab('perm'));
        btnMembers?.addEventListener('click', () => switchGroupTab('members'));
      })
      .catch(err => {
        if (container) container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
      });
  }

  // ── Group members grid (add / remove users from a group) ────────────────────
  function loadGroupMembers(tabId, dd, groupName) {
    const panel = document.getElementById('group-members-' + tabId);
    if (!panel) return;
    panel.innerHTML = '<div style="padding:8px;color:#a6adc8;font-size:12px;">Loading…</div>';

    apiFetch(`api/group_members.php?dd=${encodeURIComponent(dd)}&group=${encodeURIComponent(groupName)}`)
      .then(resp => {
        if (resp.error) {
          panel.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(resp.error)}</div>`;
          return;
        }

        const currentSet = new Set((resp.members || []).map(u => u.toLowerCase()));
        const available  = (resp.allUsers || []).filter(u => !currentSet.has(u.toLowerCase()));
        const availMap   = Object.fromEntries(available.map(u => [u, u]));

        panel.innerHTML =
          `<div style="padding:4px 6px;display:flex;gap:8px;align-items:center;background:#1e1e2e;border-bottom:1px solid #313244;">
             <button class="btn btn-sm" id="grp-add-${tabId}">+ Add Member</button>
             <button class="btn btn-sm" id="grp-del-${tabId}">&#8722; Remove Selected</button>
             <button class="btn btn-sm btn-primary" id="grp-save-${tabId}" style="margin-left:auto;">&#128190; Save</button>
             <span id="grp-save-msg-${tabId}" style="font-size:11px;color:#a6adc8;"></span>
           </div>
           <div id="grp-mem-grid-${tabId}" style="height:calc(100% - 34px);"></div>`;

        const memGrid = new Tabulator('#grp-mem-grid-' + tabId, { /* global Tabulator */
          data: (resp.members || []).map(u => ({ user: u })),
          layout: 'fitDataFill',
          selectable: 1,
          height: '100%',
          placeholder: '(no members)',
          columns: [{
            title: 'User Name', field: 'user', widthGrow: 1,
            editor: 'list',
            editorParams: { values: availMap, autocomplete: true, clearable: true },
            headerSort: true,
            formatter: v => escHtml(v.getValue()),
          }],
        });

        document.getElementById('grp-add-' + tabId)?.addEventListener('click', () => {
          const inGrid = new Set(memGrid.getData().map(r => (r.user || '').toLowerCase()));
          const opts   = (resp.allUsers || []).filter(u => !inGrid.has(u.toLowerCase()));
          if (opts.length === 0) return;
          memGrid.addRow({ user: opts[0] }, false);
        });

        document.getElementById('grp-del-' + tabId)?.addEventListener('click', () =>
          memGrid.getSelectedRows().forEach(r => r.delete()));

        const saveMsg = document.getElementById('grp-save-msg-' + tabId);
        document.getElementById('grp-save-' + tabId)?.addEventListener('click', async () => {
          if (saveMsg) saveMsg.textContent = 'Saving…';
          try {
            const members = memGrid.getData().map(r => r.user).filter(u => u.trim() !== '');
            const r = await apiFetch('api/save_group_members.php', {
              method: 'POST', headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ dd, group: groupName, members }),
            });
            if (saveMsg) saveMsg.textContent = r.error
              ? `Error: ${r.error}`
              : `Saved (+${r.added} −${r.removed})` + (r.errors?.length ? ` • ${r.errors[0]}` : '');
          } catch (err) {
            if (saveMsg) saveMsg.textContent = `Error: ${err.message}`;
          }
        });
      })
      .catch(err => {
        if (panel) panel.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
      });
  }

  // ── User groups tab ─────────────────────────────────────────────────────────
  function openUserTab(dd, userName) {
    const key = `${dd}.user.${userName}`;
    const existing = state.tabs.find(t => t.type === 'user' && t.metaKey === key);
    if (existing) { activateTab(existing.id); return; }
    const id = 'tab-' + (state.nextTabId++);
    state.tabs.push({ id, title: `User: ${userName}`, type: 'user', dd, userName, metaKey: key });
    renderTabs();
    activateTab(id);
    loadUserData(id, dd, userName);
  }

  function loadUserData(tabId, dd, userName) {
    const container = document.getElementById('user-' + tabId);
    if (!container) return;
    Promise.all([
      apiFetch(`api/user_groups.php?dd=${encodeURIComponent(dd)}&user=${encodeURIComponent(userName)}`),
      apiFetch(`api/user_meta.php?dd=${encodeURIComponent(dd)}&user=${encodeURIComponent(userName)}`),
    ]).then(([groupsResp, permsResp]) => {
      if (groupsResp.error) {
        container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(groupsResp.error)}</div>`;
        return;
      }

      const inheritBadge = permsResp?.canInherit
        ? `<span style="font-size:11px;color:#a6e3a1;margin-left:8px;" title="User inherits rights from group memberships">&#8679; Inherits from groups</span>`
        : '';

      container.innerHTML =
        `<div style="padding:4px 6px;display:flex;gap:8px;align-items:center;background:#1e1e2e;border-bottom:1px solid #313244;">
           <span style="font-size:12px;color:#89b4fa;font-weight:600;">Group Memberships</span>
           <button class="btn btn-sm" id="add-group-${tabId}">+ Add Group</button>
           <button class="btn btn-sm" id="del-group-${tabId}">&#8722; Remove Selected</button>
           <button class="btn btn-sm btn-primary" id="save-grp-${tabId}">&#128190; Save</button>
           <span id="save-grp-msg-${tabId}" style="font-size:11px;color:#a6adc8;"></span>
         </div>
         <div id="user-grp-${tabId}" style="flex:0 0 160px;min-height:0;overflow:hidden;"></div>
         <div id="user-grp-builtin-${tabId}" style="border-bottom:2px solid #313244;background:#1e1e2e;min-height:22px;"></div>
         <div style="padding:4px 6px;display:flex;gap:8px;align-items:center;background:#1e1e2e;border-bottom:1px solid #313244;">
           <button class="perm-tab-btn btn btn-sm" id="ptab-direct-${tabId}"
                   style="background:#313244;color:#cdd6f4;border:1px solid #45475a;"
                   data-ptab="direct">Direct Permissions</button>
           <button class="perm-tab-btn btn btn-sm" id="ptab-effective-${tabId}"
                   style="color:#a6adc8;border:1px solid transparent;"
                   data-ptab="effective">Effective Permissions</button>
           ${inheritBadge}
           <button class="btn btn-sm btn-primary" id="save-perm-${tabId}" style="margin-left:auto;">&#128190; Save Changes</button>
           <span id="save-perm-msg-${tabId}" style="font-size:11px;color:#a6adc8;"></span>
           <span id="perm-ro-note-${tabId}" style="font-size:11px;color:#585b70;">Alter/Drop columns are read-only</span>
         </div>
         <div id="user-perm-${tabId}" style="flex:1;min-height:0;overflow:hidden;"></div>
         <div id="user-eff-${tabId}"  style="flex:1;min-height:0;overflow:hidden;display:none;"></div>`;

      // ── Built-in DB: group badges (read-only, cannot be edited via OpenADS) ──
      const builtins = groupsResp.builtinGroups || [];
      const builtinEl = document.getElementById('user-grp-builtin-' + tabId);
      if (builtinEl) {
        const badges = builtins.map(g =>
          `<span style="display:inline-block;background:#313244;color:#a6adc8;border-radius:4px;
                        padding:1px 7px;font-size:11px;margin:2px;" title="SAP built-in group — managed by per-user cipher, read-only in OpenADS">${escHtml(g)}</span>`
        ).join('');
        const note = groupsResp.dbGroupNote || '';
        builtinEl.innerHTML = badges
          ? `<div style="padding:2px 6px;">${badges}
               <span style="font-size:10px;color:#585b70;margin-left:4px;" title="${escHtml(note)}">&#9432;</span>
             </div>`
          : `<div style="padding:2px 6px;font-size:11px;color:#585b70;">
               No SAP built-in groups decoded.
               <span title="${escHtml(note)}" style="cursor:default;">&#9432; DB:Admin/Backup/Debug not visible (per-user cipher)</span>
             </div>`;
      }

      // ── Group membership grid ────────────────────────────────────────────────
      const currentSet  = new Set((groupsResp.groups || []).map(g => g.toLowerCase()));
      const available   = (groupsResp.allGroups || []).filter(g => !currentSet.has(g.toLowerCase()));
      const availValues = Object.fromEntries(available.map(g => [g, g]));

      const grpGrid = new Tabulator('#user-grp-' + tabId, { /* global Tabulator */
        data: (groupsResp.groups || []).map(g => ({ group: g })),
        layout: 'fitDataFill',
        selectable: 1,
        height: '100%',
        placeholder: '(user is not a member of any groups)',
        columns: [{ title: 'Group Name', field: 'group', widthGrow: 1, editor: 'list',
          editorParams: { values: availValues, autocomplete: true, clearable: true },
          headerSort: true,
          formatter: v => escHtml(v.getValue()),
        }],
      });

      document.getElementById('add-group-' + tabId)?.addEventListener('click', () => {
        const inGrid = new Set(grpGrid.getData().map(r => (r.group || '').toLowerCase()));
        const opts = (groupsResp.allGroups || []).filter(g => !inGrid.has(g.toLowerCase()));
        if (opts.length === 0) return;
        grpGrid.addRow({ group: opts[0] }, false);
      });
      document.getElementById('del-group-' + tabId)?.addEventListener('click', () =>
        grpGrid.getSelectedRows().forEach(r => r.delete()));

      const saveGrpBtn = document.getElementById('save-grp-' + tabId);
      const saveGrpMsg = document.getElementById('save-grp-msg-' + tabId);
      saveGrpBtn?.addEventListener('click', async () => {
        if (saveGrpMsg) saveGrpMsg.textContent = 'Saving…';
        try {
          const groups = grpGrid.getData().map(r => r.group).filter(g => g.trim() !== '');
          const r = await apiFetch('api/save_user_groups.php', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ dd, user: userName, groups }),
          });
          if (saveGrpMsg) saveGrpMsg.textContent = r.error ? `Error: ${r.error}` : 'Saved';
        } catch (err) {
          if (saveGrpMsg) saveGrpMsg.textContent = `Error: ${err.message}`;
        }
      });

      // ── Direct permissions grid ──────────────────────────────────────────────
      if (permsResp?.error || !permsResp?.data) {
        const el = document.getElementById('user-perm-' + tabId);
        if (el) el.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(permsResp?.error || 'Failed to load permissions')}</div>`;
      } else {
        const TYPE_LABEL   = { 1:'Table', 4:'Field', 6:'View', 10:'Stored Proc', 18:'Function' };
        const isExecType   = t => t === 10 || t === 18;
        const isFieldType  = t => t === 4;
        const isSchemaType = t => t === 1 || t === 6;

        const DASH = '<span style="color:#45475a;font-size:13px;display:block;text-align:center;">—</span>';
        const check = (on, editable) => on
          ? `<span style="color:${editable?'#a6e3a1':'#6a9f7e'};font-size:16px;display:block;text-align:center;${editable?'cursor:pointer':'opacity:0.6'};">✓</span>`
          : `<span style="color:${editable?'#f38ba8':'#9a6f72'};font-size:16px;display:block;text-align:center;${editable?'cursor:pointer':'opacity:0.6'};">✗</span>`;

        const permCol = (title, field, readonly = false) => ({
          title, field, width: 72, headerSort: false,
          formatter: (cell) => {
            const t = cell.getRow().getData().type;
            if (field === 'canExec')   return isExecType(t)   ? check(cell.getValue()==='Yes', !readonly) : DASH;
            if (field === 'canDelete') return (isFieldType(t) || isExecType(t)) ? DASH : check(cell.getValue()==='Yes', !readonly);
            if (field === 'canAlter' || field === 'canDrop') return isSchemaType(t) ? check(cell.getValue()==='Yes', false) : DASH;
            if (isExecType(t)) return DASH;
            return check(cell.getValue()==='Yes', !(isFieldType(t) || readonly));
          },
          cellClick: (_e, cell) => {
            if (readonly) return;
            const t = cell.getRow().getData().type;
            if (isFieldType(t)) return;
            if (field === 'canExec'   && !isExecType(t)) return;
            if (field !== 'canExec'   &&  isExecType(t)) return;
            if (field === 'canDelete' &&  isExecType(t)) return;
            if (field === 'canAlter'  || field === 'canDrop') return;
            cell.setValue(cell.getValue() === 'Yes' ? 'No' : 'Yes');
          },
        });

        const permGrid = new Tabulator('#user-perm-' + tabId, { /* global Tabulator */
          data: permsResp.data,
          layout: 'fitDataFill',
          pagination: 'local', paginationSize: 500, height: '100%',
          placeholder: '(no permission records for this user)',
          rowFormatter: row => {
            if (row.getData().type === 4)
              row.getElement().style.background = '#181825';
          },
          columns: [
            { title: 'Object', field: 'object', widthGrow: 3, headerSort: true },
            { title: 'Parent', field: 'parent', width: 130, headerSort: true,
              formatter: cell => cell.getValue() ? escHtml(cell.getValue()) : '' },
            { title: 'Type', field: 'type', width: 100, headerSort: false,
              formatter: cell => TYPE_LABEL[cell.getValue()] || `Type ${cell.getValue()}` },
            permCol('Select',  'canSelect'),
            permCol('Insert',  'canInsert'),
            permCol('Update',  'canUpdate'),
            permCol('Delete',  'canDelete'),
            permCol('Execute', 'canExec'),
            permCol('Alter',   'canAlter', true),
            permCol('Drop',    'canDrop',  true),
          ],
        });

        const savePermBtn = document.getElementById('save-perm-' + tabId);
        const savePermMsg = document.getElementById('save-perm-msg-' + tabId);
        savePermBtn?.addEventListener('click', async () => {
          if (savePermMsg) savePermMsg.textContent = 'Saving…';
          try {
            const r = await apiFetch('api/save_user_meta.php', {
              method: 'POST', headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({ dd, user: userName, rows: permGrid.getData() }),
            });
            if (savePermMsg) savePermMsg.textContent = r.error ? `Error: ${r.error}` : `Saved ${r.saved} permission(s)`;
          } catch (err) {
            if (savePermMsg) savePermMsg.textContent = `Error: ${err.message}`;
          }
        });
      }

      // ── Permission sub-tab switching (Direct / Effective) ─────────────────────
      const permDirect  = document.getElementById('user-perm-' + tabId);
      const permEff     = document.getElementById('user-eff-'  + tabId);
      const savePermBtn = document.getElementById('save-perm-' + tabId);
      const permRoNote  = document.getElementById('perm-ro-note-' + tabId);
      const btnDirect   = document.getElementById('ptab-direct-'    + tabId);
      const btnEff      = document.getElementById('ptab-effective-' + tabId);
      let effLoaded = false;

      const switchPermTab = (which) => {
        const isDirect = which === 'direct';
        if (permDirect) permDirect.style.display = isDirect  ? '' : 'none';
        if (permEff)    permEff.style.display     = !isDirect ? '' : 'none';
        if (savePermBtn) savePermBtn.style.display = isDirect  ? '' : 'none';
        if (permRoNote)  permRoNote.style.display  = isDirect  ? '' : 'none';

        const activeStyle   = 'background:#313244;color:#cdd6f4;border:1px solid #45475a;';
        const inactiveStyle = 'color:#a6adc8;border:1px solid transparent;background:transparent;';
        if (btnDirect) btnDirect.style.cssText   = isDirect  ? activeStyle : inactiveStyle;
        if (btnEff)    btnEff.style.cssText       = !isDirect ? activeStyle : inactiveStyle;

        if (!isDirect && !effLoaded) {
          effLoaded = true;
          loadEffectivePermissions(tabId, dd, userName, permsResp?.canInherit ?? false);
        }
      };

      btnDirect?.addEventListener('click', () => switchPermTab('direct'));
      btnEff?.addEventListener('click',    () => switchPermTab('effective'));
    })
    .catch(err => {
      if (container) container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
    });
  }

  // ── Effective permissions grid (read-only) ──────────────────────────────────
  function loadEffectivePermissions(tabId, dd, userName, canInherit) {
    const effEl = document.getElementById('user-eff-' + tabId);
    if (!effEl) return;
    effEl.innerHTML = '<div style="padding:8px;color:#a6adc8;font-size:12px;">Loading…</div>';

    apiFetch(`api/user_effective_permissions.php?dd=${encodeURIComponent(dd)}&user=${encodeURIComponent(userName)}`)
      .then(resp => {
        if (resp.error) {
          effEl.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(resp.error)}</div>`;
          return;
        }

        const groups = resp.groups || [];
        const groupList = groups.length
          ? groups.map(g => `<span style="background:#313244;border-radius:3px;padding:1px 6px;font-size:11px;color:#cba6f7;">${escHtml(g)}</span>`).join(' ')
          : '<span style="color:#585b70;font-size:11px;">no groups</span>';

        const legend = canInherit
          ? `<div style="padding:3px 6px;font-size:11px;color:#a6adc8;background:#181825;border-bottom:1px solid #313244;">
               Inheriting from: ${groupList}
               &nbsp;&nbsp;
               <span style="color:#a6e3a1;">&#9679;</span> Direct
               <span style="color:#cba6f7;margin-left:6px;">&#9679;</span> Inherited
               <span style="color:#89b4fa;margin-left:6px;">&#9679;</span> Direct + Inherited
               <span style="color:#45475a;margin-left:6px;">&#9679;</span> Not granted
             </div>`
          : `<div style="padding:3px 6px;font-size:11px;color:#f38ba8;background:#181825;border-bottom:1px solid #313244;">
               Inheritance is disabled for this user — effective permissions equal direct permissions only.
             </div>`;

        const TYPE_LABEL  = { 1:'Table', 4:'Field', 6:'View', 10:'Stored Proc', 18:'Function' };
        const isExecType  = t => t === 10 || t === 18;
        const isFieldType = t => t === 4;
        const isSchemaType= t => t === 1 || t === 6;

        const DASH = '<span style="color:#45475a;font-size:13px;display:block;text-align:center;">—</span>';

        // Color by source: Direct=green, Inherited=cyan, Both=blue, None=red
        const effCheck = (on, src) => {
          if (!on) return `<span style="color:#45475a;font-size:16px;display:block;text-align:center;opacity:0.5;">✗</span>`;
          const isDirect   = src.includes('Direct');
          const isInherit  = src.split('+').some(s => s !== 'Direct' && s !== '');
          let color;
          if (isDirect && isInherit) color = '#89b4fa';   // blue   — both
          else if (isDirect)         color = '#a6e3a1';   // green  — direct only
          else                       color = '#cba6f7';   // mauve  — inherited only
          const tip = src || '';
          return `<span style="color:${color};font-size:16px;display:block;text-align:center;" title="${escHtml(tip)}">✓</span>`;
        };

        const effCol = (title, field, srcField) => ({
          title, field, width: 72, headerSort: false,
          formatter: (cell) => {
            const row = cell.getRow().getData();
            const t   = row.type;
            if (field === 'canExec')   return isExecType(t)  ? effCheck(cell.getValue()==='Yes', row[srcField] || '') : DASH;
            if (field === 'canDelete') return (isFieldType(t) || isExecType(t)) ? DASH : effCheck(cell.getValue()==='Yes', row[srcField] || '');
            if (field === 'canAlter' || field === 'canDrop') return isSchemaType(t) ? effCheck(cell.getValue()==='Yes', row[srcField] || '') : DASH;
            if (isExecType(t)) return DASH;
            return effCheck(cell.getValue()==='Yes', !(isFieldType(t)) ? (row[srcField] || '') : '');
          },
        });

        // Build a wrapper that holds the legend + the grid div
        effEl.innerHTML = legend + `<div id="user-eff-grid-${tabId}" style="height:calc(100% - 28px);"></div>`;

        new Tabulator('#user-eff-grid-' + tabId, { /* global Tabulator */
          data: resp.data,
          layout: 'fitDataFill',
          pagination: 'local', paginationSize: 500, height: '100%',
          placeholder: '(no permissions for this user)',
          rowFormatter: row => {
            if (row.getData().type === 4) row.getElement().style.background = '#181825';
          },
          columns: [
            { title: 'Object', field: 'object', widthGrow: 3, headerSort: true },
            { title: 'Parent', field: 'parent', width: 130, headerSort: true,
              formatter: cell => cell.getValue() ? escHtml(cell.getValue()) : '' },
            { title: 'Type', field: 'type', width: 100, headerSort: false,
              formatter: cell => TYPE_LABEL[cell.getValue()] || `Type ${cell.getValue()}` },
            effCol('Select',  'canSelect', 'srcSelect'),
            effCol('Insert',  'canInsert', 'srcInsert'),
            effCol('Update',  'canUpdate', 'srcUpdate'),
            effCol('Delete',  'canDelete', 'srcDelete'),
            effCol('Execute', 'canExec',   'srcExec'),
            effCol('Alter',   'canAlter',  'srcAlter'),
            effCol('Drop',    'canDrop',   'srcDrop'),
          ],
        });
      })
      .catch(err => {
        if (effEl) effEl.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
      });
  }

  // ── RI Object form tab ──────────────────────────────────────────────────────
  function openRiTab(dd, riName) {
    const key = `${dd}.ri.${riName}`;
    const existing = state.tabs.find(t => t.type === 'ri' && t.metaKey === key);
    if (existing) { activateTab(existing.id); return; }
    const id = 'tab-' + (state.nextTabId++);
    state.tabs.push({ id, title: `RI: ${riName}`, type: 'ri', dd, riName, metaKey: key });
    renderTabs();
    activateTab(id);
    loadRiData(id, dd, riName);
  }

  function loadRiData(tabId, dd, riName) {
    const container = document.getElementById('ri-' + tabId);
    if (!container) return;
    Promise.all([
      apiFetch(`api/ri_meta.php?dd=${encodeURIComponent(dd)}&ri=${encodeURIComponent(riName)}`),
      apiFetch(`api/ri_meta.php?dd=${encodeURIComponent(dd)}&action=tables`),
    ]).then(([riResp, tablesResp]) => {
      if (riResp.error) {
        container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(riResp.error)}</div>`;
        return;
      }
      const ri         = riResp.ri || {};
      const tables     = tablesResp.tables || [];
      const parentTags = riResp.parentTags || [];
      const childTags  = riResp.childTags  || [];

      const mkOpts  = (list, sel) => list.map(t => `<option${t===sel?' selected':''}>${escHtml(t)}</option>`).join('');
      const tagOpts = (list, sel) => list.map(t =>
        `<option${t.toLowerCase()===(sel||'').toLowerCase()?' selected':''}>${escHtml(t)}</option>`
      ).join('');
      const ruleOpts = (sel) => ['Restrict','Cascade','SetNull'].map(v => `<option${v===sel?' selected':''}>${v}</option>`).join('');

      container.innerHTML = `
        <div style="padding:16px;max-width:560px;display:flex;flex-direction:column;gap:14px;">
          <h3 style="margin:0;color:#cdd6f4;">RI Object: ${escHtml(riName)}</h3>
          <div style="display:grid;grid-template-columns:140px 1fr;gap:8px;align-items:center;">
            <label>Parent Table</label>
            <select id="ri-ptbl-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:4px;border-radius:4px;">
              ${mkOpts(tables, ri.parent)}
            </select>
            <label>Primary Key Tag</label>
            <select id="ri-ptag-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:4px;border-radius:4px;">
              ${tagOpts(parentTags, ri.parent_tag)}
            </select>
            <label>Child Table</label>
            <select id="ri-ctbl-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:4px;border-radius:4px;">
              ${mkOpts(tables, ri.child)}
            </select>
            <label>Foreign Key Tag</label>
            <select id="ri-ctag-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:4px;border-radius:4px;">
              ${tagOpts(childTags, ri.child_tag)}
            </select>
            <label>Update Rule</label>
            <select id="ri-urule-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:4px;border-radius:4px;">
              ${ruleOpts(ri.update_opt)}
            </select>
            <label>Delete Rule</label>
            <select id="ri-drule-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:4px;border-radius:4px;">
              ${ruleOpts(ri.delete_opt)}
            </select>
          </div>
          <div style="display:flex;gap:8px;align-items:center;">
            <button class="btn btn-primary" id="save-ri-${tabId}">&#128190; Save RI</button>
            <button class="btn" id="del-ri-${tabId}" style="color:#f38ba8;">&#128465; Delete RI</button>
            <span id="save-ri-msg-${tabId}" style="font-size:11px;color:#a6adc8;"></span>
          </div>
        </div>`;

      // Re-fetch tags when the user changes the parent or child table
      const loadTags = async (tableId, tagSelectId) => {
        const tbl = document.getElementById(tableId)?.value?.trim();
        if (!tbl) return;
        const tagSel = document.getElementById(tagSelectId);
        if (!tagSel) return;
        try {
          const r = await apiFetch(`api/ri_meta.php?dd=${encodeURIComponent(dd)}&action=tags&table=${encodeURIComponent(tbl)}`);
          if (r.error) console.warn('loadTags error for', tbl, ':', r.error);
          const tags = r.tags || [];
          tagSel.innerHTML = tags.map(t => `<option>${escHtml(t)}</option>`).join('');
        } catch (e) { console.warn('loadTags fetch error:', e); }
      };

      document.getElementById('ri-ptbl-' + tabId)?.addEventListener('change', () => loadTags('ri-ptbl-' + tabId, 'ri-ptag-' + tabId));
      document.getElementById('ri-ctbl-' + tabId)?.addEventListener('change', () => loadTags('ri-ctbl-' + tabId, 'ri-ctag-' + tabId));

      const msgEl = document.getElementById('save-ri-msg-' + tabId);
      document.getElementById('save-ri-' + tabId)?.addEventListener('click', async () => {
        if (msgEl) msgEl.textContent = 'Saving…';
        const payload = {
          dd, ri: riName,
          parent: document.getElementById('ri-ptbl-'  + tabId)?.value || '',
          parent_tag: document.getElementById('ri-ptag-'+ tabId)?.value || '',
          child:  document.getElementById('ri-ctbl-'  + tabId)?.value || '',
          child_tag:  document.getElementById('ri-ctag-'+ tabId)?.value || '',
          update_opt: document.getElementById('ri-urule-'+ tabId)?.value || 'Restrict',
          delete_opt: document.getElementById('ri-drule-'+ tabId)?.value || 'Restrict',
        };
        try {
          const r = await apiFetch('api/save_ri.php', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'save', ...payload }),
          });
          if (msgEl) msgEl.textContent = r.error ? `Error: ${r.error}` : 'Saved';
        } catch (err) { if (msgEl) msgEl.textContent = `Error: ${err.message}`; }
      });

      document.getElementById('del-ri-' + tabId)?.addEventListener('click', async () => {
        if (!confirm(`Delete RI object "${riName}"?`)) return;
        try {
          const r = await apiFetch('api/save_ri.php', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'delete', dd, ri: riName }),
          });
          if (r.error) { if (msgEl) msgEl.textContent = `Error: ${r.error}`; return; }
          closeTab(tabId);
          refreshTree();
        } catch (err) { if (msgEl) msgEl.textContent = `Error: ${err.message}`; }
      });
    }).catch(err => {
      if (container) container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
    });
  }

  // ── Trigger tab: resizable grid (top) + ACE editor (bottom) ──────────────────
  function buildTriggerPanel(tabId, tab) {
    const dd = tab.dd || '';
    const ddOpts = Array.from(state.openConnections).map(n =>
      `<option value="${escAttr(n)}" ${n === dd ? 'selected' : ''}>${escHtml(n)}</option>`
    ).join('');
    // ~36px header + 5×35px rows = 211px initial grid height
    return `
      <div class="sql-panel" style="flex-direction:column;overflow:hidden;">
        <div style="flex:0 0 auto;padding:4px 6px;display:flex;gap:8px;align-items:center;flex-wrap:wrap;background:#1e1e2e;border-bottom:1px solid #313244;">
          <button class="btn btn-sm" id="add-trig-${tabId}" style="background:#40a02b;color:#fff;">&#43; Add</button>
          <button class="btn btn-sm" id="del-trig-${tabId}" style="background:#d20f39;color:#fff;">&#128465; Delete</button>
          <button class="btn btn-sm btn-primary" id="save-trig-${tabId}">&#128190; Save</button>
          <span style="width:1px;height:18px;background:#45475a;display:inline-block;margin:0 2px;"></span>
          <label style="font-size:11px;color:#a6adc8;display:flex;align-items:center;gap:4px;cursor:pointer;">
            <input type="checkbox" id="trig-opt-values-${tabId}" checked> Pass __new/__old
          </label>
          <label style="font-size:11px;color:#a6adc8;display:flex;align-items:center;gap:4px;cursor:pointer;">
            <input type="checkbox" id="trig-opt-memos-${tabId}" checked> Include memos
          </label>
          <label style="font-size:11px;color:#a6adc8;display:flex;align-items:center;gap:4px;cursor:pointer;">
            <input type="checkbox" id="trig-opt-notxn-${tabId}"> No transaction
          </label>
          <span id="trig-save-msg-${tabId}" style="font-size:11px;color:#a6adc8;margin-left:4px;flex:1;"></span>
        </div>
        <div id="trig-add-form-${tabId}" style="display:none;flex:0 0 auto;padding:5px 8px;background:#181825;border-bottom:1px solid #313244;display:none;gap:6px;align-items:center;flex-wrap:wrap;">
          <span style="font-size:11px;color:#cdd6f4;font-weight:600;">New trigger:</span>
          <input id="trig-new-name-${tabId}" placeholder="Name" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:2px 6px;border-radius:3px;font-size:12px;width:130px;">
          <select id="trig-new-timing-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:2px 6px;border-radius:3px;font-size:12px;">
            <option value="BEFORE">BEFORE</option><option value="AFTER">AFTER</option><option value="INSTEAD OF">INSTEAD OF</option>
          </select>
          <select id="trig-new-event-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:2px 6px;border-radius:3px;font-size:12px;">
            <option value="INSERT">INSERT</option><option value="UPDATE">UPDATE</option><option value="DELETE">DELETE</option>
          </select>
          <input id="trig-new-prio-${tabId}" type="number" min="1" value="1" placeholder="Priority" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:2px 6px;border-radius:3px;font-size:12px;width:70px;">
          <button class="btn btn-sm" id="trig-add-ok-${tabId}" style="background:#40a02b;color:#fff;">Create</button>
          <button class="btn btn-sm" id="trig-add-cancel-${tabId}" style="background:#45475a;color:#cdd6f4;">Cancel</button>
          <span id="trig-add-msg-${tabId}" style="font-size:11px;color:#f38ba8;"></span>
        </div>
        <div id="trig-grid-wrap-${tabId}" style="flex:0 0 211px;min-height:120px;overflow:hidden;">
          <div id="trig-grid-${tabId}" style="height:100%;"></div>
        </div>
        <div id="trig-split-${tabId}" style="flex:0 0 7px;cursor:row-resize;background:#181825;border-top:1px solid #313244;border-bottom:1px solid #313244;display:flex;align-items:center;justify-content:center;">
          <div style="width:36px;height:3px;background:#45475a;border-radius:2px;"></div>
        </div>
        <div style="flex:1;display:flex;flex-direction:column;min-height:0;">
          <div class="sql-toolbar" style="flex:0 0 auto;">
            <select id="sql-dd-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:3px 8px;border-radius:4px;font-size:12px;">
              <option value="">— database —</option>
              ${ddOpts}
            </select>
            <button class="btn btn-primary" id="sql-run-${tabId}" title="Execute (F5)">&#9654; Execute</button>
            <span id="trig-label-${tabId}" style="font-size:11px;color:#89b4fa;margin-left:8px;font-weight:600;"></span>
            <span id="sql-msg-${tabId}" style="font-size:11px;color:#a6adc8;margin-left:8px;"></span>
          </div>
          <div class="sql-editor-wrap" style="flex:1;min-height:0;">
            <div id="sql-ace-${tabId}" class="sql-ace-editor" style="height:100%;"></div>
          </div>
        </div>
      </div>`;
  }

  // Set ACE editor to ADS SQL mode (HeidiSQL-like colours, // and /* */ comments)
  function setAdsMode(editor) {
    editor.setTheme('ace/theme/heidisql');
    editor.session.setMode('ace/mode/ads_sql');
  }

  function bindTriggerPanel(tabId, tab) {
    setTimeout(() => {
      const editor = ace.edit('sql-ace-' + tabId);
      setAdsMode(editor);
      editor.setOptions({
        showPrintMargin: false, useWorker: false, fontSize: '13px',
        fontFamily: '"Cascadia Code", "Fira Code", Consolas, monospace',
        tabSize: 2, useSoftTabs: true, scrollPastEnd: 0.5,
      });
      editor.setValue('-- Select a trigger above to view its body.', -1);
      state.aceEditors[tabId] = editor;

      const runBtn = document.getElementById('sql-run-' + tabId);
      runBtn?.addEventListener('click', () => doExecuteSql(tabId, editor.getValue().trim()));
      editor.commands.addCommand({
        name: 'executeAll', bindKey: { win: 'F5', mac: 'F5' },
        exec: () => doExecuteSql(tabId, editor.getValue().trim()),
      });

      // Splitter drag-resize between grid and editor
      const splitter = document.getElementById('trig-split-' + tabId);
      const gridWrap = document.getElementById('trig-grid-wrap-' + tabId);
      if (splitter && gridWrap) {
        let dragY = null, startH = null;
        splitter.addEventListener('mousedown', e => {
          e.preventDefault();
          dragY  = e.clientY;
          startH = gridWrap.offsetHeight;
          const onMove = ev => {
            if (dragY === null) return;
            const newH = Math.max(90, Math.min(700, startH + ev.clientY - dragY));
            gridWrap.style.flex = `0 0 ${newH}px`;
          };
          const onUp = () => {
            dragY = null;
            document.removeEventListener('mousemove', onMove);
            document.removeEventListener('mouseup', onUp);
            // Force ACE editor to re-measure after resize
            state.aceEditors[tabId]?.resize();
          };
          document.addEventListener('mousemove', onMove);
          document.addEventListener('mouseup', onUp);
        });
      }

      loadTriggerData(tabId, tab.dd, tab.table);
    }, 60);
  }

  function loadTriggerData(tabId, dd, table, restoreName) {
    const gridEl  = document.getElementById('trig-grid-' + tabId);
    const labelEl = document.getElementById('trig-label-' + tabId);
    const msgEl   = document.getElementById('trig-save-msg-' + tabId);
    if (!gridEl) return;

    apiFetch('api/trigger_body.php', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ dd, table }),
    }).then(resp => {
      if (resp.error) {
        gridEl.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(resp.error)}</div>`;
        return;
      }
      const triggers = resp.triggers || [];

      // Add _origName so we always know what key to use for save/delete
      triggers.forEach(t => { t._origName = t.name; });

      /* global Tabulator */
      const grid = new Tabulator('#trig-grid-' + tabId, {
        data: triggers,
        layout: 'fitDataFill',
        selectable: 1,
        selectableRollingSelection: false,
        placeholder: '(no triggers defined for this table)',
        columns: [
          { title: 'Name',     field: 'name',     widthGrow: 2,  minWidth: 120, headerSort: false,
            editor: 'input' },
          { title: 'Timing',   field: 'timing',   width: 120,    headerSort: false,
            editor: 'list', editorParams: { values: { BEFORE: 'BEFORE', 'INSTEAD OF': 'INSTEAD OF', AFTER: 'AFTER' } } },
          { title: 'Event',    field: 'event',    width: 90,     headerSort: false,
            editor: 'list', editorParams: { values: { INSERT: 'INSERT', UPDATE: 'UPDATE', DELETE: 'DELETE' } } },
          { title: 'Enabled',  field: 'enabled',  width: 80,     headerSort: false,
            editor: 'list', editorParams: { values: { Yes: 'Yes', No: 'No' } } },
          { title: 'Priority', field: 'priority', width: 75,     headerSort: false,
            editor: 'number', editorParams: { min: 1, step: 1 } },
        ],
      });

      // Validate uniqueness of (timing, event) when a cell is edited
      grid.on('cellEdited', cell => {
        const field = cell.getField();
        if (field !== 'timing' && field !== 'event') return;
        const row = cell.getRow();
        const d   = row.getData();
        const dups = grid.getData().filter(r =>
          r._origName !== d._origName &&
          r.timing === d.timing &&
          r.event  === d.event
        );
        if (dups.length > 0) {
          if (msgEl) msgEl.textContent = `⚠ A ${d.timing} ${d.event} trigger already exists`;
          msgEl.style.color = '#f38ba8';
          // Revert to previous value
          cell.restoreOldValue();
        } else {
          if (msgEl) { msgEl.textContent = ''; msgEl.style.color = ''; }
        }
      });

      const chkValues = document.getElementById('trig-opt-values-' + tabId);
      const chkMemos  = document.getElementById('trig-opt-memos-'  + tabId);
      const chkNoTxn  = document.getElementById('trig-opt-notxn-'  + tabId);

      const loadTrigBody = (d) => {
        const editor = state.aceEditors[tabId];
        if (!editor) return;
        const header = `-- Trigger: ${d.name}\n-- ${d.timing} ${d.event}\n\n`;
        editor.setValue(header + (d.body || '-- (body not available)'), -1);
        if (labelEl) labelEl.textContent = d.name;
        const opts = (typeof d.options === 'number') ? d.options : 0x03;
        if (chkValues) chkValues.checked = !!(opts & 0x01);
        if (chkMemos)  chkMemos.checked  = !!(opts & 0x02);
        if (chkNoTxn)  chkNoTxn.checked  = !!(opts & 0x04);
      };

      grid.on('rowClick', (e, row) => {
        grid.getRows().forEach(r => r.getElement().style.background = '');
        row.getElement().style.background = '#313244';
        row.select();
        loadTrigBody(row.getData());
      });

      // ── Save ──────────────────────────────────────────────────────────────
      const doSave = async () => {
        const label = labelEl?.textContent || '';
        if (!label) { if (msgEl) { msgEl.textContent = 'Select a trigger row first'; msgEl.style.color='#f38ba8'; } return; }
        const allRows = grid.getData();
        const row = allRows.find(r => r._origName === label || r.name === label);
        if (!row) { if (msgEl) { msgEl.textContent = 'Trigger row not found'; msgEl.style.color='#f38ba8'; } return; }

        // Uniqueness check before save
        const conflict = allRows.find(r =>
          r._origName !== row._origName &&
          r.timing === row.timing && r.event === row.event
        );
        if (conflict) {
          if (msgEl) { msgEl.textContent = `⚠ Duplicate ${row.timing} ${row.event} trigger`; msgEl.style.color='#f38ba8'; }
          return;
        }

        if (msgEl) { msgEl.textContent = 'Saving…'; msgEl.style.color = ''; }
        const editor = state.aceEditors[tabId];
        const rawBody  = editor ? editor.getValue() : '';
        const bodyLines = rawBody.split('\n');
        const sqlStart  = bodyLines.findIndex(l => !l.startsWith('-- '));
        const sqlBody   = bodyLines.slice(sqlStart < 0 ? 0 : sqlStart).join('\n').trim();
        const opts = ((chkValues?.checked ? 0x01 : 0) |
                      (chkMemos?.checked  ? 0x02 : 0) |
                      (chkNoTxn?.checked  ? 0x04 : 0));
        try {
          const r = await apiFetch('api/save_trigger.php', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
              dd, table,
              name:     row._origName,
              event:    row.event,
              timing:   row.timing,
              enabled:  row.enabled,
              priority: row.priority,
              body:     sqlBody,
              options:  opts,
            }),
          });
          if (r.error) {
            if (msgEl) { msgEl.textContent = `Error: ${r.error}`; msgEl.style.color = '#f38ba8'; }
          } else {
            if (msgEl) { msgEl.textContent = `Saved ${row._origName}`; msgEl.style.color = '#a6e3a1'; }
            // If name changed, reload grid (delete+create would be needed — for now just flag)
            if (row.name !== row._origName) loadTriggerData(tabId, dd, table, row.name);
          }
        } catch (err) {
          if (msgEl) { msgEl.textContent = `Error: ${err.message}`; msgEl.style.color = '#f38ba8'; }
        }
      };
      document.getElementById('save-trig-' + tabId)?.addEventListener('click', doSave);

      // ── Add trigger form ──────────────────────────────────────────────────
      const addForm   = document.getElementById('trig-add-form-' + tabId);
      const addMsgEl  = document.getElementById('trig-add-msg-'  + tabId);
      document.getElementById('add-trig-' + tabId)?.addEventListener('click', () => {
        if (addForm) {
          addForm.style.display = addForm.style.display === 'none' ? 'flex' : 'none';
          if (addForm.style.display === 'flex') {
            document.getElementById('trig-new-name-' + tabId)?.focus();
          }
        }
      });
      document.getElementById('trig-add-cancel-' + tabId)?.addEventListener('click', () => {
        if (addForm) addForm.style.display = 'none';
      });
      document.getElementById('trig-add-ok-' + tabId)?.addEventListener('click', async () => {
        const nameEl   = document.getElementById('trig-new-name-'   + tabId);
        const timingEl = document.getElementById('trig-new-timing-' + tabId);
        const eventEl  = document.getElementById('trig-new-event-'  + tabId);
        const prioEl   = document.getElementById('trig-new-prio-'   + tabId);
        const newName  = nameEl?.value.trim() || '';
        const newTiming = timingEl?.value || 'BEFORE';
        const newEvent  = eventEl?.value  || 'INSERT';
        const newPrio   = Math.max(1, parseInt(prioEl?.value || '1', 10));
        if (!newName) { if (addMsgEl) addMsgEl.textContent = 'Name is required'; return; }
        // Client-side uniqueness check
        const existing = grid.getData();
        if (existing.find(r => r.name === newName)) {
          if (addMsgEl) addMsgEl.textContent = `Trigger "${newName}" already exists`; return;
        }
        if (existing.find(r => r.timing === newTiming && r.event === newEvent)) {
          if (addMsgEl) addMsgEl.textContent = `A ${newTiming} ${newEvent} trigger already exists`; return;
        }
        if (addMsgEl) addMsgEl.textContent = '';
        try {
          const r = await apiFetch('api/create_trigger.php', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ dd, table, name: newName, timing: newTiming, event: newEvent, priority: newPrio }),
          });
          if (r.error) { if (addMsgEl) addMsgEl.textContent = r.error; return; }
          if (addForm) addForm.style.display = 'none';
          if (nameEl) nameEl.value = '';
          loadTriggerData(tabId, dd, table, newName);
        } catch (err) {
          if (addMsgEl) addMsgEl.textContent = err.message;
        }
      });

      // ── Delete trigger ────────────────────────────────────────────────────
      document.getElementById('del-trig-' + tabId)?.addEventListener('click', async () => {
        const label = labelEl?.textContent || '';
        if (!label) { if (msgEl) { msgEl.textContent = 'Select a trigger row first'; msgEl.style.color='#f38ba8'; } return; }
        const row = grid.getData().find(r => r._origName === label || r.name === label);
        if (!row) return;
        if (!confirm(`Delete trigger "${row._origName}"?`)) return;
        try {
          const r = await apiFetch('api/delete_trigger.php', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ dd, table, name: row._origName }),
          });
          if (r.error) {
            if (msgEl) { msgEl.textContent = `Error: ${r.error}`; msgEl.style.color='#f38ba8'; }
          } else {
            if (labelEl) labelEl.textContent = '';
            const editor = state.aceEditors[tabId];
            if (editor) editor.setValue('-- Select a trigger above to view its body.', -1);
            loadTriggerData(tabId, dd, table);
          }
        } catch (err) {
          if (msgEl) { msgEl.textContent = `Error: ${err.message}`; msgEl.style.color='#f38ba8'; }
        }
      });

      // Select first row (or restoreName row after reload)
      if (triggers.length >= 1) {
        setTimeout(() => {
          let target = null;
          if (restoreName) target = grid.getRows().find(r => r.getData().name === restoreName);
          if (!target) target = grid.getRows()[0];
          if (target) {
            grid.getRows().forEach(r => r.getElement().style.background = '');
            target.getElement().style.background = '#313244';
            target.select();
            loadTrigBody(target.getData());
          }
        }, 150);
      }
    }).catch(err => {
      if (gridEl) gridEl.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
    });
  }

  // ── ADS field type choices for the editor dropdown ─────────────────────────
  const ADS_FIELD_TYPES = {
    Character: 'Character', CICharacter: 'CICharacter', Varchar: 'Varchar',
    Memo: 'Memo', Integer: 'Integer', ShortInt: 'ShortInt', AutoIncrement: 'AutoIncrement',
    Numeric: 'Numeric', Float: 'Float', Double: 'Double', Money: 'Money',
    Logical: 'Logical', Date: 'Date', DateTime: 'DateTime', Timestamp: 'Timestamp',
    Blob: 'Blob', Binary: 'Binary',
  };

  function buildMetaColumns(kind) {
    if (kind === 'fields') {
      return [
        { title: '#',        field: 'Order',    width: 42,  minWidth: 42,  headerSort: false, formatter: 'plaintext' },
        { title: 'Field',    field: 'Field',    minWidth: 160, widthGrow: 2, headerSort: false, editor: 'input' },
        { title: 'Type',     field: 'BaseType', minWidth: 160, widthGrow: 2, headerSort: false,
          editor: 'list', editorParams: { values: ADS_FIELD_TYPES } },
        { title: 'Size',     field: 'Size',     width: 60,  headerSort: false,
          editor: 'number', editorParams: { min: 0 },
          formatter: v => (v.getValue() > 0 ? v.getValue() : '') },
        { title: 'Dec',      field: 'Decimals', width: 50,  headerSort: false,
          editor: 'number', editorParams: { min: 0 },
          formatter: v => (v.getValue() > 0 ? v.getValue() : '') },
        { title: 'Required', field: 'Required', width: 90,  headerSort: false,
          editor: 'list', editorParams: { values: { '': '—', True: 'Yes', False: 'No' } },
          formatter: v => v.getValue() === 'True' ? 'Yes' : v.getValue() === 'False' ? 'No' : '' },
        { title: 'Default',  field: 'Default',  minWidth: 100, widthGrow: 1.5, headerSort: false, editor: 'input' },
        { title: 'Index',    field: 'Index',    width: 80,  headerSort: false,
          editor: 'list', editorParams: { values: { No: 'No', Yes: 'Yes', Unique: 'Unique', Primary: 'Primary' } } },
      ];
    }
    if (kind === 'indexes') {
      return [
        { title: 'Tag',        field: 'Tag',        widthGrow: 1.5, headerSort: false, editor: 'input' },
        { title: 'Expression', field: 'Expression', widthGrow: 3,   headerSort: false, editor: 'input' },
        { title: 'Descending', field: 'Descending', width: 100, headerSort: false,
          editor: 'list', editorParams: { values: ['No', 'Yes'] } },
        { title: 'Unique',     field: 'Unique',     width: 80,  headerSort: false,
          editor: 'list', editorParams: { values: ['No', 'Yes'] } },
        { title: 'Primary',    field: 'Primary',    width: 80,  headerSort: false,
          editor: 'list', editorParams: { values: ['No', 'Yes'] } },
        { title: 'Binary',     field: 'Binary',     width: 80,  headerSort: false,
          editor: 'list', editorParams: { values: ['No', 'Yes'] } },
        { title: 'Key Type',   field: 'KeyType',    width: 90,  headerSort: false },
      ];
    }
    // triggers (plain meta view, editable columns for basic props)
    return [
      { title: 'Name',     field: 'Name',     widthGrow: 2, headerSort: false },
      { title: 'Timing',   field: 'Timing',   width: 110, headerSort: false },
      { title: 'Event',    field: 'Event',    widthGrow: 1, headerSort: false },
      { title: 'Enabled',  field: 'Enabled',  width: 80,  headerSort: false },
      { title: 'Priority', field: 'Priority', width: 80,  headerSort: false },
    ];
  }

  function loadMetaData(tabId, dd, table, kind) {
    const container = document.getElementById('meta-' + tabId);
    if (!container) return;
    fetch(`api/table_meta.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}&kind=${encodeURIComponent(kind)}`)
      .then(r => r.json())
      .then(resp => {
        if (resp.error) {
          container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(resp.error)}</div>`;
          return;
        }

        const showSave = kind === 'fields' || kind === 'indexes';
        const addIdxBtn = kind === 'indexes'
          ? `<button class="btn btn-sm" id="add-idx-btn-${tabId}" style="background:#a6e3a1;color:#1e1e2e;">+ Add Index</button>`
          : '';
        const delIdxBtn = kind === 'indexes'
          ? `<button class="btn btn-sm" id="del-idx-btn-${tabId}" disabled style="background:#f38ba8;color:#1e1e2e;opacity:0.5;cursor:not-allowed;">&#128465; Delete</button>`
          : '';
        const saveBtnExtra = kind === 'indexes'
          ? ' disabled style="opacity:0.5;cursor:not-allowed;"'
          : '';
        const toolbarHtml = showSave
          ? `<div style="padding:4px 6px;display:flex;gap:8px;align-items:center;background:#1e1e2e;border-bottom:1px solid #313244;">
               <button class="btn btn-sm btn-primary" id="save-meta-${tabId}"${saveBtnExtra}>&#128190; Save Changes</button>
               ${addIdxBtn}
               ${delIdxBtn}
               <span id="save-meta-msg-${tabId}" style="font-size:11px;color:#a6adc8;"></span>
             </div>`
          : '';
        const addIdxForm = kind === 'indexes'
          ? `<div id="add-idx-form-${tabId}" style="display:none;padding:4px 6px;gap:6px;align-items:center;flex-wrap:wrap;background:#181825;border-bottom:1px solid #313244;">
               <input id="new-idx-tag-${tabId}"  placeholder="Tag name"   style="width:120px;padding:2px 6px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:4px;" />
               <input id="new-idx-expr-${tabId}" placeholder="Expression" style="width:240px;padding:2px 6px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:4px;" />
               <select id="new-idx-desc-${tabId}" style="padding:2px 4px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:4px;" title="Sort order">
                 <option value="No">Ascending</option><option value="Yes">Descending</option>
               </select>
               <select id="new-idx-uniq-${tabId}" style="padding:2px 4px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:4px;" title="Unique">
                 <option value="No">Not Unique</option><option value="Yes">Unique</option>
               </select>
               <select id="new-idx-bin-${tabId}" style="padding:2px 4px;background:#313244;color:#cdd6f4;border:1px solid #45475a;border-radius:4px;" title="Case sensitivity">
                 <option value="No">Case Insensitive</option><option value="Yes">Binary</option>
               </select>
               <button class="btn btn-sm btn-primary" id="new-idx-ok-${tabId}">Add</button>
               <button class="btn btn-sm" id="new-idx-cancel-${tabId}" style="background:#313244;color:#cdd6f4;">Cancel</button>
               <span id="new-idx-msg-${tabId}" style="font-size:11px;"></span>
             </div>`
          : '';

        container.innerHTML = toolbarHtml + addIdxForm +
          `<div id="meta-tbl-${tabId}" style="flex:1;min-height:0;overflow:hidden;"></div>`;

        if (kind === 'indexes') {
          resp.data = resp.data.map(r => ({ ...r, _origTag: r.Tag }));
        }

        /* global Tabulator */
        const grid = new Tabulator('#meta-tbl-' + tabId, {
          data: resp.data,
          columns: buildMetaColumns(kind),
          layout: 'fitDataFill',
          pagination: 'local',
          paginationSize: 200,
          paginationCounter: 'rows',
          height: showSave ? 'calc(100% - 34px)' : '100%',
          placeholder: '(no data)',
        });

        if (kind === 'fields') {
          const saveBtn = document.getElementById('save-meta-' + tabId);
          const msgEl   = document.getElementById('save-meta-msg-' + tabId);
          saveBtn?.addEventListener('click', async () => {
            if (msgEl) msgEl.textContent = 'Saving…';
            try {
              const rows = grid.getData();
              const resp2 = await apiFetch('api/save_meta.php', {
                method:  'POST',
                headers: { 'Content-Type': 'application/json' },
                body:    JSON.stringify({ dd, table, rows }),
              });
              const errs = resp2.errors || [];
              if (msgEl) msgEl.textContent = errs.length
                ? `Saved ${resp2.saved}, errors: ${errs.join('; ')}`
                : `Saved ${resp2.saved} property change(s)`;
            } catch (err) {
              if (msgEl) msgEl.textContent = `Error: ${err.message}`;
            }
          });
        }

        if (kind === 'indexes') {
          const saveBtn     = document.getElementById('save-meta-' + tabId);
          const msgEl       = document.getElementById('save-meta-msg-' + tabId);
          const addIdxBtnEl = document.getElementById('add-idx-btn-'   + tabId);
          const addIdxFormEl= document.getElementById('add-idx-form-'  + tabId);
          const dirtyTags   = new Set();   // _origTag values of edited rows

          const delIdxBtnEl = document.getElementById('del-idx-btn-' + tabId);
          let selectedTag = null;
          let selectedRow = null;

          const applyHighlight = () => {
            grid.getRows().forEach(r => r.getElement().style.background = '');
            if (selectedRow) selectedRow.getElement().style.background = '#313244';
          };
          const enableSave = () => {
            if (saveBtn) { saveBtn.disabled = false; saveBtn.style.opacity = ''; saveBtn.style.cursor = ''; }
          };
          const disableSave = () => {
            dirtyTags.clear();
            if (saveBtn) { saveBtn.disabled = true; saveBtn.style.opacity = '0.5'; saveBtn.style.cursor = 'not-allowed'; }
          };
          const enableDel = (tag, row) => {
            selectedTag = tag;
            selectedRow = row;
            if (delIdxBtnEl) { delIdxBtnEl.disabled = false; delIdxBtnEl.style.opacity = ''; delIdxBtnEl.style.cursor = ''; }
          };
          const disableDel = () => {
            selectedTag = null;
            selectedRow = null;
            if (delIdxBtnEl) { delIdxBtnEl.disabled = true; delIdxBtnEl.style.opacity = '0.5'; delIdxBtnEl.style.cursor = 'not-allowed'; }
          };

          // Row click — highlight and arm delete button
          grid.on('rowClick', (e, row) => {
            enableDel(row.getData()._origTag || row.getData().Tag, row);
            applyHighlight();
          });

          // Mark row dirty; re-apply highlight since Tabulator re-renders edited rows
          grid.on('cellEdited', cell => {
            dirtyTags.add(cell.getRow().getData()._origTag);
            enableSave();
            applyHighlight();
          });

          // Delete selected index
          delIdxBtnEl?.addEventListener('click', async () => {
            if (!selectedTag) return;
            if (!confirm(`Delete index "${selectedTag}"?`)) return;
            if (msgEl) { msgEl.textContent = 'Deleting…'; msgEl.style.color = '#a6adc8'; }
            try {
              const r = await apiFetch('api/delete_index.php', {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ dd, table, tag: selectedTag }),
              });
              if (r.error) {
                if (msgEl) { msgEl.textContent = r.error; msgEl.style.color = '#f38ba8'; }
              } else {
                disableDel();
                if (msgEl) msgEl.textContent = '';
                loadMetaData(tabId, dd, table, kind);
              }
            } catch (err) {
              if (msgEl) { msgEl.textContent = err.message; msgEl.style.color = '#f38ba8'; }
            }
          });

          // Toggle Add Index form
          addIdxBtnEl?.addEventListener('click', () => {
            if (!addIdxFormEl) return;
            const shown = addIdxFormEl.style.display !== 'none';
            addIdxFormEl.style.display = shown ? 'none' : 'flex';
            if (!shown) document.getElementById('new-idx-tag-' + tabId)?.focus();
          });
          document.getElementById('new-idx-cancel-' + tabId)?.addEventListener('click', () => {
            if (addIdxFormEl) addIdxFormEl.style.display = 'none';
          });

          // Add Index submit
          document.getElementById('new-idx-ok-' + tabId)?.addEventListener('click', async () => {
            const tagEl  = document.getElementById('new-idx-tag-'  + tabId);
            const exprEl = document.getElementById('new-idx-expr-' + tabId);
            const descEl = document.getElementById('new-idx-desc-' + tabId);
            const uniqEl = document.getElementById('new-idx-uniq-' + tabId);
            const binEl  = document.getElementById('new-idx-bin-'  + tabId);
            const newMsg = document.getElementById('new-idx-msg-'  + tabId);
            const tagV   = tagEl?.value.trim();
            const exprV  = exprEl?.value.trim();
            if (!tagV || !exprV) {
              if (newMsg) { newMsg.textContent = 'Tag and Expression are required'; newMsg.style.color = '#f38ba8'; }
              return;
            }
            if (newMsg) { newMsg.textContent = 'Creating…'; newMsg.style.color = '#a6adc8'; }
            try {
              const r = await apiFetch('api/save_index.php', {
                method: 'POST', headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                  dd, table, origTag: '', tag: tagV, expression: exprV,
                  descending: descEl?.value || 'No',
                  unique: uniqEl?.value || 'No',
                  binary: binEl?.value || 'No',
                }),
              });
              if (r.error) {
                if (newMsg) { newMsg.textContent = r.error; newMsg.style.color = '#f38ba8'; }
              } else {
                if (addIdxFormEl) addIdxFormEl.style.display = 'none';
                if (tagEl)  tagEl.value  = '';
                if (exprEl) exprEl.value = '';
                if (newMsg) newMsg.textContent = '';
                loadMetaData(tabId, dd, table, kind);
              }
            } catch (err) {
              if (newMsg) { newMsg.textContent = err.message; newMsg.style.color = '#f38ba8'; }
            }
          });

          // Save only dirty rows (drop+recreate with origTag for rename support)
          saveBtn?.addEventListener('click', async () => {
            const targets = grid.getData().filter(r => dirtyTags.has(r._origTag));
            if (targets.length === 0) return;
            if (msgEl) { msgEl.textContent = 'Saving…'; msgEl.style.color = '#a6adc8'; }
            let ok = 0, errs = [];
            for (const row of targets) {
              try {
                const r = await apiFetch('api/save_index.php', {
                  method: 'POST', headers: { 'Content-Type': 'application/json' },
                  body: JSON.stringify({
                    dd, table,
                    origTag: row._origTag || row.Tag,
                    tag: row.Tag, expression: row.Expression,
                    descending: row.Descending, unique: row.Unique,
                    primary: row.Primary, binary: row.Binary,
                  }),
                });
                if (r.error) errs.push(`${row.Tag}: ${r.error}`); else ok++;
              } catch (err) { errs.push(`${row.Tag}: ${err.message}`); }
            }
            if (msgEl) {
              msgEl.style.color = errs.length ? '#f38ba8' : '#a6e3a1';
              msgEl.textContent = errs.length
                ? `${ok} saved, errors: ${errs.join('; ')}`
                : `${ok} index(es) saved`;
            }
            if (ok > 0) loadMetaData(tabId, dd, table, kind);
            else disableSave();
          });
        }
      })
      .catch(err => {
        if (container) container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
      });
  }

  function openSqlTab(dd = null, sql = '', scriptName = '', objMeta = null) {
    const targetDD = dd || state.selectedDD;
    const id = 'tab-' + (state.nextTabId++);
    const title = scriptName || (targetDD ? `SQL – ${targetDD}` : 'SQL');
    const tabData = { id, title, type: 'sql', dd: targetDD, initialSql: sql };
    if (objMeta) Object.assign(tabData, objMeta);
    else if (scriptName) tabData.scriptName = scriptName;
    state.tabs.push(tabData);
    renderTabs();
    activateTab(id);
    setTimeout(() => state.aceEditors[id]?.focus(), 120);
  }

  function renderTabs() {
    const bar = document.getElementById('tab-bar');
    const content = document.getElementById('tab-content');

    // Remove existing tab buttons
    bar.querySelectorAll('.tab').forEach(el => el.remove());

    // Remove tab panels that no longer exist
    const activeIds = new Set(state.tabs.map(t => t.id));
    content.querySelectorAll('.tab-panel').forEach(p => {
      if (!activeIds.has(p.dataset.tabId)) p.remove();
    });

    state.tabs.forEach(tab => {
      // Tab button
      const btn = document.createElement('div');
      btn.className = 'tab' + (tab.id === state.activeTabId ? ' active' : '');
      btn.dataset.tabId = tab.id;
      btn.innerHTML = `<span>${escHtml(tab.title)}</span><span class="tab-close" title="Close">×</span>`;
      btn.querySelector('span:first-child').addEventListener('click', () => activateTab(tab.id));
      btn.querySelector('.tab-close').addEventListener('click', e => { e.stopPropagation(); closeTab(tab.id); });
      bar.insertBefore(btn, bar.querySelector('.new-tab-btn'));

      // Create panel if needed
      if (!content.querySelector(`[data-tab-id="${tab.id}"]`)) {
        const panel = document.createElement('div');
        panel.className = 'tab-panel' + (tab.id === state.activeTabId ? ' active' : '');
        panel.dataset.tabId = tab.id;
        if (tab.type === 'table') {
          panel.innerHTML = `<div class="data-panel" id="data-${tab.id}" style="display:flex;flex-direction:column;flex:1;overflow:hidden;"><div class="alert alert-info" style="margin:8px;">Loading…</div></div>`;
        } else if (tab.type === 'meta') {
          panel.innerHTML = `<div class="data-panel" id="meta-${tab.id}" style="display:flex;flex-direction:column;flex:1;overflow:hidden;"><div class="alert alert-info" style="margin:8px;">Loading…</div></div>`;
        } else if (tab.type === 'table_triggers') {
          panel.innerHTML = buildTriggerPanel(tab.id, tab);
          bindTriggerPanel(tab.id, tab);
        } else if (tab.type === 'group') {
          panel.innerHTML = `<div class="data-panel" id="group-${tab.id}" style="display:flex;flex-direction:column;flex:1;overflow:hidden;"><div class="alert alert-info" style="margin:8px;">Loading…</div></div>`;
        } else if (tab.type === 'user') {
          panel.innerHTML = `<div class="data-panel" id="user-${tab.id}" style="display:flex;flex-direction:column;flex:1;overflow:hidden;"><div class="alert alert-info" style="margin:8px;">Loading…</div></div>`;
        } else if (tab.type === 'ri') {
          panel.innerHTML = `<div class="data-panel" id="ri-${tab.id}" style="display:flex;flex-direction:column;flex:1;overflow:hidden;overflow-y:auto;"><div class="alert alert-info" style="margin:8px;">Loading…</div></div>`;
        } else if (tab.type === 'sql') {
          panel.innerHTML = buildSqlPanel(tab.id, tab);
          bindSqlPanel(tab.id, tab);
        }
        content.appendChild(panel);
      }
    });
  }

  function activateTab(id) {
    state.activeTabId = id;
    document.querySelectorAll('.tab').forEach(el => {
      el.classList.toggle('active', el.dataset.tabId === id);
    });
    document.querySelectorAll('.tab-panel').forEach(el => {
      el.classList.toggle('active', el.dataset.tabId === id);
    });
  }

  function closeTab(id) {
    if (state.aceEditors[id]) {
      state.aceEditors[id].destroy();
      delete state.aceEditors[id];
    }
    if (procParamGrids[id]) {
      procParamGrids[id].destroy();
      delete procParamGrids[id];
    }
    state.tabs = state.tabs.filter(t => t.id !== id);
    delete tblState[id];
    if (state.activeTabId === id) {
      state.activeTabId = state.tabs.length ? state.tabs[state.tabs.length - 1].id : null;
    }
    renderTabs();
    if (state.activeTabId) activateTab(state.activeTabId);
  }

  document.getElementById('new-tab-btn').addEventListener('click', () => openSqlTab());

  // ── Table data loading ─────────────────────────────────────────────────────
  // ── Table data: load rows with optional index ordering, seek, and AOF filter ─
  // opts: { orderby, orderdir, seekVal, aofExpr }
  async function loadTableData(tabId, dd, table, opts = {}) {
    const container = document.getElementById('data-' + tabId);
    if (!container) return;

    const { orderby = '', orderdir = 'ASC', seekVal = '', aofExpr = '' } = opts;

    // Derive the leading field from the index expression (e.g. "leaseid" from "leaseid;enddate")
    const seekField = orderby ? orderby.split(/[;,]/)[0].trim() : '';
    const seekActive = !!(seekVal && seekField);

    // Fetch index tags, field metadata, and row data in parallel
    const tagsUrl   = `api/table_meta.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}&kind=indexes`;
    const fieldsUrl = `api/table_meta.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}&kind=fields`;
    let dataUrl = `api/table_data.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}`;
    if (orderby) dataUrl += `&orderby=${encodeURIComponent(orderby)}&orderdir=${encodeURIComponent(orderdir)}`;
    if (seekActive) dataUrl += `&seek=${encodeURIComponent(seekVal)}&seekfield=${encodeURIComponent(seekField)}`;
    if (aofExpr)  dataUrl += `&aof=${encodeURIComponent(aofExpr)}`;

    let indexTags = [], fields = [], resp;
    try {
      [indexTags, fields, resp] = await Promise.all([
        fetch(tagsUrl).then(r => r.json()).then(r => r.data || []).catch(() => []),
        fetch(fieldsUrl).then(r => r.json()).then(r => r.data || []).catch(() => []),
        fetch(dataUrl).then(r => r.json()),
      ]);
    } catch (err) {
      container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
      return;
    }

    // Set of PK field names (uppercase) — these cells are read-only for existing rows
    const pkFieldsUpper = new Set(
      fields.filter(f => f.Index === 'Primary').map(f => (f.Field || '').toUpperCase())
    );

    // Map: field name (uppercase) → BaseType — used to apply display formatters
    const fieldBaseTypeMap = {};
    fields.forEach(f => { fieldBaseTypeMap[(f.Field || '').toUpperCase()] = f.BaseType || ''; });

    // Types that are intrinsically read-only (engine-managed counters / binary blobs)
    const READ_ONLY_TYPES = new Set([
      'RowVersion', 'ModTime', 'AutoIncrement', 'Binary', 'Blob', 'Varbinary',
    ]);

    // Build a Tabulator formatter for a field based on its BaseType.
    // Returns null when no special formatting is needed.
    function makeTypeFormatter(baseType) {
      switch (baseType) {
        case 'Date':
        case 'AdtDate':
          return (cell) => {
            const v = cell.getValue();
            if (!v) return '';
            const s = String(v);
            if (/^\d{8}$/.test(s))
              return s.slice(0,4) + '-' + s.slice(4,6) + '-' + s.slice(6,8);
            return escHtml(s);
          };
        case 'DateTime':
        case 'Timestamp':
        case 'AdtTimestamp':
        case 'ModTime':
          // Engine returns "YYYYMMDDHHMMSS" for all timestamp types.
          return (cell) => {
            const v = cell.getValue();
            if (!v) return '';
            const s = String(v);
            if (/^\d{14}$/.test(s))
              return s.slice(0,4)+'-'+s.slice(4,6)+'-'+s.slice(6,8)+' '
                    +s.slice(8,10)+':'+s.slice(10,12)+':'+s.slice(12,14);
            if (/^\d{8}$/.test(s))
              return s.slice(0,4)+'-'+s.slice(4,6)+'-'+s.slice(6,8);
            return escHtml(s);
          };
        case 'Time':
          return (cell) => escHtml(String(cell.getValue() ?? ''));
        case 'RowVersion':
          return (cell) => {
            const v = cell.getValue();
            return v ? `<code style="font-size:11px">${escHtml(String(v))}</code>` : '';
          };
        case 'Money':
          return (cell) => {
            const v = cell.getValue();
            if (v === null || v === undefined || v === '') return '';
            const n = parseFloat(v);
            if (isNaN(n)) return escHtml(String(v));
            const abs = Math.abs(n);
            const fmt = abs.toLocaleString(undefined, {minimumFractionDigits: 2, maximumFractionDigits: 2});
            return (n < 0 ? '-$' : '$') + fmt;
          };
        case 'Logical':
          return (cell) => {
            const v = cell.getValue();
            if (v === true  || v === 1 || v === '1' || String(v).toUpperCase() === 'T') return '✓';
            if (v === false || v === 0 || v === '0' || String(v).toUpperCase() === 'F') return '✗';
            return '';
          };
        case 'Binary':
        case 'Blob':
        case 'Varbinary':
          return (cell) => {
            const v = cell.getValue();
            if (!v || String(v).length === 0)
              return '<span style="color:#6c7086;font-style:italic;">(empty)</span>';
            return '<span style="color:#a6adc8;font-style:italic;">BLOB</span>';
          };
        case 'Memo':
          return (cell) => {
            const v = cell.getValue();
            if (v === null || v === undefined || v === '') return '';
            const s = String(v);
            // PHP encodes invalid UTF-8 bytes as U+FFFD; control chars also signal binary.
            if (s.includes('�') || /[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/.test(s)) {
              return '<span style="color:#a6adc8;font-style:italic;">BLOB</span>';
            }
            return `<span style="white-space:pre-wrap;">${escHtml(s)}</span>`;
          };
        default:
          return null;
      }
    }

    if (resp.error) {
      container.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(resp.error)}</div>`;
      return;
    }

    // Build seek toolbar
    const idxOpts = ['<option value="">— Natural Order —</option>']
      .concat(indexTags.map(t => {
        const expr = t.Expression || t.Tag;
        const sel  = expr === orderby ? ' selected' : '';
        return `<option value="${escAttr(expr)}"${sel}>${escHtml(t.Tag)}</option>`;
      })).join('');

    const pillStyle = 'display:flex;align-items:center;gap:3px;background:#f0a500;color:#1e1e2e;'
      + 'padding:2px 6px 2px 9px;border-radius:10px;font-size:11px;font-weight:600;';
    const clearBtnStyle = 'background:none;border:none;cursor:pointer;color:#1e1e2e;font-size:14px;'
      + 'line-height:1;padding:0 0 0 2px;font-weight:bold;';

    const aofPillStyle = 'display:flex;align-items:center;gap:3px;background:#a6e3a1;color:#1e1e2e;'
      + 'padding:2px 6px 2px 9px;border-radius:10px;font-size:11px;font-weight:600;';
    const aofActive = aofExpr.trim() !== '';
    container.innerHTML = `
      <div style="flex:0 0 auto;display:flex;flex-direction:column;background:#1e1e2e;border-bottom:1px solid #313244;">
        <div id="seek-bar-${tabId}" style="display:flex;gap:6px;align-items:center;padding:4px 8px;">
          <select id="idx-sel-${tabId}" style="background:#181825;color:#cdd6f4;border:1px solid #45475a;padding:3px 6px;border-radius:4px;font-size:12px;">
            ${idxOpts}
          </select>
          <input id="seek-inp-${tabId}" type="text" placeholder="Seek to…" value="${escAttr(seekVal)}"
            style="background:#181825;color:#cdd6f4;border:1px solid #45475a;padding:3px 8px;border-radius:4px;font-size:12px;width:180px;">
          <button class="btn" id="seek-go-${tabId}" style="font-size:12px;padding:3px 10px;">Go</button>
          <span id="seek-pill-${tabId}" style="${pillStyle}${seekActive ? '' : 'display:none;'}">
            ⊙ ${escHtml(seekVal)}
            <button id="seek-clear-${tabId}" style="${clearBtnStyle}" title="Clear seek">×</button>
          </span>
          <span id="seek-msg-${tabId}" style="font-size:11px;color:#a6adc8;"></span>
        </div>
        <div id="aof-bar-${tabId}" style="display:flex;gap:6px;align-items:center;padding:3px 8px;border-top:1px solid #313244;">
          <span style="font-size:11px;color:#a6adc8;white-space:nowrap;">Filter:</span>
          <input id="aof-inp-${tabId}" type="text" placeholder="e.g. AGE &gt;= 25 AND CITY = &#39;Miami&#39;"
            value="${escAttr(aofExpr)}"
            style="background:#181825;color:#cdd6f4;border:1px solid #45475a;padding:3px 8px;border-radius:4px;font-size:12px;flex:1;min-width:200px;">
          <button class="btn" id="aof-apply-${tabId}" style="font-size:12px;padding:3px 10px;">Apply</button>
          <button class="btn" id="aof-clear-${tabId}" style="font-size:12px;padding:3px 10px;" ${aofActive ? '' : 'disabled'}>Clear</button>
          <span id="aof-pill-${tabId}" style="${aofPillStyle}${aofActive ? '' : 'display:none;'}">
            ⊙ filter active
            <button id="aof-pill-clear-${tabId}" style="${clearBtnStyle}" title="Clear filter">×</button>
          </span>
          <span id="aof-count-${tabId}" style="font-size:11px;color:#a6adc8;"></span>
        </div>
      </div>
      <div id="tabulator-${tabId}" style="flex:1;min-height:0;overflow:hidden;"></div>`;

    /* global Tabulator */
    const tbl = new Tabulator('#tabulator-' + tabId, {
      data: resp.data,
      autoColumns: true,
      autoColumnsDefinitions: function (defs) {
        return defs.map(def => {
          const fieldUpper = (def.field || '').toUpperCase();
          const isPk       = pkFieldsUpper.has(fieldUpper);
          const baseType   = fieldBaseTypeMap[fieldUpper] || '';
          const isROType   = READ_ONLY_TYPES.has(baseType);
          const fmtr       = makeTypeFormatter(baseType);
          const col = {
            ...def,
            editor: isROType ? false : 'input',
            editable: function (cell) {
              if (isROType) return false;
              const inst = tblState[tabId];
              if (!inst) return false;
              if (inst.pendingRow && cell.getRow() === inst.pendingRow) return true;
              return !isPk;
            },
            cssClass: isPk ? 'cell-pk' : '',
          };
          if (isPk)    col.headerTooltip = 'Primary key — read only';
          if (isROType) col.headerTooltip = baseType + ' — read only';
          if (fmtr !== null) {
            if (fmtr === 'textarea') {
              col.formatter      = 'textarea';
              col.variableHeight = true;
              col.width          = 300;
            } else {
              col.formatter = fmtr;
            }
          }
          return col;
        });
      },
      layout: 'fitDataFill',
      pagination: 'local',
      paginationSize: 50,
      paginationSizeSelector: [25, 50, 100, 200, 500],
      paginationCounter: 'rows',
      selectable: 1,
      movableColumns: true,
      resizableRows: false,
      placeholder: '(no rows)',
      height: '100%',
      rowClick: function (e, row) {
        const inst = tblState[tabId];
        if (!inst) return;
        inst.tbl.deselectRow();
        row.select();
        inst.rowIdx = inst.tbl.getRows().indexOf(row);
      },
    });

    tbl.on('tableBuilt', function () {
      // ── Navigation/edit button bar (in paginator) ─────────────────────
      const bar = document.createElement('div');
      bar.className = 'tbl-btn-bar';
      bar.innerHTML = `
        <button class="tbl-btn" data-act="refresh" title="Refresh">&#x27F3;</button>
        <button class="tbl-btn" data-act="top"     title="First record">&#x2912;</button>
        <button class="tbl-btn" data-act="bottom"  title="Last record">&#x2913;</button>
        <button class="tbl-btn" data-act="up"      title="Previous record">&#9650;</button>
        <button class="tbl-btn" data-act="down"    title="Next record">&#9660;</button>
        <span class="tbl-btn-sep"></span>
        <button class="tbl-btn" data-act="add"        title="Add row">&#xff0b;</button>
        <button class="tbl-btn" data-act="delete"     title="Delete row">&#x2715;</button>
        <button class="tbl-btn tbl-btn-confirm" data-act="confirm" title="Confirm insert" disabled>&#x2714;</button>
        <span class="tbl-btn-sep"></span>
        <button class="tbl-btn tbl-btn-save" data-act="save-edits" title="Save cell edits" disabled>&#x1F4BE;</button>
        <span class="tbl-btn-sep"></span>`;
      bar.addEventListener('click', e => {
        const btn = e.target.closest('[data-act]');
        if (btn) handleTblAction(btn.dataset.act, tabId);
      });
      const paginator = document.querySelector('#tabulator-' + tabId + ' .tabulator-paginator');
      if (paginator) paginator.prepend(bar);
      tblState[tabId] = {
        tbl, dd, table, rowIdx: -1, pendingRow: null,
        orderby, orderdir, seekVal, seekField, aofExpr,
        pkFields: [...pkFieldsUpper],
        dirtyRows: new Map(),
        confirmBtn:   bar.querySelector('[data-act="confirm"]'),
        saveEditsBtn: bar.querySelector('[data-act="save-edits"]'),
      };

      // Warn if table has no PK — edits cannot be saved without one
      if (pkFieldsUpper.size === 0) {
        const noPkWarn = document.createElement('span');
        noPkWarn.title = 'No primary key — row edits cannot be saved';
        noPkWarn.style.cssText = 'font-size:11px;color:#f38ba8;margin-left:4px;cursor:default;';
        noPkWarn.textContent = '⚠ No PK';
        bar.appendChild(noPkWarn);
        const saveBtn = bar.querySelector('[data-act="save-edits"]');
        if (saveBtn) saveBtn.disabled = true;
      }

      // Capture original row data when a cell editor opens (before value changes)
      tbl.on('cellEditing', (cell) => {
        const inst = tblState[tabId];
        if (!inst) return;
        const row = cell.getRow();
        if (row === inst.pendingRow) return;
        if (!inst.dirtyRows.has(row)) {
          inst.dirtyRows.set(row, { orig: { ...row.getData() } });
        }
      });

      // Enable Save button and mark cell dirty after any cell is changed
      tbl.on('cellEdited', (cell) => {
        const inst = tblState[tabId];
        if (!inst) return;
        if (cell.getRow() === inst.pendingRow) return;
        if (inst.saveEditsBtn) inst.saveEditsBtn.disabled = false;
        cell.getElement()?.classList.add('cell-dirty');
      });

      // When a seek is active the server already returned rows starting at the match;
      // row 0 is always the hit — select and highlight it.
      if (seekActive) {
        const firstRows = tbl.getRows();
        if (firstRows.length) {
          tbl.selectRow(firstRows[0]);
          tblState[tabId].rowIdx = 0;
          const msgEl = document.getElementById('seek-msg-' + tabId);
          if (msgEl) {
            const matched = String(firstRows[0].getData()[seekField] ?? '');
            if (matched) msgEl.textContent = `→ ${matched}`;
          }
        }
      }

      // Update index dropdown when column header is clicked (client-side sort)
      tbl.on('dataSorted', function (sorters) {
        if (!sorters || !sorters.length) return;
        const sortedField = sorters[0].field;
        const match = indexTags.find(t =>
          (t.Expression || '').toLowerCase() === sortedField.toLowerCase() ||
          (t.Tag || '').toLowerCase() === sortedField.toLowerCase()
        );
        const sel = document.getElementById('idx-sel-' + tabId);
        if (sel && match) sel.value = match.Expression || '';
      });
    });

    // ── Seek button handler ──────────────────────────────────────────────
    document.getElementById('seek-go-' + tabId)?.addEventListener('click', () => {
      const selectedExpr = document.getElementById('idx-sel-' + tabId)?.value || '';
      const val          = (document.getElementById('seek-inp-' + tabId)?.value || '').trim();
      if (val && !selectedExpr) {
        const msgEl = document.getElementById('seek-msg-' + tabId);
        if (msgEl) msgEl.textContent = 'Select an index to seek';
        return;
      }
      const curAof = tblState[tabId]?.aofExpr || '';
      loadTableData(tabId, dd, table, { orderby: selectedExpr, orderdir, seekVal: val, aofExpr: curAof });
    });

    // Clear-seek button: reload without seek, preserve ordering and AOF
    document.getElementById('seek-clear-' + tabId)?.addEventListener('click', () => {
      const curAof = tblState[tabId]?.aofExpr || '';
      loadTableData(tabId, dd, table, { orderby, orderdir, seekVal: '', aofExpr: curAof });
    });

    // Enter in seek input triggers Go
    document.getElementById('seek-inp-' + tabId)?.addEventListener('keydown', e => {
      if (e.key === 'Enter') document.getElementById('seek-go-' + tabId)?.click();
    });

    // ── AOF filter handlers ───────────────────────────────────────────────
    const applyAof = () => {
      const expr = (document.getElementById('aof-inp-' + tabId)?.value || '').trim();
      loadTableData(tabId, dd, table, { orderby, orderdir, seekVal, aofExpr: expr });
    };

    document.getElementById('aof-apply-' + tabId)?.addEventListener('click', applyAof);

    document.getElementById('aof-inp-' + tabId)?.addEventListener('keydown', e => {
      if (e.key === 'Enter') applyAof();
    });

    const clearAof = () => {
      const inp = document.getElementById('aof-inp-' + tabId);
      if (inp) inp.value = '';
      loadTableData(tabId, dd, table, { orderby, orderdir, seekVal, aofExpr: '' });
    };

    document.getElementById('aof-clear-' + tabId)?.addEventListener('click', clearAof);
    document.getElementById('aof-pill-clear-' + tabId)?.addEventListener('click', clearAof);

    // Show row count when AOF is active
    if (aofActive) {
      const countEl = document.getElementById('aof-count-' + tabId);
      if (countEl) countEl.textContent = `${resp.data.length} row(s)`;
    }
  }

  // Navigate to a row by its 0-based index in the full dataset.
  // Tabulator's scrollToRow handles page navigation automatically.
  function navigateToRow(inst, idx) {
    const rows = inst.tbl.getRows();
    if (!rows.length) return;
    idx = Math.max(0, Math.min(idx, rows.length - 1));
    inst.rowIdx = idx;
    const row = rows[idx];
    inst.tbl.deselectRow();
    inst.tbl.selectRow(row);
    inst.tbl.scrollToRow(row, 'top', false);
  }

  function handleTblAction(act, tabId) {
    const inst = tblState[tabId];
    if (!inst) return;
    const { tbl, dd, table } = inst;

    switch (act) {
      case 'save-edits': {
        if (!inst.dirtyRows || inst.dirtyRows.size === 0) return;
        if (inst.pkFields.length === 0) {
          setStatus('Cannot save: no primary key defined for this table', 'error');
          return;
        }
        const jobs = [];
        const rowEntries = [...inst.dirtyRows.entries()];
        for (const [row, { orig }] of rowEntries) {
          jobs.push(
            apiFetch('api/row_ops.php', {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({
                action: 'update', dd: inst.dd, table: inst.table,
                orig, row: row.getData(), pkFields: inst.pkFields,
              }),
            })
            .then(() => ({ row, ok: true }))
            .catch(e => ({ row, error: e.message }))
          );
        }
        Promise.all(jobs).then(results => {
          const errs = results.filter(r => r.error);
          const saved = results.filter(r => r.ok);
          // Clear dirty highlighting and tracking for successful rows
          saved.forEach(({ row }) => {
            row.getCells().forEach(c => c.getElement()?.classList.remove('cell-dirty'));
            inst.dirtyRows.delete(row);
          });
          if (inst.dirtyRows.size === 0 && inst.saveEditsBtn)
            inst.saveEditsBtn.disabled = true;
          setStatus(errs.length
            ? `${errs.length} row(s) failed: ${errs.map(r => r.error).join('; ')}`
            : `Saved ${saved.length} row(s)`);
        });
        break;
      }
      case 'refresh': {
        const ob  = inst.orderby  || '';
        const od  = inst.orderdir || 'ASC';
        const sv  = inst.seekVal  || '';
        const sf  = inst.seekField || '';
        const aof = inst.aofExpr  || '';
        let url = `api/table_data.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}`;
        if (ob) url += `&orderby=${encodeURIComponent(ob)}&orderdir=${encodeURIComponent(od)}`;
        if (sv && sf) url += `&seek=${encodeURIComponent(sv)}&seekfield=${encodeURIComponent(sf)}`;
        if (aof) url += `&aof=${encodeURIComponent(aof)}`;
        fetch(url).then(r => r.json())
          .then(resp => { if (!resp.error) { tbl.replaceData(resp.data); inst.rowIdx = -1; } })
          .catch(() => {});
        break;
      }
      case 'top':
        navigateToRow(inst, 0);
        break;
      case 'bottom':
        navigateToRow(inst, tbl.getRows().length - 1);
        break;
      case 'up':
        navigateToRow(inst, inst.rowIdx <= 0 ? 0 : inst.rowIdx - 1);
        break;
      case 'down': {
        const total = tbl.getRows().length;
        navigateToRow(inst, inst.rowIdx < 0 ? 0 : Math.min(inst.rowIdx + 1, total - 1));
        break;
      }

      case 'add': {
        if (inst.pendingRow) return; // already an unsaved row
        tbl.addRow({}, false).then(row => {
          inst.pendingRow = row;
          if (inst.confirmBtn) inst.confirmBtn.disabled = false;
          tbl.scrollToRow(row, 'bottom', false);
          setTimeout(() => {
            const cells = row.getCells();
            if (cells.length) cells[0].edit();
          }, 60);
        });
        break;
      }

      case 'delete': {
        if (inst.rowIdx < 0) { setStatus('Select a row to delete'); return; }
        pendingDeleteTabId = tabId;
        openModal('modal-del-confirm');
        break;
      }

      case 'confirm': {
        if (!inst.pendingRow) return;
        const rowData = inst.pendingRow.getData();
        apiFetch('api/row_ops.php', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: 'insert', dd, table, row: rowData }),
        }).then(() => {
          tbl.deleteRow(inst.pendingRow);
          inst.pendingRow = null;
          if (inst.confirmBtn) inst.confirmBtn.disabled = true;
          // Refresh from server to get auto-assigned field values
          return fetch(`api/table_data.php?dd=${encodeURIComponent(dd)}&table=${encodeURIComponent(table)}`)
            .then(r => r.json())
            .then(r2 => { if (!r2.error) tbl.replaceData(r2.data); });
        }).then(() => setStatus('Row inserted'))
          .catch(err => setStatus(`Insert failed: ${err.message}`));
        break;
      }
    }
  }

  // ── SQL panel ──────────────────────────────────────────────────────────────
  function initSqlSplit(tabId) {
    if (typeof Split === 'undefined') return;

    const editorPane = document.getElementById('sql-editor-pane-' + tabId);
    const resultsPane = document.getElementById('sql-results-' + tabId);
    if (!editorPane || !resultsPane || editorPane.dataset.splitReady === 'true') return;

    Split([editorPane, resultsPane], {
      direction: 'vertical',
      sizes: [36, 64],
      minSize: [120, 120],
      gutterSize: 5,
      cursor: 'row-resize',
      onDrag: () => Object.values(state.aceEditors).forEach(ed => ed.resize()),
    });
    editorPane.dataset.splitReady = 'true';
  }

  function buildSqlPanel(tabId, tabOrDd) {
    const tab = (typeof tabOrDd === 'object' && tabOrDd !== null) ? tabOrDd : null;
    const dd  = tab ? tab.dd : tabOrDd;
    if (tab?.objType) return buildProcPanel(tabId, tab);

    const ddOptions = Array.from(state.openConnections).map(n =>
      `<option value="${escAttr(n)}" ${n === dd ? 'selected' : ''}>${escHtml(n)}</option>`
    ).join('');
    const savedScriptName = tab ? savedScriptNameForTab(tab) : '';
    const saveScriptAttr = savedScriptName ? ` data-script-name="${escAttr(savedScriptName)}"` : '';
    return `
      <div class="sql-panel">
        <div class="sql-editor-pane" id="sql-editor-pane-${tabId}">
          <div class="sql-editor-wrap">
            <div id="sql-ace-${tabId}" class="sql-ace-editor"></div>
          </div>
        <div class="sql-toolbar">
          <select id="sql-dd-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:3px 8px;border-radius:4px;font-size:12px;">
            <option value="">— database —</option>
            ${ddOptions}
          </select>
          <button class="btn" id="sql-open-btn-${tabId}" title="Open saved script">&#128194; Open</button>
          <button class="btn" id="sql-save-btn-${tabId}"${saveScriptAttr} title="Save script">&#128190; Save</button>
          <button class="btn btn-primary" id="sql-run-${tabId}" title="Execute (F5 / F9)">&#9654; Execute</button>
          <button class="btn" id="sql-clear-${tabId}" title="Clear editor">Clear</button>
          <span style="font-size:11px;color:#45475a;margin-left:10px;white-space:nowrap;">
            F5 / F9 &mdash; run all &nbsp;&nbsp; Ctrl+Enter &mdash; run selection
          </span>
          <span id="sql-msg-${tabId}" style="font-size:11px;color:#a6adc8;margin-left:8px;"></span>
        </div>
        </div>
        <div class="sql-results" id="sql-results-${tabId}"></div>
      </div>`;
  }

  function buildProcPanel(tabId, tab) {
    const dd      = tab.dd || '';
    const isFunc  = tab.objType === 'function';
    const ddOpts  = Array.from(state.openConnections).map(n =>
      `<option value="${escAttr(n)}" ${n === dd ? 'selected' : ''}>${escHtml(n)}</option>`
    ).join('');
    return `
      <div class="sql-panel proc-panel">
        <div class="proc-editor-pane" id="sql-editor-pane-${tabId}">
          <div class="sql-editor-wrap">
            <div id="sql-ace-${tabId}" class="sql-ace-editor"></div>
          </div>
          <div class="sql-toolbar">
            <select id="sql-dd-${tabId}" style="background:#1e1e2e;color:#cdd6f4;border:1px solid #45475a;padding:3px 8px;border-radius:4px;font-size:12px;">
              <option value="">— database —</option>
              ${ddOpts}
            </select>
            <button class="btn btn-primary" id="proc-save-${tabId}" title="Save body and parameters to the Data Dictionary">&#128190; Save to DD</button>
            <button class="btn" id="sql-run-${tabId}" title="Execute body (F5)">&#9654; Execute</button>
            <span id="sql-msg-${tabId}" style="font-size:11px;color:#a6adc8;margin-left:10px;"></span>
          </div>
        </div>
        <div class="proc-params-pane" id="proc-params-pane-${tabId}">
          <div class="proc-params-header">
            <span class="proc-params-title">Parameters${isFunc ? ' &amp; Return' : ''}</span>
            <button class="btn btn-sm" id="proc-add-${tabId}" title="Add parameter row">+ Add</button>
            <button class="btn btn-sm" id="proc-del-${tabId}" title="Delete selected row">&#8722; Delete</button>
          </div>
          <div id="proc-grid-${tabId}" class="proc-params-grid"></div>
        </div>
      </div>`;
  }

  // Shared execute logic — accepts the exact SQL string to send
  async function doExecuteSql(tabId, sql) {
    const dd      = (document.getElementById('sql-dd-' + tabId)?.value ?? '').trim();
    const msgEl   = document.getElementById('sql-msg-' + tabId);
    const runBtn  = document.getElementById('sql-run-' + tabId);
    const results = document.getElementById('sql-results-' + tabId); // null for proc panels
    if (!msgEl) return;

    if (!sql)  { msgEl.textContent = 'Nothing to execute'; return; }
    if (!dd)   { msgEl.textContent = 'Select a database first'; return; }

    if (runBtn) runBtn.disabled = true;
    msgEl.textContent = 'Running…';
    if (results) results.innerHTML = '';

    try {
      const resp = await apiFetch('api/execute_sql.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ dd, sql }),
      });

      if (resp.columns !== undefined) {
        msgEl.textContent = `${resp.data.length} row(s)`;
        if (results) {
          results.innerHTML = `<div id="sql-tab-${tabId}" style="height:100%;"></div>`;

          // Detect columns whose values contain newlines — use textarea formatter.
          const multilineCols = new Set();
          resp.data.forEach(row => {
            resp.columns.forEach(col => {
              if (typeof row[col.field] === 'string' && row[col.field].includes('\n'))
                multilineCols.add(col.field);
            });
          });
          const columns = resp.columns.map(col =>
            multilineCols.has(col.field)
              ? { ...col, formatter: 'textarea', width: 480, variableHeight: true }
              : col
          );

          const sqlTable = new Tabulator('#sql-tab-' + tabId, {  /* global Tabulator */
            data: resp.data,
            columns,
            layout: 'fitDataFill',
            height: '100%',
            pagination: 'local',
            paginationSize: 50,
            paginationSizeSelector: [25, 50, 100, 200],
            movableColumns: true,
            placeholder: '(no rows)',
          });

          // ── Export buttons — injected into the paginator bar on the left ────
          const blobDownload = (content, filename, mime) => {
            const url = URL.createObjectURL(new Blob([content], { type: mime }));
            Object.assign(document.createElement('a'), { href: url, download: filename }).click();
            URL.revokeObjectURL(url);
          };

          sqlTable.on('tableBuilt', () => {
            const paginator = document.querySelector(`#sql-tab-${tabId} .tabulator-paginator`);
            if (!paginator) return;

            // Guard: only add once
            if (paginator.querySelector('.sql-export-bar')) return;

            const bar = document.createElement('div');
            bar.className = 'sql-export-bar';
            bar.innerHTML =
              `<span class="sql-export-label">Export:</span>
               <button class="btn btn-sm" data-fmt="csv"  >CSV</button>
               <button class="btn btn-sm" data-fmt="html" >HTML</button>
               <button class="btn btn-sm" data-fmt="json" >JSON</button>
               <button class="btn btn-sm" data-fmt="xml"  >XML</button>
               <button class="btn btn-sm" data-fmt="excel">Excel</button>`;

            bar.addEventListener('click', e => {
              const fmt = e.target.closest('[data-fmt]')?.dataset.fmt;
              if (!fmt) return;
              const stem = 'results';
              if (fmt === 'csv')  { sqlTable.download('csv',  stem + '.csv');  return; }
              if (fmt === 'html') { sqlTable.download('html', stem + '.html'); return; }
              if (fmt === 'json') { sqlTable.download('json', stem + '.json'); return; }

              const fields = sqlTable.getColumnDefinitions()
                .filter(c => c.field)
                .map(c => ({ field: c.field, title: c.title || c.field }));
              const rows = sqlTable.getData();

              if (fmt === 'xml') {
                const esc = s => String(s ?? '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
                let xml = '<?xml version="1.0" encoding="UTF-8"?>\n<results>\n';
                rows.forEach(row => {
                  xml += '  <row>\n';
                  fields.forEach(f => { xml += `    <${f.field}>${esc(row[f.field])}</${f.field}>\n`; });
                  xml += '  </row>\n';
                });
                xml += '</results>';
                blobDownload(xml, stem + '.xml', 'application/xml');
                return;
              }

              if (fmt === 'excel') {
                const esc = s => String(s ?? '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
                let xl = `<?xml version="1.0"?><?mso-application progid="Excel.Sheet"?>
<Workbook xmlns="urn:schemas-microsoft-com:office:spreadsheet"
 xmlns:ss="urn:schemas-microsoft-com:office:spreadsheet">
 <Worksheet ss:Name="Results">
  <Table>
   <Row>${fields.map(f => `<Cell><Data ss:Type="String">${esc(f.title)}</Data></Cell>`).join('')}</Row>\n`;
                rows.forEach(row => {
                  xl += `   <Row>${fields.map(f => {
                    const v = row[f.field] ?? '';
                    const isNum = v !== '' && !isNaN(Number(v));
                    return `<Cell><Data ss:Type="${isNum ? 'Number' : 'String'}">${esc(v)}</Data></Cell>`;
                  }).join('')}</Row>\n`;
                });
                xl += `  </Table>\n </Worksheet>\n</Workbook>`;
                blobDownload(xl, stem + '.xls', 'application/vnd.ms-excel');
                return;
              }
            });

            paginator.prepend(bar);
          });
        }
      } else {
        msgEl.textContent = resp.message ? '✓ ' + resp.message : '';
        if (results) results.innerHTML = `<div class="alert alert-success" style="margin:8px;">${escHtml(resp.message)}</div>`;
      }
    } catch (err) {
      msgEl.textContent = '⚠ ' + err.message;
      if (results) results.innerHTML = `<div class="alert alert-error" style="margin:8px;">${escHtml(err.message)}</div>`;
    } finally {
      if (runBtn) runBtn.disabled = false;
    }
  }

  // Returns selected text in the Ace editor, or all text if nothing is selected
  function getActiveOrAllSql(tabId) {
    const ed = state.aceEditors[tabId];
    if (!ed) return '';
    const sel = ed.getSelectedText().trim();
    return sel || ed.getValue().trim();
  }

  async function saveSqlScript(tabId, name) {
    const sql = state.aceEditors[tabId]?.getValue() ?? '';
    await apiFetch('api/sql_scripts.php', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ action: 'save', name, sql }),
    });
    const tabMeta = state.tabs.find(t => t.id === tabId);
    if (tabMeta) {
      tabMeta.scriptName = name;
      tabMeta.title = name;
    }
    setStatus(`Script '${name}' saved`);
  }

  function savedScriptNameForTab(tabMeta) {
    if (!tabMeta) return '';
    if (tabMeta.scriptName) return tabMeta.scriptName;
    const title = (tabMeta.title || '').trim();
    if (!title || title === 'SQL' || /^SQL\s*[-\u2013\u2014]/.test(title)) return '';
    return title;
  }

  function bindSqlPanel(tabId, tab) {
    if (tab?.objType) { bindProcPanel(tabId, tab); return; }
    setTimeout(() => {
      const runBtn   = document.getElementById('sql-run-' + tabId);
      const clearBtn = document.getElementById('sql-clear-' + tabId);
      const openBtn  = document.getElementById('sql-open-btn-' + tabId);
      const saveBtn  = document.getElementById('sql-save-btn-' + tabId);
      const results  = document.getElementById('sql-results-' + tabId);
      const msgEl    = document.getElementById('sql-msg-' + tabId);
      if (!runBtn) return;
      initSqlSplit(tabId);

      // ── Ace editor init ──────────────────────────────────────────────────────
      ace.require('ace/ext/language_tools');
      const editor = ace.edit('sql-ace-' + tabId);
      setAdsMode(editor);
      editor.setOptions({
        enableBasicAutocompletion: true,
        enableSnippets: false,
        enableLiveAutocompletion: false,
        showPrintMargin: false,
        useWorker: false,
        fontSize: '13px',
        fontFamily: '"Cascadia Code", "Fira Code", Consolas, monospace',
        tabSize: 2,
        useSoftTabs: true,
        scrollPastEnd: 0.5,
        placeholder: '-- Type SQL here\n-- SELECT * FROM MyTable\n-- Select text then press Ctrl+Enter to run just that portion',
      });
      if (tab.initialSql) editor.setValue(tab.initialSql, -1);
      state.aceEditors[tabId] = editor;

      // ── Auto-capitalize SQL keywords on delimiter insertion ───────────────────
      let _capitalizing = false;
      editor.session.on('change', delta => {
        if (_capitalizing || delta.action !== 'insert') return;
        const ch = delta.lines[0];
        const isNewline = delta.lines.length > 1;
        if (!isNewline && !/^[ \t,;()\[\]]$/.test(ch)) return;

        const row = delta.start.row;
        const col = delta.start.column;
        if (col === 0 && !isNewline) return;

        // Skip inside string literals and comments
        const tok = editor.session.getTokenAt(row, col);
        if (tok && /string|comment/.test(tok.type)) return;

        const wordRange = editor.session.getWordRange(row, col > 0 ? col - 1 : 0);
        const word = editor.session.getTextRange(wordRange);
        if (!word || !/^[a-zA-Z_]\w*$/.test(word)) return;

        const upper = word.toUpperCase();
        if (upper === word || !SQL_KEYWORDS.has(upper)) return;

        _capitalizing = true;
        editor.session.replace(wordRange, upper);
        _capitalizing = false;
      });

      // F5 / F9 — execute all
      editor.commands.addCommand({
        name: 'executeAll',
        bindKey: { win: 'F5', mac: 'F5' },
        exec: () => doExecuteSql(tabId, editor.getValue().trim()),
      });
      editor.commands.addCommand({
        name: 'executeAll2',
        bindKey: { win: 'F9', mac: 'F9' },
        exec: () => doExecuteSql(tabId, editor.getValue().trim()),
      });
      // Ctrl+Enter — run selection or all
      editor.commands.addCommand({
        name: 'executeSelection',
        bindKey: { win: 'Ctrl-Enter', mac: 'Cmd-Enter' },
        exec: () => doExecuteSql(tabId, getActiveOrAllSql(tabId)),
      });

      openBtn?.addEventListener('click', () => openSqlOpenModal());

      saveBtn?.addEventListener('click', async () => {
        const errEl = document.getElementById('sql-save-err');
        if (errEl) errEl.textContent = '';
        const tabMeta = state.tabs.find(t => t.id === tabId);
        const scriptName = saveBtn?.dataset.scriptName || savedScriptNameForTab(tabMeta);
        if (scriptName) {
          try {
            await saveSqlScript(tabId, scriptName);
            if (saveBtn) saveBtn.dataset.scriptName = scriptName;
          } catch (err) {
            setStatus(`Save failed: ${err.message}`);
          }
          return;
        }
        const pre = (tabMeta && !tabMeta.title.startsWith('SQL')) ? tabMeta.title : '';
        const nameInput = document.getElementById('sql-save-name');
        if (nameInput) nameInput.value = pre;
        pendingSqlSaveTabId = tabId;
        openModal('modal-sql-save');
        setTimeout(() => nameInput?.focus(), 50);
      });

      runBtn.addEventListener('click', () => {
        doExecuteSql(tabId, editor.getValue().trim());
      });

      clearBtn?.addEventListener('click', () => {
        editor.setValue('', -1);
        if (results) results.innerHTML = '';
        if (msgEl)   msgEl.textContent = '';
        editor.focus();
      });
    }, 50);
  }

  // ── Proc / Function panel ─────────────────────────────────────────────────
  function bindProcPanel(tabId, tab) {
    setTimeout(() => {
      const isFunc  = tab.objType === 'function';
      const msgEl   = document.getElementById('sql-msg-' + tabId);
      const runBtn  = document.getElementById('sql-run-' + tabId);
      const saveBtn = document.getElementById('proc-save-' + tabId);
      const addBtn  = document.getElementById('proc-add-' + tabId);
      const delBtn  = document.getElementById('proc-del-' + tabId);

      // ── Split: editor top / params bottom ──────────────────────────────────
      if (typeof Split !== 'undefined') {
        const edPane = document.getElementById('sql-editor-pane-' + tabId);
        const prPane = document.getElementById('proc-params-pane-' + tabId);
        if (edPane && prPane && !edPane.dataset.splitReady) {
          Split([edPane, prPane], {
            direction: 'vertical', sizes: [60, 40],
            minSize: [120, 120], gutterSize: 5, cursor: 'row-resize',
            onDrag: () => state.aceEditors[tabId]?.resize(),
          });
          edPane.dataset.splitReady = 'true';
        }
      }

      // ── Ace editor ─────────────────────────────────────────────────────────
      ace.require('ace/ext/language_tools');
      const editor = ace.edit('sql-ace-' + tabId);
      setAdsMode(editor);
      editor.setOptions({
        enableBasicAutocompletion: true,
        enableSnippets: false,
        enableLiveAutocompletion: false,
        showPrintMargin: false,
        useWorker: false,
        fontSize: '13px',
        fontFamily: '"Cascadia Code", "Fira Code", Consolas, monospace',
        tabSize: 2,
        useSoftTabs: true,
        scrollPastEnd: 0.5,
      });
      if (tab.initialSql) editor.setValue(tab.initialSql, -1);
      state.aceEditors[tabId] = editor;

      // F5 / F9 execute
      editor.commands.addCommand({
        name: 'executeAll', bindKey: { win: 'F5', mac: 'F5' },
        exec: () => doExecuteSql(tabId, editor.getValue().trim()),
      });
      editor.commands.addCommand({
        name: 'executeAll2', bindKey: { win: 'F9', mac: 'F9' },
        exec: () => doExecuteSql(tabId, editor.getValue().trim()),
      });
      editor.commands.addCommand({
        name: 'executeSelection', bindKey: { win: 'Ctrl-Enter', mac: 'Cmd-Enter' },
        exec: () => doExecuteSql(tabId, getActiveOrAllSql(tabId)),
      });
      runBtn?.addEventListener('click', () => doExecuteSql(tabId, editor.getValue().trim()));

      // ── Parameter Tabulator grid ────────────────────────────────────────────
      const typeValues = isFunc
        ? { Input: 'Input', Return: 'Return' }
        : { Input: 'Input', Output: 'Output' };
      const dtValues   = Object.fromEntries(ADS_TYPES.map(t => [t, t]));

      // Build initial rows: input params first, then output params (for procs)
      const initRows = (tab.inputParams || []).map((p, i) => ({
        _id: 'i' + i,
        paramType: 'Input',
        name:      p.name,
        baseType:  p.baseType,
        size:      p.size || 0,
        decimals:  p.decimals || 0,
      }));
      if (!isFunc) {
        (tab.outputParams || []).forEach((p, i) => {
          initRows.push({
            _id: 'o' + i,
            paramType: 'Output',
            name:      p.name,
            baseType:  p.baseType,
            size:      p.size || 0,
            decimals:  p.decimals || 0,
          });
        });
      }
      if (isFunc && tab.returnType) {
        const { base, size, decimals } = parseDatatype(tab.returnType);
        initRows.push({ _id: 'ret', paramType: 'Return', name: '', baseType: base, size, decimals });
      }

      const grid = new Tabulator('#proc-grid-' + tabId, {
        data:          initRows,
        layout:        'fitColumns',
        selectable:    1,
        movableRows:   true,
        placeholder:   '(no parameters)',
        columns: [
          { title: 'Name',     field: 'name',      editor: 'input',  widthGrow: 3,
            headerSort: false },
          { title: 'Type',     field: 'paramType', editor: 'list', width: 90,
            editorParams: { values: typeValues }, headerSort: false },
          { title: 'DataType', field: 'baseType',  editor: 'list', widthGrow: 2,
            editorParams: { values: dtValues, autocomplete: true, clearable: true },
            headerSort: false },
          { title: 'Size',     field: 'size',      editor: 'number', width: 70,
            editorParams: { min: 0 }, headerSort: false,
            formatter: v => (v.getValue() > 0 ? v.getValue() : '') },
          { title: 'Decimals', field: 'decimals',  editor: 'number', width: 80,
            editorParams: { min: 0 }, headerSort: false,
            formatter: v => (v.getValue() > 0 ? v.getValue() : '') },
        ],
      });
      procParamGrids[tabId] = grid;

      // Add row
      addBtn?.addEventListener('click', () => {
        grid.addRow({
          _id: 'p' + Date.now(),
          paramType: 'Input',
          name: '', baseType: 'INTEGER', size: 0, decimals: 0,
        }, false);
      });

      // Delete selected row
      delBtn?.addEventListener('click', () => {
        const sel = grid.getSelectedRows();
        sel.forEach(r => r.delete());
      });

      // ── Save to DD ─────────────────────────────────────────────────────────
      saveBtn?.addEventListener('click', async () => {
        if (msgEl) msgEl.textContent = 'Saving…';
        const dd = (document.getElementById('sql-dd-' + tabId)?.value || tab.dd || '').trim();
        if (!dd) { if (msgEl) msgEl.textContent = 'Select a database first'; return; }

        const body = editor.getValue();
        const rows = grid.getData();
        const inputParamsStr  = serializeParams(rows, tab.objType, 'Input');
        const outputParamsStr = !isFunc ? serializeParams(rows, tab.objType, 'Output') : '';
        const retRow = isFunc ? rows.find(r => r.paramType === 'Return') : null;
        const returnType = retRow
          ? serializeDatatype(retRow.baseType, retRow.size || 0, retRow.decimals || 0)
          : '';

        try {
          await apiFetch('api/save_proc.php', {
            method:  'POST',
            headers: { 'Content-Type': 'application/json' },
            body:    JSON.stringify({
              dd,
              type:          tab.objType,
              name:          tab.objName,
              body,
              input_params:  inputParamsStr,
              output_params: outputParamsStr,
              return_type:   returnType,
            }),
          });
          if (msgEl) { msgEl.textContent = 'Saved ✓'; setTimeout(() => { if (msgEl) msgEl.textContent = ''; }, 3000); }
          // Update cached tab state so re-opens reflect changes
          tab.inputParams  = parseParamStr(inputParamsStr,  tab.objType, 'Input');
          tab.outputParams = parseParamStr(outputParamsStr, tab.objType, 'Output');
          tab.returnType   = returnType;
        } catch (err) {
          if (msgEl) msgEl.textContent = 'Save failed: ' + err.message;
        }
      });
    }, 50);
  }

  // ── Helpers: open/close modals ────────────────────────────────────────────
  function openModal(id)  { document.getElementById(id)?.classList.add('open'); }
  function closeModal(id) { document.getElementById(id)?.classList.remove('open'); }
  function clearModalErr(id) { const el = document.getElementById(id); if (el) el.textContent = ''; }

  // ── Modal: New DD (create) ─────────────────────────────────────────────────
  function openCreateDDModal() {
    clearModalErr('cdd-err');
    resetToggleGroup('cdd-conn-type');
    openModal('modal-create-dd');
    setTimeout(() => document.getElementById('cdd-name')?.focus(), 50);
  }

  document.getElementById('cdd-cancel').addEventListener('click', () => closeModal('modal-create-dd'));

  document.getElementById('cdd-save').addEventListener('click', async () => {
    const name     = document.getElementById('cdd-name').value.trim();
    const path     = document.getElementById('cdd-path').value.trim();
    const password = document.getElementById('cdd-password').value;
    const connType = toggleGroupValue('cdd-conn-type');
    const errEl    = document.getElementById('cdd-err');
    errEl.textContent = '';

    if (!name || !path) { errEl.textContent = 'Name and path are required'; return; }

    try {
      await apiFetch('api/create_dd.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, path, password, connType }),
      });
      closeModal('modal-create-dd');
      document.getElementById('cdd-name').value = '';
      document.getElementById('cdd-path').value = '';
      document.getElementById('cdd-password').value = '';
      refreshTree();
      setStatus(`Created dictionary '${name}'`);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  // ── Modal: Open DD (add existing) ─────────────────────────────────────────
  function openOpenDDModal() {
    clearModalErr('odd-err');
    resetToggleGroup('odd-conn-type');
    openModal('modal-open-dd');
    setTimeout(() => document.getElementById('odd-name')?.focus(), 50);
  }

  document.getElementById('odd-cancel').addEventListener('click', () => closeModal('modal-open-dd'));

  document.getElementById('odd-save').addEventListener('click', async () => {
    const name     = document.getElementById('odd-name').value.trim();
    const path     = document.getElementById('odd-path').value.trim();
    const username = document.getElementById('odd-user').value.trim();
    const connType = toggleGroupValue('odd-conn-type');
    const errEl    = document.getElementById('odd-err');
    errEl.textContent = '';

    if (!name || !path) { errEl.textContent = 'Name and path are required'; return; }

    try {
      await apiFetch('api/dictionaries.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'add', name, path, username, connType, entryType: 'dd' }),
      });
      closeModal('modal-open-dd');
      document.getElementById('odd-name').value = '';
      document.getElementById('odd-path').value = '';
      document.getElementById('odd-user').value = '';
      refreshTree();
      setStatus(`Added dictionary '${name}'`);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  // ── Modal: Free Tables directory ───────────────────────────────────────────
  function openFreeTablesModal() {
    clearModalErr('ft-err');
    resetToggleGroup('ft-conn-type');
    openModal('modal-free-tables');
    setTimeout(() => document.getElementById('ft-name')?.focus(), 50);
  }

  document.getElementById('ft-cancel').addEventListener('click', () => closeModal('modal-free-tables'));

  document.getElementById('ft-save').addEventListener('click', async () => {
    const name     = document.getElementById('ft-name').value.trim();
    const path     = document.getElementById('ft-path').value.trim();
    const connType = toggleGroupValue('ft-conn-type');
    const errEl    = document.getElementById('ft-err');
    errEl.textContent = '';

    if (!name || !path) { errEl.textContent = 'Name and path are required'; return; }

    try {
      await apiFetch('api/dictionaries.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'add', name, path, username: '', connType, entryType: 'free' }),
      });
      closeModal('modal-free-tables');
      document.getElementById('ft-name').value = '';
      document.getElementById('ft-path').value = '';
      refreshTree();
      setStatus(`Added free tables directory '${name}'`);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  // ── Connect modal ──────────────────────────────────────────────────────────
  async function openConnectModal(ddName) {
    const overlay = document.getElementById('modal-connect');
    const errEl   = document.getElementById('connect-err');
    errEl.textContent = '';

    let d;
    try {
      const dicts = await apiFetch('api/dictionaries.php');
      d = dicts.find(x => x.name === ddName);
      if (!d) { setStatus('Dictionary not found in config'); return; }
    } catch (err) {
      setStatus(err.message); return;
    }

    // Free-tables directories need no credentials — connect immediately
    if ((d.entryType ?? 'dd') === 'free') {
      try {
        await apiFetch('api/connect.php', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: 'connect', name: ddName, path: d.path, username: '', password: '' }),
        });
        state.openConnections.add(ddName);
        refreshTree();
        setStatus(`Connected to '${ddName}'`);
        updateConnectionCount();
      } catch (err) {
        setStatus(err.message);
      }
      return;
    }

    // Data dictionary — show credentials modal
    document.getElementById('connect-dd-name').textContent = ddName;
    overlay.dataset.dd   = ddName;
    overlay.dataset.path = d.path;
    document.getElementById('connect-username').value = d.username || '';
    document.getElementById('connect-password').value = '';
    overlay.classList.add('open');
    setTimeout(() => document.getElementById('connect-password').focus(), 50);
  }

  document.getElementById('connect-cancel').addEventListener('click', () => {
    document.getElementById('modal-connect').classList.remove('open');
  });

  document.getElementById('connect-submit').addEventListener('click', async () => {
    const overlay  = document.getElementById('modal-connect');
    const ddName   = overlay.dataset.dd;
    const path     = overlay.dataset.path;
    const username = document.getElementById('connect-username').value.trim();
    const password = document.getElementById('connect-password').value;
    const errEl    = document.getElementById('connect-err');
    errEl.textContent = '';

    try {
      await apiFetch('api/connect.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'connect', name: ddName, path, username, password }),
      });
      state.openConnections.add(ddName);
      overlay.classList.remove('open');
      refreshTree();
      setStatus(`Connected to '${ddName}'`);
      updateConnectionCount();
    } catch (err) {
      if (err.data?.code === 5174) {
        overlay.classList.remove('open');
        openImportSapDDModal();
        document.getElementById('isdd-source').value = path;
        document.getElementById('isdd-name').value = ddName;
        return;
      }
      errEl.textContent = err.message;
    }
  });

  // Enter key submits connect form
  document.getElementById('connect-password').addEventListener('keydown', e => {
    if (e.key === 'Enter') document.getElementById('connect-submit').click();
  });

  // ── Disconnect ─────────────────────────────────────────────────────────────
  async function disconnectDD(ddName) {
    if (!ddName) return;
    try {
      await apiFetch('api/connect.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'disconnect', name: ddName }),
      });
      state.openConnections.delete(ddName);
      refreshTree();
      setStatus(`Disconnected from '${ddName}'`);
      updateConnectionCount();
    } catch (err) {
      setStatus(`Disconnect failed: ${err.message}`);
    }
  }

  // ── Delete row confirmation modal ──────────────────────────────────────────
  document.getElementById('del-confirm-cancel').addEventListener('click', () => {
    closeModal('modal-del-confirm');
    pendingDeleteTabId = null;
  });

  document.getElementById('del-confirm-ok').addEventListener('click', async () => {
    const tabId = pendingDeleteTabId;
    closeModal('modal-del-confirm');
    pendingDeleteTabId = null;
    const inst = tblState[tabId];
    if (!inst || inst.rowIdx < 0) return;
    const rows = inst.tbl.getRows();
    const row  = rows[inst.rowIdx];
    if (!row) return;
    try {
      await apiFetch('api/row_ops.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'delete', dd: inst.dd, table: inst.table, orig: row.getData() }),
      });
      inst.tbl.deleteRow(row);
      inst.rowIdx = -1;
      setStatus('Row deleted');
    } catch (err) {
      setStatus(`Delete failed: ${err.message}`);
    }
  });

  // ── SQL script Save / Open ─────────────────────────────────────────────────
  document.getElementById('sql-save-cancel').addEventListener('click', () => {
    closeModal('modal-sql-save');
    pendingSqlSaveTabId = null;
  });

  document.getElementById('sql-save-ok').addEventListener('click', async () => {
    const name  = document.getElementById('sql-save-name').value.trim();
    const errEl = document.getElementById('sql-save-err');
    errEl.textContent = '';
    if (!name) { errEl.textContent = 'Script name is required'; return; }
    const tabId = pendingSqlSaveTabId;
    try {
      await saveSqlScript(tabId, name);
      closeModal('modal-sql-save');
      pendingSqlSaveTabId = null;
      renderTabs();
      activateTab(tabId);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  document.getElementById('sql-save-name').addEventListener('keydown', e => {
    if (e.key === 'Enter') document.getElementById('sql-save-ok').click();
  });

  async function openSqlOpenModal() {
    const list = document.getElementById('sql-scripts-list');
    list.innerHTML = '<div style="padding:12px;color:#7f849c;">Loading…</div>';
    openModal('modal-sql-open');
    try {
      const scripts = await apiFetch('api/sql_scripts.php');
      const names = Object.keys(scripts);
      if (!names.length) {
        list.innerHTML = '<div style="padding:12px;color:#7f849c;">No saved scripts yet.</div>';
        return;
      }
      list.innerHTML = names.map(n => `
        <div class="script-row">
          <span class="script-name">${escHtml(n)}</span>
          <div style="display:flex;gap:4px;flex-shrink:0;">
            <button class="btn btn-sm script-load" data-name="${escAttr(n)}">Load</button>
            <button class="btn btn-sm btn-danger script-del"  data-name="${escAttr(n)}" title="Delete script">&#x2715;</button>
          </div>
        </div>`).join('');

      list.querySelectorAll('.script-load').forEach(btn => {
        btn.addEventListener('click', () => {
          openSqlTab(state.selectedDD, scripts[btn.dataset.name], btn.dataset.name);
          closeModal('modal-sql-open');
        });
      });
      list.querySelectorAll('.script-del').forEach(btn => {
        btn.addEventListener('click', async () => {
          await apiFetch('api/sql_scripts.php', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'delete', name: btn.dataset.name }),
          });
          btn.closest('.script-row').remove();
          if (!list.querySelector('.script-row'))
            list.innerHTML = '<div style="padding:12px;color:#7f849c;">No saved scripts yet.</div>';
        });
      });

      document.getElementById('sql-open-all').onclick = () => {
        names.forEach(n => openSqlTab(state.selectedDD, scripts[n], n));
        closeModal('modal-sql-open');
      };
    } catch (err) {
      list.innerHTML = `<div style="padding:12px;color:#f38ba8;">${escHtml(err.message)}</div>`;
    }
  }

  document.getElementById('modal-sql-open').querySelector('.modal-close')
    .addEventListener('click', () => closeModal('modal-sql-open'));

  // ── About modal ────────────────────────────────────────────────────────────
  function openAboutModal() {
    document.getElementById('modal-about').classList.add('open');
  }

  document.getElementById('about-close').addEventListener('click', () => {
    document.getElementById('modal-about').classList.remove('open');
  });

  // ── Connection submenu builder ─────────────────────────────────────────────
  async function buildConnectionMenu() {
    const dicts = await apiFetch('api/dictionaries.php').catch(() => []);
    const sessionState = await apiFetch('api/session_state.php').catch(() => ({ open: [] }));
    const openSet = new Set(sessionState.open);
    openSet.forEach(n => state.openConnections.add(n));

    const connMenu = document.getElementById('connection-submenu');
    if (!connMenu) return;

    connMenu.innerHTML = `
      <div class="drop-item" data-action="create-dd">New DD…</div>
      <div class="drop-item" data-action="open-dd">Open DD…</div>
      <div class="drop-item" data-action="free-tables">Free Tables…</div>
      <div class="drop-separator"></div>`;

    dicts.forEach(d => {
      const connected = openSet.has(d.name);
      const item = document.createElement('div');
      item.className = 'drop-item';
      item.textContent = (connected ? '● ' : '○ ') + d.name;
      item.dataset.action = connected ? 'disconnect' : 'connect';
      item.dataset.dd = d.name;
      connMenu.appendChild(item);
    });

    if (dicts.length === 0) {
      connMenu.innerHTML += `<div class="drop-item disabled">No dictionaries configured</div>`;
    }

    connMenu.innerHTML += `<div class="drop-separator"></div>
      <div class="drop-item" data-action="refresh-tree">Refresh Tree</div>`;

    updateConnectionCount();
  }

  // ── Status helpers ─────────────────────────────────────────────────────────
  function updateConnectionCount() {
    const el = document.getElementById('status-connections');
    if (el) el.textContent = `${state.openConnections.size} connection(s) open`;
  }

  // ── Close modals on overlay click ──────────────────────────────────────────
  document.querySelectorAll('.modal-overlay').forEach(overlay => {
    overlay.addEventListener('click', e => {
      if (e.target === overlay) overlay.classList.remove('open');
    });
  });

  // ── Context menu ───────────────────────────────────────────────────────────
  const ctxMenu = document.getElementById('ctx-menu');

  document.addEventListener('contextmenu', e => {
    const node = e.target.closest('.jstree-anchor[data-type]');
    if (!node) { ctxMenu.style.display = 'none'; return; }
    e.preventDefault();
    const type  = node.dataset.type  || '';
    const dd    = node.dataset.dd    || '';
    const name  = node.dataset.table || node.dataset.name || dd;
    buildCtxMenu(type, dd, name);
    ctxMenu.style.left = e.pageX + 'px';
    ctxMenu.style.top  = e.pageY + 'px';
    ctxMenu.style.display = 'block';
  });

  document.addEventListener('click', () => { ctxMenu.style.display = 'none'; });

  function buildCtxMenu(type, dd, name) {
    ctxMenu.innerHTML = '';
    const items = [];

    if (type === 'dd') {
      const connected = state.openConnections.has(dd);
      if (connected) {
        items.push({ label: 'Disconnect', action: () => disconnectDD(dd) });
        items.push({ label: 'Open SQL Editor', action: () => openSqlTab(dd) });
      } else {
        items.push({ label: 'Connect…', action: () => openConnectModal(dd) });
      }
      items.push({ label: 'Properties…', action: () => openPropertiesModal(dd) });
      items.push({ label: 'Remove from list', action: () => removeDDFromList(dd) });
    } else if (type === 'table') {
      items.push({ label: 'Open Table', action: () => openTableTab(dd, name) });
    }

    items.forEach(item => {
      const el = document.createElement('div');
      el.className = 'ctx-item';
      el.textContent = item.label;
      el.addEventListener('click', item.action);
      ctxMenu.appendChild(el);
    });
  }

  async function removeDDFromList(ddName) {
    try {
      await apiFetch('api/dictionaries.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'remove', name: ddName }),
      });
      state.openConnections.delete(ddName);
      refreshTree();
      setStatus(`Removed '${ddName}' from list`);
      updateConnectionCount();
    } catch (err) {
      setStatus(`Remove failed: ${err.message}`);
    }
  }

  // ── Properties modal ───────────────────────────────────────────────────────
  async function openPropertiesModal(ddName) {
    let d;
    try {
      const dicts = await apiFetch('api/dictionaries.php');
      d = dicts.find(x => x.name === ddName);
      if (!d) { setStatus('Entry not found in config'); return; }
    } catch (err) {
      setStatus(err.message); return;
    }

    const isFree = (d.entryType ?? 'dd') === 'free';
    document.getElementById('props-title').textContent       = ddName;
    document.getElementById('props-name').value              = ddName;
    document.getElementById('props-name-display').value      = ddName;
    document.getElementById('props-entry-type').value        = d.entryType ?? 'dd';
    document.getElementById('props-path').value              = d.path ?? '';
    document.getElementById('props-username').value          = d.username ?? '';
    document.getElementById('props-err').textContent         = '';
    // Username not meaningful for free tables
    document.getElementById('props-user-row').style.display  = isFree ? 'none' : '';
    resetToggleGroup('props-conn-type');
    document.querySelectorAll('#props-conn-type .toggle-btn').forEach(btn => {
      btn.classList.toggle('active', btn.dataset.value === (d.connType ?? 'local'));
    });
    openModal('modal-props');
    setTimeout(() => document.getElementById('props-path').focus(), 50);
  }

  document.getElementById('props-cancel').addEventListener('click', () => closeModal('modal-props'));

  document.getElementById('props-save').addEventListener('click', async () => {
    const name     = document.getElementById('props-name').value;
    const path     = document.getElementById('props-path').value.trim();
    const username = document.getElementById('props-username').value.trim();
    const connType = toggleGroupValue('props-conn-type');
    const errEl    = document.getElementById('props-err');
    errEl.textContent = '';

    if (!path) { errEl.textContent = 'Path is required'; return; }

    try {
      await apiFetch('api/dictionaries.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ action: 'update', name, path, username, connType }),
      });
      closeModal('modal-props');
      // If currently connected with old path, update session by reconnecting silently
      if (state.openConnections.has(name)) {
        await apiFetch('api/connect.php', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ action: 'connect', name, path, username, password: '' }),
        }).catch(() => {});
      }
      refreshTree();
      setStatus(`Properties saved for '${name}'`);
    } catch (err) {
      errEl.textContent = err.message;
    }
  });

  // ── Utility ────────────────────────────────────────────────────────────────
  function escHtml(s) {
    return String(s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  function escAttr(s) { return escHtml(s); }

  // ── Modal: Import SAP DD ──────────────────────────────────────────────────
  function openImportSapDDModal() {
    clearModalErr('isdd-err');
    const res = document.getElementById('isdd-result');
    if (res) { res.style.display = 'none'; res.textContent = ''; }
    const btn = document.getElementById('isdd-run');
    if (btn) { btn.textContent = 'Import'; btn.disabled = false; }
    openModal('modal-import-sap-dd');
    setTimeout(() => document.getElementById('isdd-name')?.focus(), 50);
  }

  document.getElementById('isdd-cancel')?.addEventListener('click', () => closeModal('modal-import-sap-dd'));

  document.getElementById('isdd-run')?.addEventListener('click', async () => {
    const btn      = document.getElementById('isdd-run');
    if (btn.textContent === 'Done') { closeModal('modal-import-sap-dd'); return; }

    const name     = document.getElementById('isdd-name').value.trim();
    const source   = document.getElementById('isdd-source').value.trim();
    const dest     = document.getElementById('isdd-dest').value.trim();
    const user     = document.getElementById('isdd-user').value.trim();
    const password = document.getElementById('isdd-password').value;
    const sapLib   = document.getElementById('isdd-saplib').value.trim();
    const errEl    = document.getElementById('isdd-err');
    const resEl    = document.getElementById('isdd-result');
    errEl.textContent = '';
    resEl.style.display = 'none';

    if (!name || !source || !dest || !user) {
      errEl.textContent = 'Name, source, destination, and user are required';
      return;
    }

    btn.textContent = 'Importing…';
    btn.disabled = true;
    setStatus('Running import…');

    try {
      const data = await apiFetch('api/import_sap_dd.php', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, source, dest, user, password, sapLib }),
      });

      const lines = [
        `✓ Import complete`,
        `  Group memberships: ${data.memberships}`,
        `  Permissions:       ${data.permissions}`,
        data.registered ? `  Registered as "${name}" in the connection list` : `  (already registered)`,
      ];
      if (data.warnings?.length) {
        lines.push('', 'Warnings:');
        data.warnings.forEach(w => lines.push(`  • ${w}`));
      }

      resEl.style.display = 'block';
      resEl.style.background = '#1e6621';
      resEl.style.color = '#cfffd1';
      resEl.style.whiteSpace = 'pre';
      resEl.textContent = lines.join('\n');

      btn.textContent = 'Done';
      setStatus(`Imported SAP DD as '${name}'`);
      refreshTree();
    } catch (err) {
      resEl.style.display = 'block';
      resEl.style.background = '#3b0f0f';
      resEl.style.color = '#f38ba8';
      resEl.style.whiteSpace = 'pre-wrap';
      resEl.textContent = err.message;
      btn.textContent = 'Import';
      btn.disabled = false;
      setStatus('Import failed');
    }
  });

  // ── Boot ───────────────────────────────────────────────────────────────────
  document.addEventListener('DOMContentLoaded', async () => {
    initToggleGroups();
    initTree();
    await buildConnectionMenu();
    setStatus('Ready');

    // Rebuild connection submenu each time Connection menu is opened
    const connMenuItem = document.querySelector('#menubar .menu-item[data-menu="connection"]');
    if (connMenuItem) {
      connMenuItem.addEventListener('click', buildConnectionMenu);
    }
  });

}());
