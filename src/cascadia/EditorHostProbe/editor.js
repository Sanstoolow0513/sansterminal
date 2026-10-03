import * as monaco from 'monaco-editor/editor/editor.main.js';
import { getWorker as getJsonWorker } from 'monaco-editor/language/json/monaco.contribution.js';
import { getTypeScriptWorker } from 'monaco-editor/language/typescript/monaco.contribution.js';
import { createWorkspace, createCommandRegistry, registerBundledCommands, protocolVersion } from './workspace.js';

const bridge = window.chrome.webview;
const workspaceMode = new URLSearchParams(window.location?.search ?? '').get('workspace') === '1';
const diagnosticMode = !workspaceMode || new URLSearchParams(window.location?.search ?? '').get('diagnostics') === '1';
const send = (type, fields = {}) => bridge.postMessage(workspaceMode ? { version: protocolVersion, type, ...fields } : type);
const status = document.querySelector('#status');
const workspaceHint = '从终端工作区选择文件；Ctrl+S 保存，F6 返回终端。';
const report = (message) => {
  status.textContent = message;
  // Host diagnostics are capped at 1024 UTF-16 code units. Keep the full error
  // visible on the page, while retaining its prefix and useful detail in logs.
  let diagnostic = message.slice(0, 1024);
  if (/[\uD800-\uDBFF]$/.test(diagnostic)) diagnostic = diagnostic.slice(0, -1);
  if (workspaceMode) send('report', { message: diagnostic });
  else bridge.postMessage(diagnostic);
};
const workers = new Set();
self.MonacoEnvironment = {
  getWorker(_moduleId, label) {
    const name = label === 'json' ? 'json' : ['typescript', 'javascript'].includes(label) ? 'ts' :
      ['css', 'scss', 'less'].includes(label) ? 'css' : ['html', 'handlebars', 'razor'].includes(label) ? 'html' : 'editor';
    workers.add(name);
    return new Worker(new URL(`./${name}.worker.js`, import.meta.url), { type: 'module' });
  },
};

const models = new Map(workspaceMode ? [] : [
  ['json', monaco.editor.createModel('{\n  "project": "A",\n  "message": "中文输入法、复制粘贴与撤销测试"\n}\n', 'json', monaco.Uri.parse('inmemory://probe/A/settings.json'))],
  ['typescript', monaco.editor.createModel('const project: string = "B";\nconsole.log(project);\n', 'typescript', monaco.Uri.parse('inmemory://probe/B/main.ts'))],
]);
const views = new Map();
let active = workspaceMode ? null : 'json';
const editor = monaco.editor.create(document.querySelector('#editor'), {
  model: models.get(active) ?? null, automaticLayout: false, theme: 'vs-dark', minimap: { enabled: false },
  // Use the established textarea composition and accessibility path in the
  // native WebView2 host instead of the experimental browser EditContext API.
  editContext: false,
  ariaLabel: workspaceMode ? '工作区文件编辑器' : 'Monaco 宿主验证编辑器',
});
let commands;
const workspace = workspaceMode ? createWorkspace({
  monaco, editor, postMessage: (message) => bridge.postMessage(message),
  onState(document, dirty) {
    documentName.textContent = document ? `${document.path}${dirty ? ' • 未保存' : ''}${document.readOnly ? '（只读）' : ''}` : '等待选择文件';
    documentName.title = document?.reason ?? document?.path ?? '';
    documentName.setAttribute('aria-label', documentName.textContent);
    status.textContent = document?.error ?? document?.reason ?? workspaceHint;
    documentSave.disabled = !document || workspace.commandContext().readOnly;
    documentRetry.hidden = !document?.readOnly || !document?.reason;
    updateCommands();
  },
  onError: (message) => { status.textContent = message; },
  onTheme: (theme) => { document.body.dataset.theme = theme; },
}) : null;
const documentName = document.querySelector('#document-name');
const documentSave = document.querySelector('#save');
const documentRetry = document.querySelector('#retry-load');
const extensionButtons = [
  [document.querySelector('#uppercase'), 'selection.uppercase'],
  [document.querySelector('#timestamp'), 'insert.timestamp'],
];
function updateCommands() {
  for (const [button, id] of extensionButtons) button.disabled = !commands?.canRun(id);
}
document.querySelector('#scratch-tools').hidden = workspaceMode;
document.querySelector('#workspace-tools').hidden = !workspaceMode;
for (const id of ['check', 'dialog', 'uppercase', 'timestamp']) document.querySelector(`#${id}`).hidden = !diagnosticMode;
document.querySelector('#toolbar').setAttribute('aria-label', workspaceMode ? '文件编辑器' : '验证工具');
if (workspace) {
  commands = createCommandRegistry(() => workspace.commandContext());
  registerBundledCommands(commands);
  for (const [button, id] of extensionButtons) button.onclick = () => commands.executeCommand(id);
  documentSave.onclick = () => workspace.save();
  documentRetry.onclick = () => {
    const document = workspace.getActive();
    if (document?.readOnly) send('retry-load', { id: document.id });
  };
  documentSave.disabled = true;
  updateCommands();
  status.textContent = workspaceHint;
}
const focusListener = editor.onDidFocusEditorText(() => { if (workspaceMode) send('focused'); });
const selectionListener = editor.onDidChangeCursorSelection(updateCommands);
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
  focusListener.dispose();
  selectionListener.dispose();
  workspace?.dispose();
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
document.querySelector('#terminal').onclick = () => send('focus-terminal');
document.querySelector('#dialog').onclick = () => send('show-dialog');
editor.addCommand(monaco.KeyCode.F6, () => send('focus-terminal'));
editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => workspace ? workspace.save() : report('scratch-only: saving is intentionally unavailable'));

let checking = false;
async function smoke() {
  if (checking) return;
  checking = true;
  // Keep all model switching and edits in a separate editor. Worker requests
  // can take time, during which the visible editor remains fully interactive.
  const testModels = new Map();
  const container = document.createElement('div');
  container.style.cssText = 'position:absolute;left:-10000px;top:0;width:640px;height:480px';
  container.setAttribute('aria-hidden', 'true');
  container.inert = true;
  let testEditor;
  let probe;
  try {
    const samples = workspaceMode ? [['json', '{"probe":true}\n'], ['typescript', 'const probe: string = "test";\n']] :
      [...models].map(([name, model]) => [name, model.getValue()]);
    for (const [name, text] of samples) {
      testModels.set(name, monaco.editor.createModel(text, name, monaco.Uri.parse(`inmemory://probe/self-test/${name === 'json' ? 'settings.json' : 'main.ts'}`)));
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
    const testCommands = createCommandRegistry(() => ({
      model: probe, selection: new monaco.Range(1, 8, 1, 16), readOnly: false,
      executeEdits(source, edits) {
        testEditor.pushUndoStop();
        const result = testEditor.executeEdits(source, edits);
        testEditor.pushUndoStop();
        return result;
      },
      focusTerminal() {},
    }));
    registerBundledCommands(testCommands);
    testCommands.executeCommand('selection.uppercase');
    if (probe.getValue() !== 'edited ORIGINAL\n') throw Error('bundled command failed');
    await probe.undo();
    if (probe.getValue() !== 'edited original\n') throw Error('bundled command undo lost');
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
    report('smoke-ok: 100 switches; text/view/undo/redo preserved; bundled command undo preserved; JSON/TS workers responded');
  } catch (error) {
    report(`smoke-failed: ${String(error)}`);
  } finally {
    testEditor?.dispose();
    container.remove();
    probe?.dispose();
    for (const model of testModels.values()) model.dispose();
    const liveModels = new Set(monaco.editor.getModels());
    if (liveModels.has(probe) || [...testModels.values()].some((model) => liveModels.has(model))) report('smoke-failed: model leak');
    checking = false;
  }
}
document.querySelector('#check').onclick = smoke;
bridge.addEventListener('message', ({ data }) => {
  if (data === 'run-smoke') smoke();
  else if (data === 'focus') editor.focus();
  else if (workspace) workspace.receive(data);
});
window.addEventListener('error', (event) => report(`page-error: ${event.message}`));
window.addEventListener('unhandledrejection', (event) => report(`page-error: ${String(event.reason)}`));
if (workspace) send('ready');
else {
  activate(active, false);
  report('ready');
}
if (diagnosticMode) smoke();
