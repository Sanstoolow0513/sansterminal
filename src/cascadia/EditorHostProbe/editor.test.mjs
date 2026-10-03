import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import * as workspaceExports from './workspace.js';

const source = await readFile(new URL('./editor.js', import.meta.url), 'utf8');
const deferred = () => Promise.withResolvers();

async function startProbe(workspaceMode = false) {
  const liveModels = new Set();
  const editors = [];
  const elements = new Map();
  const messages = [];
  const validations = [];
  let workerGate = deferred();
  let workerEntered = deferred();
  let finished = deferred();
  let receive;
  const element = () => ({ style: {}, setAttribute() {}, remove() {} });
  const document = {
    querySelector: (selector) => document.getElementById(selector.slice(1)),
    getElementById: (id) => {
      if (!elements.has(id)) elements.set(id, element());
      return elements.get(id);
    },
    createElement: element,
    body: { dataset: {}, append() {} },
  };
  const monaco = {
    Uri: { parse: (value) => ({ toString: () => value }) },
    Range: class {
      constructor(startLineNumber, startColumn, endLineNumber, endColumn) { Object.assign(this, { startLineNumber, startColumn, endLineNumber, endColumn }); }
      isEmpty() { return this.startLineNumber === this.endLineNumber && this.startColumn === this.endColumn; }
    }, KeyCode: {}, KeyMod: {},
    editor: {
      EndOfLineSequence: { LF: 0, CRLF: 1 },
      setTheme() {},
      getModels: () => [...liveModels],
      createModel(value, language, uri) {
        const model = {
          uri, value, language, version: 1,
          listeners: new Set(),
          getValue() { return this.value; },
          getValueInRange(range) { return this.value.slice(range.startColumn - 1, range.endColumn - 1); },
          getAlternativeVersionId() { return this.version; },
          getVersionId() { return this.version; },
          setValue(text) { this.value = text; ++this.version; for (const listener of this.listeners) listener(); },
          setEOL(sequence) { this.setValue(this.value.replace(/\r?\n/g, sequence ? '\r\n' : '\n')); },
          onDidChangeContent(listener) { this.listeners.add(listener); return { dispose: () => this.listeners.delete(listener) }; },
          undo() { this.value = this.beforeEdit; },
          redo() { this.value = this.afterEdit; },
          dispose() { liveModels.delete(this); },
        };
        liveModels.add(model);
        return model;
      },
      create(container, { model }) {
        const editor = {
          model, position: { lineNumber: 1, column: 1 }, disposed: false,
          getModel() { return this.model; },
          setModel(next) { this.model = next; this.position = { lineNumber: 1, column: 1 }; },
          executeEdits(_source, edits) {
            this.model.beforeEdit = this.model.value;
            const { range, text } = edits[0];
            this.model.setValue(this.model.value.slice(0, range.startColumn - 1) + text + this.model.value.slice(range.endColumn - 1));
            this.model.afterEdit = this.model.value;
          },
          pushUndoStop() {},
          setPosition(position) { this.position = { ...position }; },
          getPosition() { return this.position; },
          getSelection() { return new monaco.Range(this.position.lineNumber, this.position.column, this.position.lineNumber, this.position.column); },
          updateOptions() {},
          saveViewState() { return { position: { ...this.position } }; },
          restoreViewState(view) { if (view) this.position = { ...view.position }; },
          focus() {}, addCommand() {}, layout() {},
          onDidFocusEditorText: () => ({ dispose() {} }),
          onDidChangeCursorSelection: () => ({ dispose() {} }),
          dispose() { this.disposed = true; },
        };
        editors.push(editor);
        return editor;
      },
    },
  };
  const bridge = {
    addEventListener(_type, callback) { receive = callback; },
    postMessage(message) {
      messages.push(message);
      const report = typeof message === 'string' ? message : message.type === 'report' ? message.message : '';
      if (report.startsWith('smoke-')) finished.resolve(report);
    },
  };
  const context = vm.createContext({
    document, window: { chrome: { webview: bridge }, location: { search: workspaceMode ? '?workspace=1' : '' }, addEventListener() {} }, self: {}, URL, URLSearchParams,
    Worker: class { constructor(url) { this.url = url; } }, ResizeObserver: class { observe() {} disconnect() {} },
    requestAnimationFrame() {}, cancelAnimationFrame() {},
  });
  const dependencies = new Map([
    ['./workspace.js', workspaceExports],
    ['monaco-editor/editor/editor.main.js', monaco],
    ['monaco-editor/language/json/monaco.contribution.js', {
      async getWorker() {
        context.self.MonacoEnvironment.getWorker('', 'json');
        workerEntered.resolve();
        await workerGate.promise;
        return async () => ({ doValidation: async (uri) => validations.push(uri) });
      },
    }],
    ['monaco-editor/language/typescript/monaco.contribution.js', {
      async getTypeScriptWorker() {
        context.self.MonacoEnvironment.getWorker('', 'typescript');
        return async () => ({ getSyntacticDiagnostics: async (uri) => validations.push(uri) });
      },
    }],
  ]);
  const module = new vm.SourceTextModule(`${source}\nexport { editor, models, active, views, smoke, workspace, report };`, {
    context, initializeImportMeta(meta) { meta.url = new URL('./editor.js', import.meta.url).href; },
  });
  await module.link((specifier) => {
    const exports = dependencies.get(specifier);
    return new vm.SyntheticModule(Object.keys(exports), function () {
      for (const [key, value] of Object.entries(exports)) this.setExport(key, value);
    }, { context });
  });
  await module.evaluate();
  return {
    app: module.namespace, document, editors, liveModels, messages, validations,
    environment: context.self.MonacoEnvironment,
    entered: () => workerEntered.promise,
    release: () => workerGate.resolve(),
    fail: () => workerGate.reject(Error('worker unavailable')),
    finished: () => finished.promise,
    receive: (data) => receive({ data }),
    reset() { workerGate = deferred(); workerEntered = deferred(); finished = deferred(); },
  };
}

test('startup and manual self-tests preserve editing, document identity and view state during worker waits', async () => {
  const harness = await startProbe();
  for (const manual of [false, true]) {
    if (manual) {
      harness.reset();
      harness.document.getElementById('check').onclick();
    }
    await harness.entered();
    const { app } = harness;
    const json = app.models.get('json');
    assert.equal(app.editor.getModel(), json);
    json.setValue(`user edit ${manual}\n`);
    app.editor.setPosition({ lineNumber: 1, column: 7 });
    harness.document.getElementById('typescript').onclick();
    app.editor.setPosition({ lineNumber: 2, column: 3 });
    await app.smoke(); // A concurrent request must not create another test editor.
    assert.equal(harness.editors.filter((editor) => !editor.disposed).length, 2);
    harness.release();
    assert.match(await harness.finished(), /^smoke-ok:/);
    assert.equal(app.active, 'typescript');
    assert.equal(app.editor.getModel(), app.models.get('typescript'));
    assert.equal(app.editor.getPosition().lineNumber, 2);
    assert.equal(harness.liveModels.size, 2);
    assert.equal(harness.editors.filter((editor) => !editor.disposed).length, 1);
    harness.document.getElementById('json').onclick();
    assert.equal(app.editor.getModel(), json);
    assert.equal(json.getValue(), `user edit ${manual}\n`);
    assert.equal(app.editor.getPosition().column, 7);
  }
  assert.equal(harness.validations.length, 4);
  assert.ok(harness.validations.every((uri) => uri.includes('/self-test/')));
});

test('worker failures clean up the test models without changing the active document', async () => {
  const harness = await startProbe();
  await harness.entered();
  harness.document.getElementById('typescript').onclick();
  const model = harness.app.editor.getModel();
  model.setValue('typing while the worker fails');
  harness.fail();
  assert.match(await harness.finished(), /^smoke-failed:.*worker unavailable/);
  assert.equal(harness.app.active, 'typescript');
  assert.equal(harness.app.editor.getModel(), model);
  assert.equal(model.getValue(), 'typing while the worker fails');
  assert.equal(harness.liveModels.size, 2);
  assert.equal(harness.editors.filter((editor) => !editor.disposed).length, 1);
});

test('production workspace starts without scratch buffers or automatic self-tests', async () => {
  const harness = await startProbe(true);
  assert.equal(harness.app.models.size, 0);
  assert.equal(harness.app.editor.getModel(), null);
  assert.equal(harness.document.getElementById('scratch-tools').hidden, true);
  assert.equal(harness.document.getElementById('workspace-tools').hidden, false);
  assert.equal(harness.document.getElementById('save').disabled, true);
  assert.ok(harness.messages.some((message) => message.type === 'ready' && message.version === 1));
  assert.equal(harness.liveModels.size, 0);
  assert.equal(harness.editors.length, 1);
  assert.equal(harness.validations.length, 0);
  for (const id of ['check', 'dialog', 'uppercase', 'timestamp']) assert.equal(harness.document.getElementById(id).hidden, true);
  assert.ok(harness.messages.every((message) => typeof message === 'object' && message.version === 1));
});

test('opening a workspace file while smoke awaits workers preserves the document and does not report a leak', async () => {
  const harness = await startProbe(true);
  harness.app.smoke();
  await harness.entered();
  harness.receive({ version: 1, type: 'open', id: 'file', workspaceId: 'workspace', path: 'C:\\project\\file.ts', text: 'const x = 1;\n', readOnly: false });
  harness.receive({ version: 1, type: 'activate', id: 'file' });
  const model = harness.app.editor.getModel();
  model.setValue('typing during worker request');
  harness.release();
  assert.match(await harness.finished(), /^smoke-ok:/);
  assert.equal(harness.liveModels.size, 1);
  assert.equal(harness.app.editor.getModel(), model);
  assert.equal(model.getValue(), 'typing during worker request');
  assert.ok(!harness.messages.some((message) => message.message === 'smoke-failed: model leak'));
});

test('workers route CSS and HTML variants correctly and long diagnostics remain loggable', async () => {
  const harness = await startProbe(true);
  for (const label of ['css', 'scss', 'less']) assert.ok(harness.environment.getWorker('', label).url.pathname.endsWith('/css.worker.js'));
  for (const label of ['html', 'handlebars', 'razor']) assert.ok(harness.environment.getWorker('', label).url.pathname.endsWith('/html.worker.js'));
  const message = `page-error: ${'x'.repeat(2000)}`;
  harness.app.report(message);
  assert.equal(harness.messages.at(-1).message.length, 1024);
  assert.equal(harness.document.getElementById('status').textContent, message);
  harness.receive({ version: 1, type: 'theme', theme: 'vs' });
  assert.equal(harness.document.body.dataset.theme, 'vs');
});

test('read failures offer a retry for the known document without allowing arbitrary file access', async () => {
  const harness = await startProbe(true);
  harness.receive({ version: 1, type: 'open', id: 'unavailable', workspaceId: 'workspace', path: 'C:\\project\\missing.txt', text: 'Unable to read the file.', readOnly: true, reason: 'Unable to read the file.' });
  harness.receive({ version: 1, type: 'activate', id: 'unavailable' });
  const retry = harness.document.getElementById('retry-load');
  assert.equal(retry.hidden, false);
  assert.equal(harness.document.getElementById('save').disabled, true);
  retry.onclick();
  assert.deepEqual({ ...harness.messages.at(-1) }, { version: 1, type: 'retry-load', id: 'unavailable' });
  harness.receive({ version: 1, type: 'open', id: 'editable', workspaceId: 'workspace', path: 'C:\\project\\file.txt', text: 'text', readOnly: false });
  harness.receive({ version: 1, type: 'activate', id: 'editable' });
  assert.equal(retry.hidden, true);
  const count = harness.messages.length;
  retry.onclick();
  assert.equal(harness.messages.length, count);
});
