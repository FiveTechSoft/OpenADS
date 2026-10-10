// Sandbox-only renderer regression: node tests/health-consumers/render_health.js
const fs = require('fs'), vm = require('vm'), path = require('path');
const root = path.resolve(__dirname, '../..');
const source = fs.readFileSync(path.join(root, 'DA-Web/js/app.js'), 'utf8');
const fn = source.slice(source.indexOf('  function serverHealthHtml'), source.indexOf('  function showManagementLogin'));
const context = {escHtml: s => String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))};
vm.createContext(context); vm.runInContext(fn, context);
const html = context.serverHealthHtml({schema_version:1, version:'<img onerror=bad()>',
  max_sessions:0, bytes_in:4294967296, workareas:{current:1,max_used:5,rejected:null},
  worker_threads:{current:2,max_used:null,rejected:null}, semantics:{snapshot:'<private>'}});
for (const token of ['Unlimited (0)', '4294967296', 'Unavailable / not measured', '&lt;img', '&lt;private&gt;'])
  if (!html.includes(token)) throw new Error('missing ' + token);
if (html.includes('<img') || html.includes('<private>')) throw new Error('unescaped text');
if (!context.serverHealthHtml(null, 'older <server>').includes('older &lt;server&gt;')) throw new Error('fallback');
console.log('health renderer: escaping/null/wide/zero/fallback PASS');
