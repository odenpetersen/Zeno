// The browser page, embedded in the binary.
extern const char* const kUiHtml;
const char* const kUiHtml = R"zenoui(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Zeno notebook</title>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/xterm@5.3.0/css/xterm.css">
<script src="https://cdn.jsdelivr.net/npm/xterm@5.3.0/lib/xterm.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/xterm-addon-fit@0.8.0/lib/xterm-addon-fit.min.js"></script>
<style>
:root{--bg:#fcfcfb;--panel:#f4f3f0;--line:#e3e1dc;--ink:#1b1b1a;--ink2:#52514e;--ink3:#8a8984;--acc:#2a78d6;--accbg:#e8f0fb;
 --err:#c93b3a;--errbg:#fbeceb;--ok:#1b8a5a;--warn:#a86b00;--code:#f6f5f2;--kw:#7a3fc4;--str:#1b7a4a;--num:#b35c00;--com:#8a8984;--pp:#b0306a;--ty:#1d6fb8;
 --macro:#b0306a;--exp:#fff3c4;--expline:#e6b800;--pad:repeating-linear-gradient(45deg,#e9e7e2 0 4px,transparent 4px 8px);
 --s1:#2a78d6;--s2:#eb6834;--s3:#1baf7a;--s4:#eda100;--s5:#e87ba4;--s6:#008300;--s7:#6250d6;--s8:#e34948;--surface:#fcfcfb;
 --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;--sans:system-ui,-apple-system,Segoe UI,sans-serif}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#1a1a19;--panel:#222221;--line:#34332f;--ink:#f0efec;--ink2:#c3c2b7;--ink3:#8f8e86;--acc:#5a9ff0;--accbg:#1f2c3d;
 --err:#ef7070;--errbg:#3a2222;--ok:#4cc38a;--warn:#e0a63a;--code:#20201f;--kw:#c39bf5;--str:#7fd19b;--num:#f0a35e;--com:#86857e;--pp:#f08cb8;--ty:#7db8f0;
 --macro:#f08cb8;--exp:#3d3416;--expline:#c9a227;--pad:repeating-linear-gradient(45deg,#33322e 0 4px,transparent 4px 8px);
 --s1:#3987e5;--s2:#d95926;--s3:#199e70;--s4:#c98500;--s5:#d55181;--s6:#008300;--s7:#9085e9;--s8:#e66767;--surface:#1a1a19}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.5 var(--sans)}
header{position:sticky;top:0;z-index:5;display:flex;gap:8px;align-items:center;padding:8px 16px;background:var(--panel);border-bottom:1px solid var(--line);flex-wrap:wrap}
header h1{font-size:15px;margin:0 12px 0 0;font-weight:600}
header .sp{flex:1}
button{font:inherit;font-size:13px;color:var(--ink);background:transparent;border:1px solid var(--line);border-radius:6px;padding:3px 9px;cursor:pointer}
button:hover{background:var(--accbg);border-color:var(--acc)}
button.pri{background:var(--acc);color:#fff;border-color:var(--acc)}
#status{font-size:12px;color:var(--ink3);min-width:120px}
#status.busy::before{content:"";display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--acc);margin-right:6px;animation:pulse 1s infinite}
@keyframes pulse{50%{opacity:.3}}
#wrap{display:flex;align-items:flex-start}
main{flex:1;min-width:0;max-width:1100px;margin:0 auto;padding:16px 16px 40vh}
aside{width:420px;max-width:45vw;position:sticky;top:49px;height:calc(100vh - 49px);overflow:auto;border-left:1px solid var(--line);background:var(--panel);padding:12px;display:none}
aside.open{display:block}
aside h2{font-size:13px;margin:0 0 8px;text-transform:uppercase;letter-spacing:.04em;color:var(--ink2)}
.grp{margin:2px 0}
.gh{display:flex;align-items:center;gap:6px;padding:3px 4px;border-radius:6px;cursor:pointer;user-select:none}
.gh:hover{background:var(--panel)}
.gh .car,.vh .car{display:inline-block;width:14px;color:var(--ink3);font-size:14px;line-height:1;text-align:center;transition:transform .12s;transform:rotate(90deg)}
.grp.closed>.gh .car,.v.closed>.vh .car{transform:none}
.gh .ti{font-weight:600}
.gh.file .ti{font-family:var(--mono);font-size:13px}
.gh.inc .ti::before{content:"#include ";color:var(--pp);font-family:var(--mono);font-weight:400;font-size:12px}
.gh.sec1 .ti{font-size:22px}.gh.sec2 .ti{font-size:18px}.gh.sec3 .ti{font-size:15px}
.gh .cnt{color:var(--ink3);font-size:12px}
.gh .act{margin-left:auto;opacity:0}
.gh:hover .act{opacity:1}
.gb{padding-left:18px;border-left:1px solid var(--line);margin-left:10px}
.grp.closed>.gb{display:none}
.md{padding:2px 8px;color:var(--ink)}
.md p{margin:.4em 0}.md code{font-family:var(--mono);font-size:.92em;background:var(--code);padding:0 3px;border-radius:3px}
.md pre{background:var(--code);padding:8px;border-radius:6px;overflow:auto}
.ccode{font-family:var(--mono);font-size:12.5px;color:var(--com);white-space:pre;padding:2px 8px;margin:0;overflow:auto}
.cell{display:flex;gap:4px;margin:3px 0;border-radius:8px;border:1px solid transparent}
.cell.focus{border-color:var(--acc)}
.cell.sel{background:var(--accbg)}
.cell.undone{opacity:.55}
.cell.stale .out{opacity:.5}
.cell.gitmod pre.code{box-shadow:inset 3px 0 var(--s4)}
.cell.gitnew pre.code{box-shadow:inset 3px 0 var(--s3)}
.gh .git{font:11px var(--mono);color:var(--s4)}
#branch{font:12px var(--mono);color:var(--ink3)}
.res.preview{opacity:.75}
.res.preview i{font-style:normal;color:var(--ink3)}
.cell.dirty .gut .no{color:var(--warn)}
.cell.dirty .gut .no::before{content:"⟳ ";font-size:10px}
.cell.stale .gut .no::after{content:"•";color:var(--warn);margin-left:2px}
.gut{width:46px;flex:none;text-align:right;padding-top:6px;color:var(--ink3);font:12px var(--mono);cursor:pointer}
.gut .run{display:block;margin:2px 0 0 auto;padding:0 6px;font-size:11px;opacity:0}
.cell:hover .gut .run,.cell.focus .gut .run{opacity:1}
.cm{flex:1;min-width:0}
pre.code{margin:0;padding:6px 10px;background:var(--code);border-radius:6px;font:13px/1.45 var(--mono);overflow:auto;white-space:pre}
.ln{display:block}.ln.hot{background:var(--accbg)}.ln.errl{background:var(--errbg);box-shadow:inset 3px 0 var(--err)}
.k{color:var(--kw)}.s{color:var(--str)}.n{color:var(--num)}.c{color:var(--com);font-style:italic}.p{color:var(--pp)}.t{color:var(--ty)}
.mac{color:var(--macro);text-decoration:underline dotted;cursor:pointer}
.exp{background:var(--exp);box-shadow:0 0 0 1px var(--expline);border-radius:3px;cursor:pointer}
.tools{display:none;gap:4px;margin:3px 0 0;font-size:12px}
.cell:hover .tools,.cell.focus .tools{display:flex}
.tools button{font-size:11px;padding:1px 7px}
.tools .meta{margin-left:auto;color:var(--ink3);font-size:11px;align-self:center}
.out{font-size:13px}
.out>*{margin-top:4px}
pre.so,pre.se,pre.diag,pre.bt{margin:4px 0 0;padding:6px 10px;font:12.5px/1.4 var(--mono);white-space:pre-wrap;word-break:break-word;border-radius:6px}
pre.se{color:var(--err)}
pre.diag{background:var(--code);max-height:320px;overflow:auto}
.err{background:var(--errbg);color:var(--err);border-radius:6px;padding:6px 10px}
.err pre{margin:6px 0 0;color:var(--ink);font:12px/1.4 var(--mono);white-space:pre-wrap;max-height:300px;overflow:auto}
.res{display:inline-flex;gap:6px;align-items:center;font:12px var(--mono);color:var(--ink3);cursor:pointer;padding:1px 6px;border-radius:5px}
.res:hover{background:var(--panel);color:var(--ink)}
.res b{font-weight:400;color:var(--acc)}
.rich svg{max-width:100%;height:auto;color:var(--ink)}
.stats{font-size:11.5px;color:var(--ink3);display:flex;flex-wrap:wrap;gap:4px 12px;padding:2px 0}
.stats span b{color:var(--ink2);font-weight:500}
.asm{font:12px/1.4 var(--mono);background:var(--code);border-radius:6px;padding:6px 10px;max-height:420px;overflow:auto;white-space:pre}
.asm .fn{color:var(--ty);font-weight:600;margin-top:6px}
.asm .i{display:block;padding-left:8px}.asm .i.other{color:var(--ink3)}.asm .i.hot{background:var(--accbg)}
.asm .i .lnr{display:inline-block;width:32px;color:var(--ink3)}
.gh.fn .ti{font-family:var(--mono);font-size:13px;font-weight:500}
.gh.fn .ti::after{content:" { … }";color:var(--ink3)}
.gh .ro{font-size:11px;color:var(--ink3);border:1px solid var(--line);border-radius:4px;padding:0 5px}
.chat{display:flex;flex-direction:column;height:100%}
.msgs{flex:1;overflow:auto;padding-bottom:8px}
.msg{margin:8px 0;padding:8px 10px;border-radius:8px;background:var(--bg);border:1px solid var(--line);font-size:13.5px}
.msg.user{background:var(--accbg);border-color:transparent}
.msg pre.code{font-size:12px}
.msg .apply{margin:4px 0 8px}
.chatin{display:flex;flex-direction:column;gap:6px}
.chatin textarea{width:100%;min-height:64px;resize:vertical;font:13px var(--sans);padding:6px 8px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--ink)}
.chatin .row{display:flex;gap:6px;align-items:center;font-size:12px;color:var(--ink3)}
.settings{display:grid;gap:6px;margin:8px 0;font-size:12px}
.settings input{font:12px var(--mono);padding:4px 6px;border:1px solid var(--line);border-radius:5px;background:var(--bg);color:var(--ink);width:100%}
.term{border-radius:6px;overflow:hidden;background:#111;padding:4px}
textarea.ed{display:block;width:100%;box-sizing:border-box;margin:0;padding:6px 10px;background:var(--code);color:var(--ink);border:1px solid var(--s1);border-radius:6px;font:13px/1.45 var(--mono);white-space:pre;overflow:auto hidden;resize:none;tab-size:2;outline:none}
.edhint{font-size:11px;color:var(--muted);margin:2px 2px 0}
.toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:var(--ink);color:var(--bg);padding:6px 14px;border-radius:8px;font-size:13px;z-index:9;opacity:0;transition:opacity .2s}
.toast.on{opacity:.95}
/* value inspector */
.v{font:12.5px/1.5 var(--mono)}
.vh{display:flex;gap:6px;align-items:baseline;flex-wrap:wrap}
.vh .car{cursor:pointer;user-select:none}
.vn{color:var(--ink);font-weight:600}.vt{color:var(--ink3)}.vv{color:var(--num)}.vv.str{color:var(--str)}.vv.ptr{color:var(--pp)}
.vb{padding-left:16px;border-left:1px dotted var(--line);margin-left:5px}
.v.closed>.vb{display:none}
.vbtn{font:11px var(--mono);padding:0 5px;border-radius:4px;margin-left:2px}
.vdim{color:var(--ink3);font-size:11.5px}
table.lay{border-collapse:collapse;margin:3px 0;font:12px var(--mono)}
table.lay td,table.lay th{padding:1px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:top}
table.lay th{color:var(--ink3);font-weight:400;font-size:11px}
table.lay td.num{text-align:right;color:var(--ink3)}
table.lay tr.pad td{background:var(--pad);color:var(--ink3)}
.spark{vertical-align:middle}
.heapbar{display:flex;height:16px;border-radius:4px;overflow:hidden;margin:6px 0;gap:2px}
canvas#heapmap{width:100%;height:90px;border-radius:6px;background:var(--code)}
</style></head><body>
<header>
 <h1 id="title">Zeno</h1>
 <button class="pri" id="sync" title="Re-run exactly the cells that are out of date (changed, new, or depending on a change), so the state always matches running the files top to bottom">⟳ Sync</button>
 <button id="runall" title="Restart the kernel and run everything">Restart &amp; run all</button>
 <button id="intr" title="Interrupt (the run is rolled back)">■ Interrupt</button>
 <button id="undo" title="Roll back the last run">⟲ Undo</button>
 <button id="restart">Restart</button>
 <button id="extract" style="display:none">Move selected to file…</button>
 <span class="sp"></span>
 <span id="branch"></span> <span id="status">idle</span>
 <button id="teditor" title="Which editor opens when you edit a cell (remembered in this browser)"></button>
 <button id="tchat">Assistant</button><button id="tvars">Variables</button><button id="theap">Heap</button>
</header>
<div id="wrap"><main id="doc"></main><aside id="side"></aside></div>
<div class="toast" id="toast"></div>
<script>
const TOKEN = new URLSearchParams(location.search).get('token') || '';
const $ = (s, r = document) => r.querySelector(s);
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const esc = s => String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
let tree, cells = {}, order = [], outs = {}, macros = new Map(), collapsed = new Set(JSON.parse(localStorage.getItem('zeno.collapsed') || '[]'));
let ownWrite = 0, focusUid = null, selected = new Set(), batches = {}, busy = 0, ranText = {}, editing = 0, reloadPending = false;

function toast(msg) { const t = $('#toast'); t.textContent = msg; t.classList.add('on'); clearTimeout(t._h); t._h = setTimeout(() => t.classList.remove('on'), 3000); }
function setBusy(d) { busy += d; const s = $('#status'); s.classList.toggle('busy', busy > 0); s.textContent = busy > 0 ? 'running…' : 'idle'; }
async function api(path, body) {
  if (/cell|extract|term\/open/.test(path)) ownWrite = Date.now() + 600000;   // our own edits shouldn't announce themselves
  const r = await fetch(path, {method: 'POST', headers: {'X-Token': TOKEN, 'Content-Type': 'application/json'}, body: JSON.stringify(body || {})});
  return r.headers.get('content-type')?.includes('json') ? r.json() : r.text();
}
async function stream(path, body, on) {
  setBusy(1);
  try {
    const r = await fetch(path, {method: 'POST', headers: {'X-Token': TOKEN}, body: JSON.stringify(body || {})});
    const rd = r.body.getReader(), dec = new TextDecoder(); let buf = '';
    for (;;) {
      const {done, value} = await rd.read(); if (done) break;
      buf += dec.decode(value, {stream: true});
      let i; while ((i = buf.indexOf('\n')) >= 0) { const line = buf.slice(0, i); buf = buf.slice(i + 1); if (line) on(JSON.parse(line)); }
    }
  } finally { setBusy(-1); }
}
const op = (o, fields) => new Promise(res => { const evs = []; stream('/api/op', {op: o, ...fields}, e => evs.push(e)).then(() => res(evs)); });

// ---------------------------------------------------------------- text rendering
function ansi(s) {
  const cols = {30:'#666',31:'var(--err)',32:'var(--ok)',33:'var(--warn)',34:'var(--acc)',35:'var(--pp)',36:'var(--ty)',37:'var(--ink2)',90:'var(--ink3)',91:'var(--err)',92:'var(--ok)',93:'var(--warn)',94:'var(--acc)',95:'var(--pp)',96:'var(--ty)'};
  let out = '', open = 0;
  s = s.replace(/\x1b\[K/g, '');
  for (const part of s.split(/(\x1b\[[0-9;]*m)/)) {
    const m = part.match(/^\x1b\[([0-9;]*)m$/);
    if (!m) { out += esc(part); continue; }
    const codes = m[1].split(';').map(Number); let st = '';
    for (const c of codes) { if (c === 0 || isNaN(c)) { out += '</span>'.repeat(open); open = 0; } else if (c === 1) st += 'font-weight:600;'; else if (c === 2) st += 'opacity:.65;'; else if (cols[c]) st += 'color:' + cols[c] + ';'; }
    if (st) { out += '<span style="' + st + '">'; open++; }
  }
  return out + '</span>'.repeat(open);
}
function inline(s) {
  return esc(s).replace(/`([^`]+)`/g, '<code>$1</code>').replace(/\*\*([^*]+)\*\*/g, '<b>$1</b>').replace(/(^|\W)\*([^*\s][^*]*)\*/g, '$1<i>$2</i>')
    .replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (m, t, u) => /^(https?:|#|\.|\/)/.test(u) ? '<a href="' + u + '">' + t + '</a>' : m);
}
function markdown(src) {
  const lines = src.split('\n'); let html = '', para = [], list = null, fence = null;
  const flush = () => { if (para.length) html += '<p>' + inline(para.join(' ')) + '</p>'; para = []; if (list) { html += '</' + list + '>'; list = null; } };
  for (const l of lines) {
    if (fence !== null) { if (/^```/.test(l)) { html += '<pre>' + esc(fence) + '</pre>'; fence = null; } else fence += l + '\n'; continue; }
    if (/^```/.test(l)) { flush(); fence = ''; continue; }
    let m;
    if ((m = l.match(/^(#{1,6})\s+(.*)/))) { flush(); html += '<h' + (m[1].length + 2) + '>' + inline(m[2]) + '</h' + (m[1].length + 2) + '>'; }
    else if ((m = l.match(/^\s*([-*]|\d+\.)\s+(.*)/))) { if (para.length) { html += '<p>' + inline(para.join(' ')) + '</p>'; para = []; } const t = /\d/.test(m[1]) ? 'ol' : 'ul'; if (list !== t) { if (list) html += '</' + list + '>'; html += '<' + t + '>'; list = t; } html += '<li>' + inline(m[2]) + '</li>'; }
    else if (!l.trim()) flush();
    else { if (list) { html += '</' + list + '>'; list = null; } para.push(l); }
  }
  flush();
  return html;
}
const KW = new Set('alignas alignof asm auto bool break case catch char char8_t char16_t char32_t class concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept nullptr operator private protected public register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while'.split(' '));
// Highlight C++; returns HTML with one span.ln per line. `exps` = [{b,e,text}] macro expansions shown in place.
function hl(code, exps) {
  const toks = [];
  const re = /(\/\/[^\n]*|\/\*[\s\S]*?\*\/)|(R"([^(]*)\([\s\S]*?\)\3"|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])+')|(^[ \t]*#[ \t]*\w+)|(\b\d[\w.']*)|([A-Za-z_]\w*)|(\s+|.)/gm;
  let m;
  while ((m = re.exec(code))) {
    const t = m[0], at = m.index;
    const ex = (exps || []).find(x => x.b === at);
    if (ex) { toks.push({cls: 'exp', text: ex.text, at}); re.lastIndex = ex.e; continue; }
    let cls = m[1] ? 'c' : m[2] ? 's' : m[4] ? 'p' : m[5] ? 'n' : m[6] ? (KW.has(t) ? 'k' : macros.has(t) ? 'mac' : /^[A-Z]\w*[a-z]\w*$|_t$/.test(t) ? 't' : '') : '';
    toks.push({cls, text: t, at});
  }
  let html = '<span class="ln" data-l="0">', ln = 0;
  for (const t of toks) {
    const parts = t.text.split('\n');
    parts.forEach((p, i) => {
      if (i) html += '</span><span class="ln" data-l="' + (++ln) + '">';
      if (!p) return;
      const attr = t.cls === 'mac' || t.cls === 'exp' ? ' data-at="' + t.at + '"' : '';
      html += t.cls ? '<span class="' + t.cls + '"' + attr + (t.cls === 'exp' ? ' title="click to collapse"' : t.cls === 'mac' ? ' title="click to expand"' : '') + '>' + esc(p) + '</span>' : esc(p);
    });
  }
  return html + '</span>';
}

// ---------------------------------------------------------------- value inspector
function spark(sp) {
  const v = sp.filter(x => x != null); if (v.length < 2) return '';
  const mn = Math.min(...v), mx = Math.max(...v), W = 120, H = 22;
  const d = sp.map((y, i) => (i ? 'L' : 'M') + (i / (sp.length - 1) * W).toFixed(1) + ',' + (y == null ? H / 2 : (H - 2 - (y - mn) / ((mx - mn) || 1) * (H - 4))).toFixed(1)).join('');
  return '<svg class="spark" width="' + W + '" height="' + H + '"><path d="' + d + '" fill="none" stroke="var(--s1)" stroke-width="1.5"/></svg>';
}
const scalar = n => ['num', 'str', 'ptr'].includes(n.k) && !n.c;
function valHTML(n) {
  if (n.k === 'str') return '<span class="vv str">' + esc(JSON.stringify(n.v)) + '</span>';
  if (n.k === 'ptr') return '<span class="vv ptr">' + esc(n.v) + '</span>' + (n.h ? ' <span class="vdim">' + esc(n.h) + '</span>' : '') + (n.uses != null ? ' <span class="vdim">use_count ' + n.uses + '</span>' : '');
  const v = String(n.v ?? ''), short = /^-?\d+\.\d{7,}(e[-+]?\d+)?$/.test(v) ? (+v).toPrecision(6) : v;   // full precision on hover
  return '<span class="vv" title="' + esc(v) + '">' + esc(short) + '</span>';
}
function lazyBtn(label, ref, into, replace) {
  const b = el('button', 'vbtn', label);
  b.onclick = async e => {
    e.stopPropagation(); b.disabled = true;
    const evs = await op('expand', {ref});
    const nd = evs.find(x => x.ev === 'node'), er = evs.find(x => x.ev === 'error');
    if (nd) { const v = nodeEl(nd.node, replace ? null : '*', 0); if (replace) into.replaceWith(v); else into.append(v); }
    else into.append(el('div', 'vdim', er ? er.what : 'not readable'));
    if (!replace) b.remove();
  };
  return b;
}
function nodeEl(n, name, depth) {
  const v = el('div', 'v'), h = el('div', 'vh'), body = el('div', 'vb');
  v.append(h);
  const car = el('span', 'car', '');
  h.append(car);
  if (name != null) h.append(el('span', 'vn', name));
  h.insertAdjacentHTML('beforeend', '<span class="vt">' + esc(n.t || '') + '</span>');
  if (n.k === 'more') { (n.c || []).forEach(c => v.append(nodeEl(c, c.n, depth + 1))); if (n.more) v.append(lazyBtn('more…', n.more, v, false)); h.remove(); return v; }
  if (scalar(n) || (n.k === 'obj' && !n.bytes)) {
    h.insertAdjacentHTML('beforeend', ' = ' + valHTML(n));
    if (n.x) { const b = lazyBtn('▸ *', n.x, body, false); h.append(b); v.append(body); }
    if (n.xa) { h.append(lazyBtn('[ ]', n.xa, body, false)); if (!body.parentNode) v.append(body); }
    return v;
  }
  car.textContent = '›';
  car.onclick = () => v.classList.toggle('closed');
  if (depth > 1) v.classList.add('closed');
  v.append(body);
  if (n.k === 'rec') {
    h.insertAdjacentHTML('beforeend', '<span class="vdim">' + n.z + ' B, align ' + n.al + '</span>' + (n.v ? ' <span class="vv">' + esc(n.v) + '</span>' : ''));
    if (!n.c) { if (n.x) body.append(lazyBtn('expand', n.x, v, true)); return v; }
    const t = el('table', 'lay'); t.innerHTML = '<tr><th>offset</th><th>size</th><th>field</th><th>type</th><th>value</th></tr>';
    const rows = n.c.map(c => ({o: c.o, c})).concat((n.p || []).map(p => ({o: p[0], pad: p[1]}))).sort((a, b) => a.o - b.o);
    for (const r of rows) {
      const tr = el('tr', r.pad ? 'pad' : '');
      if (r.pad) { tr.innerHTML = '<td class="num">' + r.o + '</td><td class="num">' + r.pad + '</td><td colspan="3">padding</td>'; t.append(tr); continue; }
      const c = r.c; tr.innerHTML = '<td class="num">' + c.o + '</td><td class="num">' + c.z + '</td><td>' + esc(c.n) + '</td><td class="vt">' + esc(c.t) + '</td>';
      const td = el('td'); tr.append(td);
      if (scalar(c)) { td.innerHTML = valHTML(c); if (c.x) { const box = el('div'); td.append(lazyBtn('▸ *', c.x, box, false), box); } if (c.xa) { const box = el('div'); td.append(lazyBtn('[ ]', c.xa, box, false), box); } }
      else { const sub = nodeEl(c, null, depth + 1); td.append(sub); }
      t.append(tr);
    }
    body.append(t);
  } else if (n.k === 'seq' || n.k === 'map') {
    h.insertAdjacentHTML('beforeend', '<span class="vdim">size ' + n.n + '</span>');
    if (n.st) h.insertAdjacentHTML('beforeend', spark(n.sp) + '<span class="vdim">min ' + n.st.min.toPrecision(4) + ' · max ' + n.st.max.toPrecision(4) + ' · mean ' + n.st.mean.toPrecision(4) + ' · std ' + n.st.std.toPrecision(4) + '</span>');
    if (!n.c) { if (n.x) body.append(lazyBtn('expand', n.x, v, true)); return v; }
    const recs = n.c.length && n.c.every(c => c.k === 'rec' && c.c && c.c.every(f => scalar(f)));
    if (recs) {   // a sequence of flat records is a table
      const cols = n.c[0].c.map(f => f.n), t = el('table', 'lay');
      t.innerHTML = '<tr><th></th>' + cols.map(c => '<th>' + esc(c) + '</th>').join('') + '</tr>';
      for (const r of n.c) t.insertAdjacentHTML('beforeend', '<tr><td class="num">' + esc(r.n) + '</td>' + r.c.map(f => '<td>' + valHTML(f) + '</td>').join('') + '</tr>');
      body.append(t);
    } else if (n.k === 'seq' && n.c.every(c => scalar(c) && c.k !== 'ptr')) {
      const d = el('div'), few = n.c.slice(0, 24);
      d.innerHTML = '[' + few.map(valHTML).join(', ') + (n.c.length > few.length || n.more ? ', …' : '') + ']';
      body.append(d);
      if (n.c.length > few.length) { const b = el('button', 'vbtn', 'show ' + n.c.length + (n.more ? '+' : '')); b.onclick = () => { d.innerHTML = '[' + n.c.map(valHTML).join(', ') + (n.more ? ', …' : '') + ']'; b.remove(); }; body.append(b); }
    } else n.c.forEach(c => body.append(nodeEl(c, c.n, depth + 1)));
    if (n.more) { const box = el('div'); body.append(box, lazyBtn('more…', n.more, box, false)); }
  } else if (n.k === 'opt') {
    h.insertAdjacentHTML('beforeend', '<span class="vdim">' + esc(n.v || '') + '</span>');
    (n.c || []).forEach(c => body.append(nodeEl(c, c.n, depth + 1)));
  } else {   // opaque object
    h.insertAdjacentHTML('beforeend', '<span class="vdim">' + n.z + ' B</span>');
    body.append(el('div', 'vdim', n.bytes || ''));
    const b = el('button', 'vbtn', 'memory layout (gdb)');
    b.onclick = async () => { const evs = await op('layout', {type: n.rt}); const l = evs.find(x => x.ev === 'layout'); body.append(el('pre', 'diag', l ? l.text : '?')); b.remove(); };
    body.append(b);
  }
  return v;
}

// ---------------------------------------------------------------- document
function saveCollapsed() { localStorage.setItem('zeno.collapsed', JSON.stringify([...collapsed])); }
function countCells(n) { if (n.k === 'code') return 1; if (n.k === 'inc') return n.file ? countCells(n.file) : 0; return (n.kids || []).reduce((a, k) => a + countCells(k), 0); }
function group(kind, id, title, kids, runTarget, extraCls, defaultClosed, ro) {
  const g = el('div', 'grp'), h = el('div', 'gh ' + kind + ' ' + (extraCls || '')), b = el('div', 'gb');
  const key = kind + ':' + id;
  g.dataset.key = key; g.dataset.dc = defaultClosed ? '1' : '';
  const closed = collapsed.has(key) || (defaultClosed && !collapsed.has('open:' + key));
  if (closed) g.classList.add('closed');
  const car = el('span', 'car', '›'), cnt = el('span', 'cnt');
  h.append(car, el('span', 'ti', title), cnt);
  if (ro) { const r = el('span', 'ro', 'read-only'); r.title = 'listed in .zeno.json "readonly"'; h.append(r); }
  const n = kids.reduce((a, k) => a + countCells(k), 0);
  cnt.textContent = n + (n === 1 ? ' cell' : ' cells');
  if (runTarget != null) { const r = el('button', 'act', '▶ run'); r.onclick = e => { e.stopPropagation(); run(runTarget, 'group'); }; h.append(r); }
  h.onclick = () => {
    const c = g.classList.toggle('closed');
    if (c) { collapsed.add(key); collapsed.delete('open:' + key); } else { collapsed.delete(key); if (defaultClosed) collapsed.add('open:' + key); }
    saveCollapsed();
  };
  g.append(h, b);
  kids.forEach(k => render(k, b, ctx));
  return g;
}
let ctx = {file: '', ro: false};
function render(n, into) {
  const k = n.k;
  if (k === 'entry') {
    render(n.main, into);
    if (n.others.length) into.append(group('others', 'others', 'Other files (not included from the entry point)', n.others, null, '', true));
  } else if (k === 'file' || k === 'dir') {   // everything starts collapsed except the top of the document
    const saved = {...ctx}; if (k === 'file') { ctx.file = n.path; ctx.ro = !!n.ro; }
    const top = into === $('#doc');
    into.append(group(k, n.id, n.title, n.kids, n.id, '', !top, n.ro));
    ctx = saved;
  } else if (k === 'sec') into.append(group('sec', n.id, n.title, n.kids, n.id, 'sec' + n.level, true));
  else if (k === 'fn') into.append(group('fn', n.id, n.title, n.kids, null, '', true));
  else if (k === 'inc') {
    if (n.dup) { into.append(el('div', 'ccode', n.text + '   // already included above')); return; }
    const saved = {...ctx}; ctx.file = n.file.path; ctx.ro = !!n.file.ro;
    const g = group('inc', n.file.id, n.file.title, n.file.kids, n.file.id, '', true, n.file.ro);
    ctx = saved;
    into.append(g);
  } else if (k === 'md') { const d = el('div', 'md'); d.innerHTML = markdown(n.text); into.append(d); }
  else if (k === 'ccode') into.append(el('pre', 'ccode', n.text));
  else if (k === 'code') into.append(cellEl(n));
}
function cellEl(n) {
  const c = el('div', 'cell'), gut = el('div', 'gut'), cm = el('div', 'cm'), pre = el('pre', 'code'), tools = el('div', 'tools');
  order.push(n.uid);
  const info = cells[n.uid] = {uid: n.uid, text: n.text, line: n.line, file: ctx.file, nlines: n.text.split('\n').length, el: c, pre, exps: [], no: order.length};
  c.dataset.uid = n.uid; c.tabIndex = -1;
  gut.append(el('span', 'no', '[' + order.length + ']'));
  const rb = el('button', 'run', '▶'); rb.title = 'Bring this cell up to date: runs it and anything out of date before it (Shift+Enter). Alt+click: run just this cell regardless.';
  rb.onclick = e => { e.stopPropagation(); e.altKey ? run(n.uid, 'cell') : sync(n.uid); };
  gut.append(rb);
  gut.onclick = e => { if (e.shiftKey || e.metaKey || e.ctrlKey) toggleSel(n.uid); else focus(n.uid); };
  pre.innerHTML = hl(n.text, []);
  pre.onclick = e => {
    const m = e.target.closest('.mac,.exp');
    if (m) { toggleMacro(info, +m.dataset.at, m.classList.contains('exp')); return; }
    focus(n.uid);
  };
  pre.ondblclick = () => { if (!ctx.ro) edit(n.uid); };
  info.ro = ctx.ro;
  const acts = [['ask', () => askAbout(n.uid), 'Ask the assistant about this cell'], ['asm', () => showAsm(info), 'Disassembly of this cell']];
  if (!ctx.ro) acts.unshift(['edit', () => edit(n.uid), 'Edit (Enter)']), acts.push(['+ below', () => insertBelow(n.uid), 'New cell below (b)'], ['delete', () => del(n.uid), 'Delete cell']);
  for (const [label, f, tip] of acts) {
    const b = el('button', '', label); b.title = tip; b.onclick = e => { e.stopPropagation(); f(); }; tools.append(b);
  }
  info.meta = el('span', 'meta'); tools.append(info.meta);
  const out = outs[n.uid] || el('div', 'out'); outs[n.uid] = out; info.out = out;
  cm.append(pre, tools, out);
  c.append(gut, cm);
  if (selected.has(n.uid)) c.classList.add('sel');
  if (ranText[n.uid] != null && ranText[n.uid] !== n.text) { c.classList.add('stale'); c.title = 'changed since it last ran'; }
  return c;
}
// Cells are addressed by position (file:index), which shifts when code changes. To keep outputs with the
// right cells, align the old and new cell lists like a diff: identical text matches first (longest common
// subsequence), and leftover cells between matches pair up in order, which is an edit (shown as stale).
function codeList(n, out = []) {
  if (n.k === 'code') out.push({uid: n.uid, text: n.text});
  for (const k of n.kids || []) codeList(k, out);
  if (n.main) codeList(n.main, out);
  for (const k of n.others || []) codeList(k, out);
  if (n.file) codeList(n.file, out);
  return out;
}
function align(a, b) {   // -> Map(new uid -> old uid)
  const n = a.length, m = b.length, map = new Map();
  if (n * m > 4e6) { b.forEach(x => a.some(y => y.uid === x.uid) && map.set(x.uid, x.uid)); return map; }
  const L = Array.from({length: n + 1}, () => new Int32Array(m + 1));
  for (let i = n - 1; i >= 0; --i) for (let j = m - 1; j >= 0; --j) L[i][j] = a[i].text === b[j].text ? L[i + 1][j + 1] + 1 : Math.max(L[i + 1][j], L[i][j + 1]);
  let i = 0, j = 0, ga = [], gb = [];
  const flushGap = () => { for (let k = 0; k < Math.min(ga.length, gb.length); ++k) map.set(gb[k].uid, ga[k].uid); ga = []; gb = []; };
  while (i < n && j < m) {
    if (a[i].text === b[j].text) { flushGap(); map.set(b[j].uid, a[i].uid); ++i; ++j; }
    else if (L[i + 1][j] >= L[i][j + 1]) ga.push(a[i++]); else gb.push(b[j++]);
  }
  while (i < n) ga.push(a[i++]); while (j < m) gb.push(b[j++]);
  flushGap();
  return map;
}
let lastList = [];
function carryState(tree) {
  const list = codeList(tree), map = align(lastList, list);
  const re = obj => { const o = {}; for (const [nu, ou] of map) if (obj[ou] !== undefined) o[nu] = obj[ou]; return o; };
  outs = re(outs); ranText = re(ranText); batches = re(batches);
  const inv = new Map([...map].map(([nu, ou]) => [ou, nu]));
  selected = new Set([...selected].map(u => inv.get(u)).filter(Boolean));
  if (focusUid) focusUid = inv.get(focusUid) || null;
  lastList = list;
}
async function load() {
  if (ownWrite > Date.now()) ownWrite = Date.now();
  const r = await fetch('/api/tree', {headers: {'X-Token': TOKEN}});
  if (r.status === 403) { $('#doc').textContent = 'Open the URL printed by zeno (it carries an access token).'; return; }
  const j = await r.json();
  tree = j.tree; cells = {}; order = [];
  carryState(tree);
  $('#title').textContent = j.root.split('/').filter(Boolean).pop() || 'Zeno';
  document.title = $('#title').textContent + ' — Zeno';
  const doc = $('#doc'); doc.textContent = '';
  render(tree, doc);
  if (!order.length) {   // nothing to show yet: offer the starter notebook
    const card = el('div', 'md');
    card.innerHTML = '<h3>No C++ here yet</h3><p>This folder has no source files. Start from a short tour notebook, or create <code>main.cpp</code> yourself: every top-level statement in it becomes a cell.</p>';
    const b = el('button', 'pri', 'Create a starter notebook');
    b.onclick = async () => { await api('/api/new'); await load(); };
    card.append(b); doc.append(card);
  }
  if (focusUid && cells[focusUid]) cells[focusUid].el.classList.add('focus');
  loadMacros(); markDirty(); gitMarks(); previewTypes();
}
async function loadMacros() {
  const pre = order.map(u => cells[u].text).filter(t => /^\s*#\s*define\b/.test(t)).join('\n');
  const evs = await op('macros', {pre});
  const m = evs.find(e => e.ev === 'macros');
  if (!m) return;
  macros = new Map(m.names.map(([n, f]) => [n, f]));
  for (const u of order) { const c = cells[u]; if (/[A-Z_]{2,}|\w+\s*\(/.test(c.text)) c.pre.innerHTML = hl(c.text, c.exps); }
}
async function toggleMacro(c, at, collapse) {
  if (collapse) c.exps = c.exps.filter(x => x.b !== at);
  else {
    const pre = order.slice(0, order.indexOf(c.uid)).map(u => cells[u].text).filter(t => /^\s*#\s*define\b/.test(t)).join('\n');
    const evs = await op('macro', {code: c.text, at, pre});
    const m = evs.find(e => e.ev === 'macro');
    if (m) c.exps.push(m.m);
  }
  c.pre.innerHTML = hl(c.text, c.exps);
}

// ---------------------------------------------------------------- focus, selection, keys
function focus(uid) {
  if (focusUid && cells[focusUid]) cells[focusUid].el.classList.remove('focus');
  focusUid = uid;
  if (uid && cells[uid]) { cells[uid].el.classList.add('focus'); cells[uid].el.scrollIntoView({block: 'nearest'}); }
}
function toggleSel(uid) {
  selected.has(uid) ? selected.delete(uid) : selected.add(uid);
  cells[uid]?.el.classList.toggle('sel', selected.has(uid));
  $('#extract').style.display = selected.size ? '' : 'none';
  $('#extract').textContent = 'Move ' + selected.size + ' selected to file…';
}
document.addEventListener('keydown', e => {
  if (e.target.closest('.term,input,textarea,aside')) return;
  const i = order.indexOf(focusUid);
  if (e.key === 'Enter' && e.shiftKey && focusUid) { e.preventDefault(); sync(focusUid); focus(order[Math.min(i + 1, order.length - 1)]); }
  else if (e.key === 'Enter' && focusUid) { e.preventDefault(); edit(focusUid); }
  else if (e.key === 'j' || e.key === 'ArrowDown') { e.preventDefault(); focus(order[Math.min(i + 1, order.length - 1)]); }
  else if (e.key === 'k' || e.key === 'ArrowUp') { e.preventDefault(); focus(order[Math.max(i - 1, 0)]); }
  else if (e.key === 'b' && focusUid) insertBelow(focusUid);
  else if (e.key === 'x' && focusUid) toggleSel(focusUid);
  else if (e.key === 'Escape') { [...selected].forEach(toggleSel); }
});

// ---------------------------------------------------------------- running
// Open every collapsed group around a cell, and remember that, so reloads keep it visible.
function showCell(uid) {
  let g = cells[uid]?.el.closest('.grp.closed');
  while (g) {
    g.classList.remove('closed');
    const k = g.dataset.key; collapsed.delete(k); if (g.dataset.dc) collapsed.add('open:' + k);
    g = g.parentElement.closest('.grp.closed');
  }
  saveCollapsed();
}
function cellOut(uid) { return cells[uid]?.out || outs[uid]; }
const ME = Math.random().toString(36).slice(2);
async function run(target, kind) {
  const h = runHandler();
  await stream('/api/run', {target, kind, origin: ME}, h);
  markDirty();
  if ($('#side').classList.contains('open') && sideMode !== 'chat') refreshSide();
}
async function sync(upto) {
  const h = runHandler(); let up = false;
  await stream('/api/sync', {upto: upto || '', origin: ME}, e => { if (e.ev === 'uptodate') up = true; else h(e); });
  if (up) toast(upto ? 'up to date — nothing to re-run (Alt+click ▶ forces a run)' : 'everything is up to date');
  markDirty();
  if ($('#side').classList.contains('open') && sideMode !== 'chat') refreshSide();
}
// Before a cell has run, show the type and layout of the value it will produce (compiled, not executed).
let previewing = false;
async function previewTypes() {
  if (previewing || !order.some(u => !cells[u].out.querySelector('.res'))) return;
  previewing = true;
  try {
    const p = await api('/api/preview');
    for (const [uid, text] of Object.entries(p.types || {})) {
      const c = cells[uid]; if (!c || c.out.childElementCount) continue;
      const m = /type = (?:class |struct )?([^\n{]*?)(?: \[with .*)?(?: :.*)?\s*(\{|$)/m.exec(text);
      const ptr = /\}\s*(\*+|&)\s*$/.exec(text.trim());
      const r = el('div', 'res preview'); r.innerHTML = '⤷ <span>' + esc((m ? m[1] : 'value').trim() + (ptr ? ' ' + ptr[1] : '')) + '</span> <i>not run</i> <b>layout</b>';
      r.title = 'Type of this cell\'s value, from the compiler. Run it to see the value.';
      r.onclick = () => { const n = r.nextElementSibling; if (n?.classList.contains('diag')) n.remove(); else r.after(Object.assign(el('pre', 'diag'), {textContent: text.trim()})); };
      c.out.append(r);
    }
  } finally { previewing = false; }
}
// Git: branch, per-file status, and a bar on cells that differ from HEAD.
async function gitMarks() {
  const g = await api('/api/git');
  $('#branch').textContent = g.repo ? '⎇ ' + g.branch : '';
  if (!g.repo) return;
  for (const u of order) {
    const c = cells[u], st = g.files[c.file] || '', ranges = g.changed[c.file] || [];
    const hit = ranges.some(([a, n]) => a < c.line + c.nlines && a + Math.max(n, 1) > c.line);
    c.el.classList.toggle('gitnew', st.includes('?') || st.includes('A'));
    c.el.classList.toggle('gitmod', !st.includes('?') && hit);
  }
}
async function markDirty() {
  const p = await api('/api/plan');
  const d = new Set(p.dirty || []);
  for (const u of order) { const c = cells[u]; c.el.classList.toggle('dirty', d.has(u)); c.el.querySelector('.gut .no').title = d.has(u) ? 'out of date: Sync will run it' : ''; }
}
// Renders one run's events into the cells; used for this page's runs and for runs started elsewhere (agents, other tabs).
function runHandler() {
  let cur = null, first = null, uids = [], marks = [];
  const at = () => cellOut(cur || first) || $('#doc');
  const pre = (cls, text) => { const o = at(); let p = o.lastElementChild; if (!p || !p.classList.contains(cls)) { p = el('pre', cls); o.append(p); } p.insertAdjacentHTML('beforeend', ansi(text)); };
  return e => {
    switch (e.ev) {
      case 'batch':
        uids = e.uids; first = uids[0];
        for (const u of uids) { batches[u] = e.id; if (cells[u]) { ranText[u] = cells[u].text; cells[u].el.classList.remove('stale'); } const o = cellOut(u); if (o) o.textContent = ''; cells[u]?.el.classList.remove('undone'); if (cells[u]) cells[u].meta.textContent = ''; cells[u]?.pre.querySelectorAll('.errl').forEach(x => x.classList.remove('errl')); }
        if (!uids.length) toast('nothing to run');
        break;
      case 'cell': if (cur) marks.push([cur, e]); cur = e.uid; marks.push([cur, e, 'start']); break;
      case 'out': pre('so', e.s); break;
      case 'err': pre('se', e.s); break;
      case 'status': toast(e.msg); break;
      case 'compile': if (e.diag) placeDiag(e.diag, uids, e.ok); break;
      case 'result': {
        const o = cellOut(e.uid); if (!o) break;
        const r = el('div', 'res'); r.innerHTML = '⤷ <span>' + esc(e.t) + '</span> <b>reveal</b>'; r.title = 'Show the value';
        r.onclick = async () => {
          if (r.nextElementSibling?.classList.contains('v')) { r.nextElementSibling.remove(); r.querySelector('b').textContent = 'reveal'; return; }
          const evs = await op('expand', {ref: e.ref}); const nd = evs.find(x => x.ev === 'node'), er = evs.find(x => x.ev === 'error');
          r.after(nd ? nodeEl(nd.node, null, 0) : el('div', 'vdim', er ? er.what : 'unavailable')); r.querySelector('b').textContent = 'hide';
        };
        o.append(r); break;
      }
      case 'rich': {
        const d = el('div', 'rich');
        if (e.mime === 'image/svg+xml' || e.mime === 'text/html') d.innerHTML = e.data;
        else d.innerHTML = '<pre class="so">' + (e.mime === 'text/x-stack' ? '<b>stack snapshot (gdb)</b>\n' : '') + ansi(e.data) + '</pre>';
        at().append(d); break;
      }
      case 'error': { showCell(cur || first); const d = el('div', 'err'); d.innerHTML = '<b>' + esc(e.type) + '</b>: ' + esc(e.what); at().append(d); break; }
      case 'crash': { showCell(cur || first); const d = el('div', 'err'); d.innerHTML = '<b>' + esc(e.sig) + '</b> at ' + esc(e.addr) + '<pre>' + esc(e.bt) + '</pre>'; at().append(d); break; }
      case 'asm': showAsmData(e.asm, uids); break;
      case 'stats': {
        const last = cellOut(uids[uids.length - 1]); if (!last) break;
        const s = el('div', 'stats'), fmtB = b => b > 1048576 ? (b / 1048576).toFixed(1) + ' MB' : b > 1024 ? (b / 1024).toFixed(1) + ' KB' : b + ' B';
        const items = [['compile', e.compile_ms.toFixed(0) + ' ms'], ['run', e.run_ms < 1 ? (e.run_ms * 1000).toFixed(0) + ' µs' : e.run_ms.toFixed(1) + ' ms']];
        if (e.instructions > 0) items.push(['instr', (e.instructions / 1e6).toFixed(2) + 'M'], ['IPC', (e.instructions / e.cycles).toFixed(2)], ['cache miss', (e.cache_misses / 1e3).toFixed(1) + 'k'], ['branch miss', (e.branch_misses / 1e3).toFixed(1) + 'k']);
        items.push(['allocs', e.allocs + ' (' + fmtB(e.alloc_bytes) + ')'], ['heap Δ', fmtB(e.heap_net)], ['peak stack', fmtB(e.stack)], ['RSS', fmtB(e.rss_kb * 1024)]);
        s.innerHTML = (uids.length > 1 ? '<span><b>run of ' + uids.length + ' cells</b></span>' : '') + items.map(([k, v]) => '<span>' + k + ' <b>' + v + '</b></span>').join('');
        last.append(s);
        // per-cell time from the markers
        const starts = marks.filter(m => m[2]).map(m => m[1]);
        starts.forEach((m, i) => { const nx = starts[i + 1]; const dt = nx ? nx.t - m.t : starts[0].t + e.run_ms - m.t; const c = cells[m.uid]; if (c && dt >= 0) c.meta.textContent = dt < 1 ? (dt * 1000).toFixed(0) + ' µs' : dt.toFixed(1) + ' ms'; });
        break;
      }
      case 'restored': toast('kernel state was lost (' + e.why + '); restored to an earlier snapshot'); break;
      case 'done':
        if (e.ok === false && e.why) {
          const d = el('div', 'err'); d.innerHTML = '⟲ <b>' + esc(e.why) + '</b> — this run was rolled back' + (uids.length > 1 ? '; the cells above it in the run were undone too' : '');
          at().append(d);
          const k = uids.indexOf(cur); uids.slice(0, Math.max(0, k)).forEach(u => cells[u]?.el.classList.add('undone'));
        }
        break;
    }
  };
}
function placeDiag(text, uids, ok) {
  const lines = text.split('\n'), plain = s => s.replace(/\x1b\[[0-9;]*[mK]/g, '');
  let target = uids[0], buckets = {};
  for (const l of lines) {
    const m = plain(l).match(/^(\/[^:]+):(\d+):(\d+):/);
    if (m) {
      const file = m[1], line = +m[2];
      const hit = uids.find(u => cells[u] && cells[u].file === file && line >= cells[u].line && line < cells[u].line + cells[u].nlines);
      if (hit) { target = hit; if (!ok) showCell(hit); if (/error/.test(plain(l))) cells[hit].pre.querySelector('.ln[data-l="' + (line - cells[hit].line) + '"]')?.classList.add('errl'); }
    }
    (buckets[target] = buckets[target] || []).push(l);
  }
  for (const [u, ls] of Object.entries(buckets)) {
    const o = cellOut(u) || $('#doc'); const p = el('pre', 'diag'); p.innerHTML = ansi(ls.join('\n'));
    if (ok) { p.style.maxHeight = '90px'; p.title = 'warnings'; }
    o.append(p);
  }
}

// ---------------------------------------------------------------- disassembly
async function showAsm(c) {
  const id = batches[c.uid];
  if (!id) { toast('run the cell first'); return; }
  const evs = await op('asm', {id});
  const a = evs.find(e => e.ev === 'asm'); if (a) showAsmData(a.asm, [c.uid], true);
}
function showAsmData(a, uids, force) {
  for (const u of uids) {
    const c = cells[u]; if (!c) continue;
    const fi = a.files.indexOf(c.file);
    const inCell = i => i[0] === fi && i[1] >= c.line && i[1] < c.line + c.nlines;
    const fns = a.fns.filter(f => f.ins.some(inCell));
    if (!fns.length) { if (force) c.out.append(el('div', 'vdim', 'no machine code for this cell (declarations only, or inlined elsewhere)')); continue; }
    c.out.querySelector('.asm')?.remove();
    const box = el('div', 'asm');
    for (const f of fns) {
      box.append(el('div', 'fn', f.fn.replace(/\{lambda\(\)#\d+\}::operator\(\)\(\) const/, '‹cell code›')));
      for (const i of f.ins) {
        const d = el('span', 'i' + (inCell(i) ? '' : ' other'));
        d.innerHTML = '<span class="lnr">' + (inCell(i) ? i[1] - c.line + 1 : '') + '</span>' + esc(i[2]);
        if (inCell(i)) {
          const l = i[1] - c.line;
          d.onmouseenter = () => { c.pre.querySelectorAll('.ln')[l]?.classList.add('hot'); box.querySelectorAll('.i').forEach(x => x.classList.toggle('hot', x._l === l)); };
          d.onmouseleave = () => { c.pre.querySelectorAll('.hot').forEach(x => x.classList.remove('hot')); box.querySelectorAll('.hot').forEach(x => x.classList.remove('hot')); };
          d._l = l;
        }
        box.append(d);
      }
    }
    c.out.append(box);
  }
}

// ---------------------------------------------------------------- editing (a plain editor, or vim in a terminal inside the cell)
let editor = localStorage.getItem('zeno.editor') === 'vim' ? 'vim' : 'plain';
function showEditor() { $('#teditor').textContent = 'editor: ' + editor; }
async function edit(uid) {
  const c = cells[uid]; if (!c || c.editing) return;
  if (c.ro) { toast('read-only (listed in .zeno.json)'); return; }
  editing++; c.editing = true;
  try { showCell(uid); await (editor === 'vim' && window.Terminal ? editVim(c, uid) : editPlain(c, uid)); }
  finally { c.editing = false; editing--; if (reloadPending && !editing) { reloadPending = false; load(); } }
}
// A textarea in place of the cell. Ctrl+S or Ctrl+Enter saves, Shift+Enter saves and syncs, Esc cancels, Ctrl+Space asks the model.
function editPlain(c, uid) {
  return new Promise(done => {
    const box = el('div'), ta = el('textarea', 'ed'), hint = el('div', 'edhint', 'Ctrl+S save · Shift+Enter save & sync · Esc cancel · Ctrl+Space complete');
    ta.value = c.text; ta.spellcheck = false; box.append(ta, hint); c.pre.replaceWith(box);
    const fit = () => { ta.style.height = 'auto'; ta.style.height = ta.scrollHeight + 2 + 'px'; };
    const put = t => { ta.setRangeText(t, ta.selectionStart, ta.selectionEnd, 'end'); fit(); };
    const finish = async (save, run) => {
      if (save && ta.value !== c.text) await api('/api/cell', {action: 'replace', uid, text: ta.value});
      focusUid = uid; ownWrite = Date.now(); await load(); done();
      if (run) sync(uid);
    };
    ta.oninput = fit;
    ta.onkeydown = async e => {
      const ctl = e.ctrlKey || e.metaKey;
      if (e.key === 'Escape') { e.preventDefault(); finish(false); }
      else if ((ctl && (e.key === 's' || e.key === 'Enter')) || (e.key === 'Enter' && e.shiftKey)) { e.preventDefault(); finish(true, e.shiftKey); }
      else if (e.key === 'Tab') { e.preventDefault(); put('  '); }
      else if (e.key === 'Enter') { e.preventDefault(); const line = ta.value.slice(ta.value.lastIndexOf('\n', ta.selectionStart - 1) + 1, ta.selectionStart); put('\n' + /^\s*/.exec(line)[0]); }
      else if (ctl && e.key === ' ') {
        e.preventDefault(); const at = ta.selectionStart;
        const {items} = await api('/api/complete', {uid, prefix: ta.value.slice(0, at), suffix: ta.value.slice(ta.selectionEnd)});
        if (items && items.length) { ta.setRangeText(items[0], at, ta.selectionEnd, 'select'); fit(); } else toast('no suggestion');
      }
    };
    fit(); ta.focus(); ta.setSelectionRange(ta.value.length, ta.value.length);
  });
}
async function editVim(c, uid) {
  const box = el('div', 'term'); c.pre.replaceWith(box);
  const rows0 = Math.max(5, c.nlines + 1);
  const t = new Terminal({rows: rows0, fontSize: 13, fontFamily: 'ui-monospace,SFMono-Regular,Menlo,Consolas,monospace', cursorBlink: true, theme: {background: '#111111'}});
  const fit = new FitAddon.FitAddon(); t.loadAddon(fit); t.open(box);
  const cols = () => (fit.proposeDimensions() || {cols: 100}).cols;
  t.resize(cols(), rows0);
  const {id} = await api('/api/term/open', {uid, rows: rows0, cols: cols()});
  if (!(id > 0)) { t.dispose(); box.replaceWith(c.pre); toast('could not start vim; using the plain editor'); return editPlain(c, uid); }
  // vim reports its line count via OSC 777 (see web.cpp); grow or shrink the terminal to fit
  t.parser.registerOscHandler(777, data => {
    const m = /lines=(\d+)/.exec(data);
    if (m) { const rows = Math.max(5, Math.min(60, +m[1] + 1)); if (rows !== t.rows) { t.resize(cols(), rows); api('/api/term/size', {id, rows, cols: cols()}); } }
    return true;
  });
  let q = Promise.resolve();
  t.onData(d => { q = q.then(() => fetch('/api/term/in?id=' + id, {method: 'POST', headers: {'X-Token': TOKEN}, body: d})); });
  t.focus();
  const r = await fetch('/api/term/out?id=' + id + '&token=' + TOKEN);
  const rd = r.body.getReader();
  for (;;) { const {done, value} = await rd.read(); if (done) break; t.write(value); }
  t.dispose(); focusUid = uid; ownWrite = Date.now();
  await load();
}
async function insertBelow(uid) {
  await api('/api/cell', {action: 'insert_after', uid, text: 'void(0);'});
  const [f, i] = [uid.slice(0, uid.lastIndexOf(':')), +uid.slice(uid.lastIndexOf(':') + 1)];
  const nu = f + ':' + (i + 1);
  await load(); focus(nu); await edit(nu);
  if (cells[nu] && cells[nu].text === 'void(0);') { await api('/api/cell', {action: 'delete', uid: nu}); load(); }
}
async function del(uid) { if (!confirm('Delete this cell from ' + uid.split(':')[0] + '?')) return; await api('/api/cell', {action: 'delete', uid}); load(); }

// ---------------------------------------------------------------- side panel
let sideMode = null;
async function refreshSide() {
  const s = $('#side'); s.textContent = '';
  if (sideMode === 'chat') return chatPanel(s);
  if (sideMode === 'vars') {
    s.append(el('h2', '', 'Variables'));
    const evs = await op('vars');
    const v = evs.find(e => e.ev === 'vars');
    if (!v || !v.vars.length) { s.append(el('div', 'vdim', 'nothing declared yet')); return; }
    for (const x of v.vars.slice().reverse()) { const n = nodeEl(x.node, x.name, 2); s.append(n); }
  } else if (sideMode === 'heap') {
    s.append(el('h2', '', 'Heap'));
    const evs = await op('heap'); const h = evs.find(e => e.ev === 'heap')?.heap; if (!h) return;
    const fmtB = b => b > 1048576 ? (b / 1048576).toFixed(1) + ' MB' : b > 1024 ? (b / 1024).toFixed(1) + ' KB' : b + ' B';
    s.insertAdjacentHTML('beforeend', '<div><b>' + fmtB(h.live) + '</b> live in ' + h.blocks + ' blocks <span class="vdim">· ' + h.allocs + ' allocs, ' + h.frees + ' frees, ' + fmtB(h.total) + ' total</span></div>');
    const pal = i => 'var(--s' + (i % 8 + 1) + ')';
    const bar = el('div', 'heapbar'), tot = h.cells.reduce((a, c) => a + c[1], 0) || 1;
    h.cells.forEach(([cid, b], i) => { const d = el('div'); d.style.cssText = 'flex:' + b / tot + ';background:' + pal(i); d.title = 'run ' + cid + ': ' + fmtB(b); bar.append(d); });
    s.append(el('div', 'vdim', 'live bytes by run (hover)'), bar);
    const cv = el('canvas'); cv.id = 'heapmap'; s.append(el('div', 'vdim', 'heap map: blocks in address order, width ∝ size, colour = run'), cv);
    requestAnimationFrame(() => {
      const W = cv.width = cv.clientWidth * devicePixelRatio, H = cv.height = 90 * devicePixelRatio, g = cv.getContext('2d');
      const rows = 6, rh = H / rows, total = h.map.reduce((a, b) => a + Math.max(b[1], 64), 0) || 1; let x = 0;
      const idx = new Map(h.cells.map((c, i) => [c[0], i]));
      for (const [p, sz, cid] of h.map) {
        let w = Math.max(sz, 64) / total * W * rows;
        while (w > 0) { const row = Math.floor(x / W), xx = x % W, ww = Math.min(w, W - xx); g.fillStyle = getComputedStyle(document.body).getPropertyValue('--s' + ((idx.get(cid) || 0) % 8 + 1)); g.fillRect(xx, row * rh + 1, Math.max(ww - .5, .5), rh - 2); x += ww; w -= ww; }
      }
    });
    const t = el('table', 'lay'); t.innerHTML = '<tr><th>bytes</th><th>blocks</th><th>allocated at</th></tr>' + h.sites.map(x => '<tr><td class="num">' + fmtB(x.bytes) + '</td><td class="num">' + x.n + '</td><td>' + esc(x.site) + '</td></tr>').join('');
    s.append(el('h2', '', 'Top allocation sites'), t);
    const lb = el('button', '', 'Check for leaks (unreachable blocks)');
    lb.onclick = async () => {
      const evs = await op('leaks'); const L = evs.find(e => e.ev === 'leaks')?.leaks; if (!L) return;
      const d = el('div'); d.innerHTML = '<p><b style="color:' + (L.bytes ? 'var(--err)' : 'var(--ok)') + '">definitely lost: ' + fmtB(L.bytes) + ' in ' + L.blocks + ' blocks</b></p>' +
        (L.sites.length ? '<table class="lay"><tr><th>bytes</th><th>blocks</th><th>allocated at</th></tr>' + L.sites.map(x => '<tr><td class="num">' + fmtB(x.bytes) + '</td><td class="num">' + x.n + '</td><td>' + esc(x.site) + '</td></tr>').join('') + '</table>' : '');
      lb.replaceWith(d);
    };
    s.append(el('h2', '', 'Leaks'), lb);
  }
}
// ---------------------------------------------------------------- assistant
let chatMsgs = JSON.parse(sessionStorage.getItem('zeno.chat') || '[]'), chatFocus = null;
function chatBody(text) {   // markdown, with ```cpp replace=uid / after=uid blocks turned into one-click edits
  const d = el('div'); let last = 0, m;
  const re = /```([^\n]*)\n([\s\S]*?)(```|$)/g;
  while ((m = re.exec(text))) {
    d.insertAdjacentHTML('beforeend', markdown(text.slice(last, m.index)));
    const info = m[1].trim(), code = m[2].replace(/\n$/, ''), pre = el('pre', 'code'); pre.innerHTML = hl(code, []);
    d.append(pre);
    const t = /(replace|after)=\[?([^\s\]]+)\]?/.exec(info);
    if (t && m[3]) {
      const b = el('button', 'apply', t[1] === 'replace' ? 'Apply to cell ' + t[2] : 'Insert after cell ' + t[2]);
      b.onclick = async () => {
        const r = await api('/api/cell', {action: t[1] === 'replace' ? 'replace' : 'insert_after', uid: t[2], text: code});
        if (r.ok) { b.textContent = 'applied ✓'; b.disabled = true; await load(); focus(t[2]); showCell(t[2]); } else toast(r.error || 'could not apply');
      };
      d.append(b);
    }
    last = re.lastIndex;
  }
  d.insertAdjacentHTML('beforeend', markdown(text.slice(last)));
  return d;
}
async function chatPanel(s) {
  const cfg = await api('/api/config');
  const wrap = el('div', 'chat'), head = el('div', 'row'), msgs = el('div', 'msgs'), inp = el('div', 'chatin');
  head.innerHTML = '<h2 style="margin:0">Assistant</h2>';
  const model = el('span', 'vdim', ' ' + (cfg.url ? (cfg.model || 'model') + ' @ ' + cfg.url.replace(/^https?:\/\//, '') : 'no model configured')); head.append(model);
  const gear = el('button', 'vbtn', 'settings'); gear.style.marginLeft = '8px'; head.append(gear);
  const clear = el('button', 'vbtn', 'clear'); head.append(clear);
  const set = el('div', 'settings'); set.style.display = cfg.url ? 'none' : 'grid';
  set.innerHTML = '<div class="vdim">Any OpenAI-compatible server: llama.cpp, Ollama (http://host:11434/v1), vLLM, LM Studio, OpenRouter, OpenAI…</div>' +
    '<label>Base URL<input id="cfg-url" placeholder="http://localhost:8080/v1"></label><label>Model<input id="cfg-model" placeholder="model name"></label>' +
    '<label>API key (optional)<input id="cfg-key" type="password" placeholder="' + (cfg.has_key ? '(saved)' : '') + '"></label><button class="pri" id="cfg-save">Save</button>';
  gear.onclick = () => set.style.display = set.style.display === 'none' ? 'grid' : 'none';
  clear.onclick = () => { chatMsgs = []; sessionStorage.removeItem('zeno.chat'); refreshSide(); };
  wrap.append(head, set, msgs, inp);
  s.append(wrap);
  $('#cfg-url').value = cfg.url; $('#cfg-model').value = cfg.model;
  $('#cfg-save').onclick = async () => { const b = {url: $('#cfg-url').value.trim(), model: $('#cfg-model').value.trim()}; if ($('#cfg-key').value) b.key = $('#cfg-key').value; await api('/api/config', b); refreshSide(); };
  const show = m => { const d = el('div', 'msg ' + m.role); d.append(m.role === 'user' ? el('div', '', m.content) : chatBody(m.content)); msgs.append(d); return d; };
  chatMsgs.forEach(show);
  const ta = el('textarea'); ta.placeholder = 'Ask about the notebook, or ask for a change… (Enter to send, Shift+Enter for a newline)';
  const row = el('div', 'row'), ctxl = el('span', '', chatFocus ? 'about cell [' + (cells[chatFocus]?.no || chatFocus) + ']' : 'about the whole notebook');
  row.append(ctxl);
  inp.append(ta, row);
  msgs.scrollTop = msgs.scrollHeight;
  ta.focus();
  ta.onkeydown = async e => {
    if (e.key !== 'Enter' || e.shiftKey) return;
    e.preventDefault();
    const q = ta.value.trim(); if (!q) return;
    ta.value = '';
    chatMsgs.push({role: 'user', content: q}); show(chatMsgs[chatMsgs.length - 1]);
    const a = {role: 'assistant', content: ''}, d = show(a);
    await stream('/api/chat', {messages: chatMsgs, focus: chatFocus || focusUid || ''}, ev => {
      if (ev.ev === 'thinking' && !a.content) { d.textContent = 'thinking…'; d.classList.add('vdim'); return; }
      if (ev.ev !== 'delta') return;
      d.classList.remove('vdim');
      a.content += ev.s; d.textContent = ''; d.append(chatBody(a.content)); msgs.scrollTop = msgs.scrollHeight;
    });
    chatMsgs.push(a); sessionStorage.setItem('zeno.chat', JSON.stringify(chatMsgs.slice(-40)));
  };
}
function askAbout(uid) { chatFocus = uid; sideMode = 'chat'; $('#side').classList.add('open'); refreshSide(); }

function side(mode) {
  const s = $('#side');
  if (sideMode === mode && s.classList.contains('open')) { s.classList.remove('open'); sideMode = null; return; }
  sideMode = mode; s.classList.add('open'); refreshSide();
}
$('#tvars').onclick = () => side('vars');
$('#teditor').onclick = () => { editor = editor === 'vim' ? 'plain' : 'vim'; localStorage.setItem('zeno.editor', editor); showEditor(); toast(editor === 'vim' ? 'cells open in vim (:wq saves)' : 'cells open in the plain editor'); };
showEditor();
$('#tchat').onclick = () => { chatFocus = null; side('chat'); };
$('#theap').onclick = () => side('heap');
$('#runall').onclick = async () => { await api('/api/restart'); Object.values(outs).forEach(o => o.textContent = ''); run('', 'group'); };
$('#sync').onclick = () => sync('');
$('#intr').onclick = () => api('/api/interrupt');
$('#undo').onclick = async () => { const evs = await op('undo'); const d = evs.find(e => e.ev === 'done'); toast(d && d.ok ? 'rolled back the last run' : (d?.why || 'nothing to undo')); markDirty(); if (sideMode) refreshSide(); };
$('#restart').onclick = async () => { await api('/api/restart'); Object.values(outs).forEach(o => o.textContent = ''); toast('kernel restarted'); markDirty(); };
$('#extract').onclick = async () => {
  const uids = order.filter(u => selected.has(u));
  const name = prompt('Move ' + uids.length + ' cell(s) into which file (relative to the first cell\'s folder)? They will be replaced by an #include.', 'part.hpp');
  if (!name) return;
  const r = await api('/api/extract', {uids, file: name});
  if (!r.ok) { toast('could not move cells'); return; }
  [...selected].forEach(toggleSel); load();
};
// Hot reload: the server long-polls for source changes made outside the page (an editor, a coding agent, git).
fetch('/api/watch?v=-1&token=' + TOKEN).then(r => r.json()).then(j => { watchStart(j.v); });
function watchStart(v0) {
  (async () => {
    let v = v0;
    for (;;) {
      try {
        const j = await (await fetch('/api/watch?v=' + v + '&token=' + TOKEN)).json();
        if (j.v !== v) {
          v = j.v;
          if (editing) reloadPending = true;
          else { const y = scrollY; await load(); scrollTo(0, y); if (Date.now() - ownWrite > 2000) toast('files changed on disk — reloaded'); }
        }
      } catch (e) { await new Promise(r => setTimeout(r, 2000)); }
    }
  })();
}
// Runs started by other clients (coding agents via `zeno mcp`, other tabs) show up here too.
(async function follow() {
  let since = 0, h = null;
  for (;;) {
    try {
      await stream0('/api/events?since=' + since + '&token=' + TOKEN, e => {
        if (e.ev === 'hello') { since = e.seq; return; }
        if (e.ev === 'rollback') { toast(e.restart ? 'restarted the kernel to keep state consistent' : 'rolled back ' + e.undo + ' run(s) to keep state consistent'); return; }
        if (e.ev === 'ping' || e.origin === ME) return;
        if (e.ev === 'batch') { h = runHandler(); toast('running ' + e.uids.length + ' cell(s) for ' + e.origin); }
        if (h) h(e);
        if (e.ev === 'done') markDirty();
      });
    } catch (err) {}
    await new Promise(r => setTimeout(r, 1500));
  }
})();
async function stream0(url, on) {   // GET stream without the busy indicator
  const r = await fetch(url), rd = r.body.getReader(), dec = new TextDecoder(); let buf = '';
  for (;;) { const {done, value} = await rd.read(); if (done) return; buf += dec.decode(value, {stream: true}); let i; while ((i = buf.indexOf('\n')) >= 0) { const l = buf.slice(0, i); buf = buf.slice(i + 1); if (l) on(JSON.parse(l)); } }
}
load().then(() => { if (new URLSearchParams(location.search).get('run') === 'all') sync(''); });
</script></body></html>)zenoui";
