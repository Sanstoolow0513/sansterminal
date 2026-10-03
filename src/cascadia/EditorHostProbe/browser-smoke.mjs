// Run the bundled Monaco code and real workers in an isolated headless Chrome.
// Native file/dialog behavior is covered separately by Test-WorkspaceEditor.ps1.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { mkdtemp, readFile, rm, access } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { basename, dirname, join, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';

const root = dirname(fileURLToPath(import.meta.url));
const candidates = [process.env.CHROME_PATH, join(process.env.ProgramFiles ?? 'C:\\Program Files', 'Google/Chrome/Application/chrome.exe'), join(process.env['ProgramFiles(x86)'] ?? 'C:\\Program Files (x86)', 'Microsoft/Edge/Application/msedge.exe')].filter(Boolean);
let chrome;
for (const candidate of candidates) {
  try { await access(candidate); chrome = candidate; break; } catch {}
}
if (!chrome) throw Error('Set CHROME_PATH to an installed Chromium browser executable.');

const profile = await mkdtemp(join(tmpdir(), 'sansterminal-editor-smoke-'));
const dist = resolve(root, 'dist');
const server = createServer(async (request, response) => {
  try {
    const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
    const path = resolve(dist, `.${pathname === '/' ? '/index.html' : pathname}`);
    if (!path.startsWith(dist + sep)) { response.writeHead(403); response.end(); return; }
    const content = await readFile(path);
    response.setHeader('Content-Type', path.endsWith('.js') ? 'text/javascript' : path.endsWith('.css') ? 'text/css' : path.endsWith('.html') ? 'text/html' : 'application/octet-stream');
    response.end(content);
  } catch { response.writeHead(404); response.end(); }
});
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
const browser = spawn(chrome, ['--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--remote-debugging-port=0', `--user-data-dir=${profile}`, 'about:blank'], { windowsHide: true, stdio: 'ignore' });
let socket;
let call;
try {
  let debugPort;
  const startupDeadline = Date.now() + 15000;
  while (!debugPort && Date.now() < startupDeadline) {
    try { debugPort = (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; } catch { await delay(50); }
  }
  if (!debugPort) throw Error('Chromium did not expose its isolated debugging port.');
  const pages = await (await fetch(`http://127.0.0.1:${debugPort}/json/list`)).json();
  socket = new WebSocket(pages.find((page) => page.type === 'page').webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
  let serial = 0;
  const pending = new Map();
  socket.addEventListener('message', ({ data }) => {
    const message = JSON.parse(data);
    const request = pending.get(message.id);
    if (!request) return;
    pending.delete(message.id);
    clearTimeout(request.timer);
    if (message.error) request.reject(Error(JSON.stringify(message.error)));
    else request.resolve(message.result);
  });
  call = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++serial;
    const timer = setTimeout(() => { pending.delete(id); reject(Error(`CDP timeout: ${method}`)); }, 10000);
    pending.set(id, { resolve, reject, timer });
    socket.send(JSON.stringify({ id, method, params }));
  });
  const evaluate = async (expression) => {
    const result = await call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  const waitFor = async (expression) => {
    const deadline = Date.now() + 20000;
    while (Date.now() < deadline) { if (await evaluate(expression)) return; await delay(50); }
    throw Error(`Timed out waiting for ${expression}`);
  };
  const host = (type, fields = {}) => evaluate(`window.__receive(${JSON.stringify({ version: 1, type, ...fields })})`);
  const click = (id) => evaluate(`document.getElementById(${JSON.stringify(id)}).click()`);
  const key = async (key, code, modifiers = 0) => {
    const windowsVirtualKeyCode = key === 'Control' ? 17 : key.toUpperCase().charCodeAt(0);
    await call('Input.dispatchKeyEvent', { type: 'keyDown', key, code, modifiers, windowsVirtualKeyCode });
    await call('Input.dispatchKeyEvent', { type: 'keyUp', key, code, modifiers, windowsVirtualKeyCode });
  };
  await call('Page.enable');
  await call('Runtime.enable');
  await call('Page.addScriptToEvaluateOnNewDocument', { source: `window.__messages=[];window.__workerUrls=[];window.__receive=()=>{};const NativeWorker=window.Worker;window.Worker=class extends NativeWorker{constructor(...args){window.__workerUrls.push(String(args[0]));super(...args)}};window.chrome={webview:{postMessage(message){window.__messages.push(message)},addEventListener(type,callback){window.__receive=data=>callback({data})}}};` });
  await call('Page.navigate', { url: `http://127.0.0.1:${server.address().port}/index.html?workspace=1&diagnostics=1` });
  await waitFor(`window.__messages?.some(message => message.type === 'report' && message.message.startsWith('smoke-'))`);
  const report = await evaluate(`window.__messages.find(message => message.type === 'report' && message.message.startsWith('smoke-')).message`);
  assert.match(report, /^smoke-ok:/);
  assert.equal(await evaluate(`document.getElementById('scratch-tools').hidden`), true);
  assert.equal(await evaluate(`document.getElementById('save').disabled`), true);

  await host('open', { id: 'first', workspaceId: 'test', path: 'C:\\test\\first.ts', text: 'original\n', readOnly: false });
  await host('activate', { id: 'first' });
  await host('focus');
  await call('Input.insertText', { text: 'edited ' });
  await waitFor(`window.__messages.some(message => message.type === 'changed' && message.text === ${JSON.stringify('edited original\n')})`);
  await key('a', 'KeyA', 2);
  await click('uppercase');
  await waitFor(`window.__messages.some(message => message.type === 'changed' && message.text === ${JSON.stringify('EDITED ORIGINAL\n')})`);
  await key('z', 'KeyZ', 2);
  await waitFor(`window.__messages.filter(message => message.type === 'changed').at(-1).text === ${JSON.stringify('edited original\n')}`);

  await host('save-active');
  const saved = await evaluate(`window.__messages.filter(message => message.type === 'save').at(-1)`);
  assert.equal(saved.text, 'edited original\n');
  await host('saved', { id: saved.id, revision: saved.revision });
  assert.equal(await evaluate(`document.getElementById('document-name').textContent.includes('未保存')`), false);
  await host('open', { id: 'second', workspaceId: 'test', path: 'C:\\test\\second.json', text: '{"second":true}\n', readOnly: false });
  await host('activate', { id: 'second' });
  await host('activate', { id: 'first' });
  await click('save');
  assert.equal(await evaluate(`window.__messages.filter(message => message.type === 'save').at(-1).text`), 'edited original\n');
  await host('flush', { requestId: 'close-test' });
  assert.equal(await evaluate(`window.__messages.at(-1).type`), 'flushed');
  assert.equal(await evaluate(`document.getElementById('save').disabled`), true);
  await host('resume');
  assert.equal(await evaluate(`document.getElementById('save').disabled`), false);
  for (const [language, text] of [['css', 'body { color: red; }\n'], ['html', '<main>example</main>\n']]) {
    await host('open', { id: language, workspaceId: 'test', path: `C:\\test\\sample.${language}`, text, readOnly: false });
    await host('activate', { id: language });
    await waitFor(`window.__workerUrls.some(url => url.endsWith('/${language}.worker.js'))`);
    await delay(500);
    await host('close', { id: language });
  }
  assert.deepEqual(await evaluate(`window.__messages.filter(message => message.type === 'report' && message.message.startsWith('page-error:'))`), []);
  await host('theme', { theme: 'vs' });
  assert.equal(await evaluate(`getComputedStyle(document.body).backgroundColor`), 'rgb(255, 255, 255)');
  await host('theme', { theme: 'vs-dark' });
  assert.equal(await evaluate(`getComputedStyle(document.body).backgroundColor`), 'rgb(30, 30, 30)');
  await host('activate', { id: 'first' });
  await host('open', { id: 'conflict', workspaceId: 'test', path: 'C:\\test\\conflict.txt', text: 'conflict\n', readOnly: false });
  await host('activate', { id: 'conflict' });
  await host('error', { id: 'conflict', message: 'The file changed on disk.' });
  assert.equal(await evaluate(`document.getElementById('status').textContent`), 'The file changed on disk.');
  await host('activate', { id: 'first' });
  assert.match(await evaluate(`document.getElementById('status').textContent`), /Ctrl\+S/);
  await host('activate', { id: 'conflict' });
  assert.equal(await evaluate(`document.getElementById('status').textContent`), 'The file changed on disk.');
  await host('close', { id: 'conflict' });
  await host('activate', { id: 'first' });
  assert.match(await evaluate(`document.getElementById('status').textContent`), /Ctrl\+S/);
  await host('error', { id: 'conflict', message: 'Late error for a closed document.' });
  assert.match(await evaluate(`document.getElementById('status').textContent`), /Ctrl\+S/);
  await host('close', { id: 'first' });
  assert.equal(await evaluate(`document.getElementById('save').disabled`), true);
  console.log(`browser-smoke-ok: real Monaco and JSON/TS/CSS/HTML workers; extension undo; model switch; save ack; flush/resume; light/dark themes; document-scoped errors; close; no page errors. ${report}`);
} finally {
  if (call && socket?.readyState === WebSocket.OPEN) { try { await call('Browser.close'); } catch {} }
  socket?.close();
  if (browser.exitCode === null) {
    await Promise.race([new Promise((resolve) => browser.once('exit', resolve)), delay(3000)]);
    if (browser.exitCode === null) browser.kill();
  }
  await new Promise((resolve) => server.close(resolve));
  if (dirname(profile) !== resolve(tmpdir()) || !basename(profile).startsWith('sansterminal-editor-smoke-')) throw Error('Unexpected temporary profile path.');
  await rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
}
