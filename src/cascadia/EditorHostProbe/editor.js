import * as monaco from 'monaco-editor/editor/editor.main.js';
import { getWorker as getJsonWorker } from 'monaco-editor/language/json/monaco.contribution.js';
import { getTypeScriptWorker } from 'monaco-editor/language/typescript/monaco.contribution.js';

const bridge = window.chrome.webview;
const status = document.querySelector('#status');
const report = (message) => {
  status.textContent = message;
  bridge.postMessage(message);
};
const workers = new Set();
self.MonacoEnvironment = {
  getWorker(_moduleId, label) {
    const name = label === 'json' ? 'json' : ['typescript', 'javascript'].includes(label) ? 'ts' : 'editor';
    workers.add(name);
    return new Worker(new URL(`./${name}.worker.js`, import.meta.url), { type: 'module' });
  },
};

const models = new Map([
  ['json', monaco.editor.createModel('{\n  "project": "A",\n  "message": "中文输入法、复制粘贴与撤销测试"\n}\n', 'json', monaco.Uri.parse('inmemory://probe/A/settings.json'))],
  ['typescript', monaco.editor.createModel('const project: string = "B";\nconsole.log(project);\n', 'typescript', monaco.Uri.parse('inmemory://probe/B/main.ts'))],
]);
const views = new Map();
let active = 'json';
const editor = monaco.editor.create(document.querySelector('#editor'), {
  model: models.get(active), automaticLayout: false, theme: 'vs-dark', minimap: { enabled: false },
});
// Defer writes until the next frame: resizing the native surface and the
// wrapping status line in one observer delivery can otherwise form a loop.
let layoutFrame = 0;
const observer = new ResizeObserver(() => {
  if (layoutFrame) return;
  layoutFrame = requestAnimationFrame(() => {
    layoutFrame = 0;
    editor.layout();
  });
});
observer.observe(document.querySelector('#editor'));
window.addEventListener('unload', () => {
  observer.disconnect();
  cancelAnimationFrame(layoutFrame);
  editor.dispose();
  for (const model of models.values()) model.dispose();
});
const activate = (name, focus = true) => {
  views.set(active, editor.saveViewState());
  active = name;
  editor.setModel(models.get(name));
  editor.restoreViewState(views.get(name) ?? null);
  for (const key of models.keys()) document.getElementById(key).setAttribute('aria-pressed', String(key === name));
  if (focus) editor.focus();
};
for (const key of models.keys()) document.getElementById(key).onclick = () => activate(key);
document.querySelector('#terminal').onclick = () => bridge.postMessage('focus-terminal');
document.querySelector('#dialog').onclick = () => bridge.postMessage('show-dialog');
editor.addCommand(monaco.KeyCode.F6, () => bridge.postMessage('focus-terminal'));
editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => report('scratch-only: saving is intentionally unavailable'));

let checking = false;
async function smoke() {
  if (checking) return;
  checking = true;
  // Use a disposable model so self-tests never change the interactive buffers.
  const previous = active;
  const previousView = editor.saveViewState();
  const count = monaco.editor.getModels().length;
  const probe = monaco.editor.createModel('original\n', 'plaintext', monaco.Uri.parse('inmemory://probe/self-test'));
  try {
    const saved = [...models].map(([name, model]) => [name, model.getValue(), model.getAlternativeVersionId()]);
    editor.setModel(probe);
    editor.executeEdits('probe', [{ range: new monaco.Range(1, 1, 1, 1), text: 'edited ', forceMoveMarkers: true }]);
    editor.pushUndoStop();
    editor.setPosition({ lineNumber: 1, column: 5 });
    const view = editor.saveViewState();
    for (let i = 0; i < 100; ++i) editor.setModel(models.get(i % 2 ? 'json' : 'typescript'));
    editor.setModel(probe);
    editor.restoreViewState(view);
    if (probe.getValue() !== 'edited original\n' || editor.getPosition().column !== 5) throw Error('model/view state lost');
    await probe.undo();
    if (probe.getValue() !== 'original\n') throw Error('undo lost');
    await probe.redo();
    if (probe.getValue() !== 'edited original\n') throw Error('redo lost');
    for (const [name, value, version] of saved) {
      if (models.get(name).getValue() !== value || models.get(name).getAlternativeVersionId() !== version) throw Error('interactive buffer changed');
    }
    const jsonWorker = await getJsonWorker();
    const json = await jsonWorker(models.get('json').uri);
    await json.doValidation(models.get('json').uri.toString());
    const tsWorker = await getTypeScriptWorker();
    const ts = await tsWorker(models.get('typescript').uri);
    await ts.getSyntacticDiagnostics(models.get('typescript').uri.toString());
    if (!workers.has('json') || !workers.has('ts')) throw Error('language workers missing');
    report('smoke-ok: 100 switches; text/view/undo/redo preserved; JSON/TS workers responded');
  } catch (error) {
    report(`smoke-failed: ${String(error)}`);
  } finally {
    editor.setModel(models.get(previous));
    editor.restoreViewState(previousView);
    probe.dispose();
    if (monaco.editor.getModels().length !== count) report('smoke-failed: model leak');
    checking = false;
  }
}
document.querySelector('#check').onclick = smoke;
bridge.addEventListener('message', ({ data }) => {
  if (data === 'run-smoke') smoke();
  else if (data === 'focus') editor.focus();
});
window.addEventListener('error', (event) => report(`page-error: ${event.message}`));
window.addEventListener('unhandledrejection', (event) => report(`page-error: ${String(event.reason)}`));
activate(active, false);
report('ready');
smoke();
