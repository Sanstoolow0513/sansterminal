import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const source = await readFile(new URL('./editor.js', import.meta.url), 'utf8');
const deferred = () => Promise.withResolvers();

async function startProbe() {
  const liveModels = new Set();
  const editors = [];
  const elements = new Map();
  const messages = [];
  const validations = [];
  let workerGate = deferred();
  let workerEntered = deferred();
  let finished = deferred();
  const element = () => ({ style: {}, setAttribute() {}, remove() {} });
  const document = {
    querySelector: (selector) => document.getElementById(selector.slice(1)),
    getElementById: (id) => {
      if (!elements.has(id)) elements.set(id, element());
      return elements.get(id);
    },
    createElement: element,
    body: { append() {} },
  };
  const monaco = {
    Uri: { parse: (value) => ({ toString: () => value }) },
    Range: class {}, KeyCode: {}, KeyMod: {},
    editor: {
      getModels: () => [...liveModels],
      createModel(value, language, uri) {
        const model = {
          uri, value, language, version: 1,
          getValue() { return this.value; },
          getAlternativeVersionId() { return this.version; },
          setValue(text) { this.value = text; ++this.version; },
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
            this.model.setValue(edits[0].text + this.model.value);
            this.model.afterEdit = this.model.value;
          },
          pushUndoStop() {},
          setPosition(position) { this.position = { ...position }; },
          getPosition() { return this.position; },
          saveViewState() { return { position: { ...this.position } }; },
          restoreViewState(view) { if (view) this.position = { ...view.position }; },
          focus() {}, addCommand() {}, layout() {},
          dispose() { this.disposed = true; },
        };
        editors.push(editor);
        return editor;
      },
    },
  };
  const bridge = {
    addEventListener() {},
    postMessage(message) {
      messages.push(message);
      if (message.startsWith('smoke-')) finished.resolve(message);
    },
  };
  const context = vm.createContext({
    document, window: { chrome: { webview: bridge }, addEventListener() {} }, self: {}, URL,
    Worker: class {}, ResizeObserver: class { observe() {} disconnect() {} },
    requestAnimationFrame() {}, cancelAnimationFrame() {},
  });
  const dependencies = new Map([
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
  const module = new vm.SourceTextModule(`${source}\nexport { editor, models, active, views, smoke };`, {
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
    entered: () => workerEntered.promise,
    release: () => workerGate.resolve(),
    fail: () => workerGate.reject(Error('worker unavailable')),
    finished: () => finished.promise,
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
