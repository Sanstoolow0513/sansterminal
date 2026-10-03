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
  // Keep all model switching and edits in a separate editor. Worker requests
  // can take time, during which the visible editor remains fully interactive.
  const count = monaco.editor.getModels().length;
  const testModels = new Map();
  const container = document.createElement('div');
  container.style.cssText = 'position:absolute;left:-10000px;top:0;width:640px;height:480px';
  container.setAttribute('aria-hidden', 'true');
  container.inert = true;
  let testEditor;
  let probe;
  try {
    for (const [name, model] of models) {
      testModels.set(name, monaco.editor.createModel(model.getValue(), name, monaco.Uri.parse(`inmemory://probe/self-test/${name === 'json' ? 'settings.json' : 'main.ts'}`)));
    }
    const saved = [...testModels].map(([name, model]) => [name, model.getValue(), model.getAlternativeVersionId()]);
    probe = monaco.editor.createModel('original\n', 'plaintext', monaco.Uri.parse('inmemory://probe/self-test/scratch'));
    document.body.append(container);
    testEditor = monaco.editor.create(container, {
      model: probe, dimension: { width: 640, height: 480 }, theme: 'vs-dark', minimap: { enabled: false },
    });
    testEditor.executeEdits('probe', [{ range: new monaco.Range(1, 1, 1, 1), text: 'edited ', forceMoveMarkers: true }]);
    testEditor.pushUndoStop();
    testEditor.setPosition({ lineNumber: 1, column: 5 });
    const view = testEditor.saveViewState();
    for (let i = 0; i < 100; ++i) testEditor.setModel(testModels.get(i % 2 ? 'json' : 'typescript'));
    testEditor.setModel(probe);
    testEditor.restoreViewState(view);
    if (probe.getValue() !== 'edited original\n' || testEditor.getPosition().column !== 5) throw Error('model/view state lost');
    await probe.undo();
    if (probe.getValue() !== 'original\n') throw Error('undo lost');
    await probe.redo();
    if (probe.getValue() !== 'edited original\n') throw Error('redo lost');
    for (const [name, value, version] of saved) {
      if (testModels.get(name).getValue() !== value || testModels.get(name).getAlternativeVersionId() !== version) throw Error('test buffer changed');
    }
    const jsonWorker = await getJsonWorker();
    const json = await jsonWorker(testModels.get('json').uri);
    await json.doValidation(testModels.get('json').uri.toString());
    const tsWorker = await getTypeScriptWorker();
    const ts = await tsWorker(testModels.get('typescript').uri);
    await ts.getSyntacticDiagnostics(testModels.get('typescript').uri.toString());
    if (!workers.has('json') || !workers.has('ts')) throw Error('language workers missing');
    report('smoke-ok: 100 switches; text/view/undo/redo preserved; JSON/TS workers responded');
  } catch (error) {
    report(`smoke-failed: ${String(error)}`);
  } finally {
    testEditor?.dispose();
    container.remove();
    probe?.dispose();
    for (const model of testModels.values()) model.dispose();
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
