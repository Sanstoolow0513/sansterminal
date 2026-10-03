import assert from 'node:assert/strict';
import test from 'node:test';
import { createWorkspace, createCommandRegistry, registerBundledCommands, validateHostMessage, maxTextLength } from './workspace.js';

class Range {
  constructor(startLineNumber, startColumn, endLineNumber, endColumn) { Object.assign(this, { startLineNumber, startColumn, endLineNumber, endColumn }); }
  isEmpty() { return this.startLineNumber === this.endLineNumber && this.startColumn === this.endColumn; }
}

function fixture() {
  const liveModels = new Set();
  const messages = [];
  const states = [];
  const errors = [];
  const monaco = {
    Uri: { parse: (value) => ({ toString: () => value }) },
    editor: {
      EndOfLineSequence: { LF: 0, CRLF: 1 },
      createModel(text, _language, uri) {
        let value = text.replace(/\r\n/g, '\n');
        let eol = '\n';
        let revision = 1;
        let nextRevision = 1;
        let version = 1;
        let history = [{ value, revision }];
        let index = 0;
        const listeners = new Set();
        const notify = () => { ++version; for (const listener of listeners) listener(); };
        const restore = () => { ({ value, revision } = history[index]); notify(); };
        const offset = (line, column) => value.split(eol).slice(0, line - 1).reduce((length, part) => length + part.length + eol.length, 0) + column - 1;
        const model = {
          uri, disposed: false, listeners,
          getValue: () => value,
          getAlternativeVersionId: () => revision,
          getVersionId: () => version,
          setEOL(sequence) {
            eol = sequence ? '\r\n' : '\n';
            value = value.replace(/\r?\n/g, eol);
            revision = ++nextRevision;
            ++version;
            history = [{ value, revision }];
            index = 0;
          },
          getValueInRange(range) { return value.slice(offset(range.startLineNumber, range.startColumn), offset(range.endLineNumber, range.endColumn)); },
          edit(edits) {
            for (const { range, text } of [...edits].reverse()) value = value.slice(0, offset(range.startLineNumber, range.startColumn)) + text + value.slice(offset(range.endLineNumber, range.endColumn));
            revision = ++nextRevision;
            history.splice(++index, history.length, { value, revision });
            notify();
          },
          undo() { if (index > 0) { --index; restore(); } },
          redo() { if (index + 1 < history.length) { ++index; restore(); } },
          onDidChangeContent(listener) { listeners.add(listener); return { dispose: () => listeners.delete(listener) }; },
          dispose() { this.disposed = true; liveModels.delete(this); },
        };
        liveModels.add(model);
        return model;
      },
      setTheme() {},
    },
  };
  const editor = {
    model: null, selection: new Range(1, 1, 1, 1), readOnly: false, focused: 0, undoStops: 0,
    getModel() { return this.model; },
    setModel(model) { this.model = model; this.selection = new Range(1, 1, 1, 1); },
    saveViewState() { return { selection: this.selection }; },
    restoreViewState(view) { if (view) this.selection = view.selection; },
    updateOptions({ readOnly }) { this.readOnly = readOnly; },
    getSelection() { return this.selection; },
    executeEdits(_source, edits) { this.model.edit(edits); return true; },
    pushUndoStop() { ++this.undoStops; },
    focus() { ++this.focused; },
  };
  const app = createWorkspace({ monaco, editor, postMessage: (message) => messages.push(message), onState: (document, dirty) => states.push({ id: document?.id, dirty, error: document?.error }), onError: (error) => errors.push(error) });
  const receive = (type, fields = {}) => app.receive({ version: 1, type, ...fields });
  const open = (id, options = {}) => {
    receive('open', { id, workspaceId: 'workspace', path: `C:\\project\\${id}.ts`, text: 'original\n', readOnly: false, ...options });
    receive('activate', { id });
    return editor.getModel();
  };
  const insert = (text) => editor.executeEdits('typing', [{ range: new Range(1, 1, 1, 1), text }]);
  return { app, editor, messages, states, errors, liveModels, receive, open, insert };
}

test('pending native selection detaches the previous buffer without losing its edits or view', () => {
  const f = fixture();
  const first = f.open('first');
  f.insert('edited ');
  const selection = new Range(1, 8, 1, 16);
  f.editor.selection = selection;
  const staleContext = f.app.commandContext();
  assert.equal(f.receive('deactivate'), true);
  assert.equal(f.editor.getModel(), null);
  assert.equal(f.app.getActive(), null);
  assert.equal(f.app.commandContext().readOnly, true);
  assert.equal(staleContext.executeEdits('stale', []), false);
  const saves = f.messages.filter(message => message.type === 'save').length;
  f.receive('focus');
  f.receive('save-active');
  assert.equal(f.app.save(), false);
  assert.equal(f.messages.filter(message => message.type === 'save').length, saves);
  // An unrelated read completion can open a model without selecting it.
  f.receive('open', { id: 'second', workspaceId: 'workspace', path: 'C:\\project\\second.ts', text: 'second', readOnly: false });
  assert.equal(f.editor.getModel(), null);
  f.receive('activate', { id: 'second' });
  assert.equal(f.editor.getModel().getValue(), 'second');
  f.receive('activate', { id: 'first' });
  assert.equal(f.editor.getModel(), first);
  assert.equal(f.editor.selection, selection);
  assert.equal(first.getValue(), 'edited original\n');
  first.undo();
  assert.equal(first.getValue(), 'original\n');
});

test('switching models preserves each buffer, undo history and selection; repeated open cannot overwrite edits', () => {
  const f = fixture();
  const first = f.open('first');
  f.insert('edited ');
  const selection = new Range(1, 8, 1, 16);
  f.editor.selection = selection;
  const second = f.open('second');
  f.insert('other ');
  f.receive('activate', { id: 'first' });
  assert.equal(f.editor.getModel(), first);
  assert.equal(f.editor.selection, selection);
  assert.equal(first.getValue(), 'edited original\n');
  f.receive('open', { id: 'first', workspaceId: 'workspace', path: 'C:\\project\\first.ts', text: 'stale host text', readOnly: false });
  assert.equal(first.getValue(), 'edited original\n');
  first.undo();
  assert.equal(first.getValue(), 'original\n');
  assert.equal(f.app.isDirty('first'), false);
  assert.equal(second.getValue(), 'other original\n');
  first.redo();
  assert.equal(f.app.isDirty('first'), true);
  assert.equal(f.messages.at(-1).text, first.getValue());
});

test('an asynchronous save acknowledges its snapshot, leaves later edits dirty and becomes clean after undo', () => {
  const f = fixture();
  const model = f.open('file');
  f.insert('save ');
  const savedRevision = model.getVersionId();
  const savedAlternativeRevision = model.getAlternativeVersionId();
  assert.equal(f.app.save(), true);
  assert.deepEqual(f.messages.at(-1), { version: 1, type: 'save', id: 'file', text: 'save original\n', revision: savedRevision });
  f.insert('later ');
  f.receive('saved', { id: 'file', revision: savedRevision });
  assert.equal(f.app.isDirty('file'), true);
  model.undo();
  assert.equal(model.getAlternativeVersionId(), savedAlternativeRevision);
  assert.ok(model.getVersionId() > savedRevision);
  assert.equal(f.app.isDirty('file'), false);
  assert.equal(f.messages.at(-1).dirty, false);
});

test('native save accelerator snapshots only the active editable document and respects close suspension', () => {
  const f = fixture();
  assert.equal(f.receive('save-active'), true);
  assert.equal(f.messages.length, 0);
  f.open('first');
  f.insert('first ');
  f.open('second');
  f.insert('active ');
  assert.equal(f.receive('save-active'), true);
  assert.deepEqual(f.messages.at(-1), { version: 1, type: 'save', id: 'second', text: 'active original\n', revision: f.editor.getModel().getVersionId() });
  f.receive('flush', { requestId: 'closing' });
  const suspendedCount = f.messages.length;
  f.receive('save-active');
  assert.equal(f.messages.length, suspendedCount);
  f.receive('resume');
  f.receive('save-active');
  assert.equal(f.messages.at(-1).type, 'save');
  f.open('readonly', { readOnly: true });
  const readonlyCount = f.messages.length;
  f.receive('save-active');
  assert.equal(f.messages.length, readonlyCount);
});

test('out-of-order save acknowledgements cannot replace a newer saved baseline', () => {
  const f = fixture();
  const model = f.open('file');
  f.insert('one ');
  f.app.save();
  const first = model.getVersionId();
  f.insert('two ');
  f.app.save();
  const second = model.getVersionId();
  f.receive('saved', { id: 'file', revision: second });
  f.receive('saved', { id: 'file', revision: first });
  assert.equal(f.app.isDirty('file'), false);
  model.undo();
  assert.equal(f.app.isDirty('file'), true);
});

test('native dialog saves may acknowledge the current revision but unrelated old revisions are ignored', () => {
  const f = fixture();
  const model = f.open('file', { dirty: true });
  const originalRevision = model.getVersionId();
  assert.equal(f.app.isDirty('file'), true);
  f.insert('new ');
  f.receive('saved', { id: 'file', revision: originalRevision });
  assert.equal(f.app.isDirty('file'), true);
  f.receive('saved', { id: 'file', revision: model.getVersionId() });
  assert.equal(f.app.isDirty('file'), false);
});

test('EOL follows source content or the native encoding hint, and restored buffers synchronize their initial revision', () => {
  const f = fixture();
  const model = f.open('windows', { text: 'one\ntwo\n', eol: 'crlf', dirty: true });
  assert.equal(model.getValue(), 'one\r\ntwo\r\n');
  assert.equal(f.messages[0].revision, model.getVersionId());
  assert.equal(f.messages[0].dirty, true);
  assert.equal(f.open('detected', { text: 'one\r\ntwo\r\n' }).getValue(), 'one\r\ntwo\r\n');
  assert.equal(f.open('unix', { text: 'one\ntwo\n' }).getValue(), 'one\ntwo\n');
});

test('closing disposes model, listener and views without switching another buffer', () => {
  const f = fixture();
  const first = f.open('first');
  const second = f.open('second');
  f.receive('close', { id: 'first' });
  assert.equal(first.disposed, true);
  assert.equal(first.listeners.size, 0);
  assert.equal(f.editor.getModel(), second);
  const count = f.messages.length;
  first.edit([{ range: new Range(1, 1, 1, 1), text: 'disposed ' }]);
  assert.equal(f.messages.length, count);
  f.receive('close', { id: 'second' });
  assert.equal(f.editor.getModel(), null);
  assert.equal(f.app.getActive(), null);
  assert.equal(f.liveModels.size, 0);
  f.app.dispose();
});

test('read-only documents prohibit saving and extension edits, including stale contexts after switching', () => {
  const f = fixture();
  const first = f.open('first');
  const context = f.app.commandContext();
  const model = f.open('readonly', { readOnly: true, reason: 'unsupported encoding' });
  assert.equal(f.editor.readOnly, true);
  assert.equal(f.app.save(), false);
  assert.equal(f.app.commandContext().executeEdits('extension', []), false);
  assert.equal(context.executeEdits('stale', []), false);
  assert.equal(model.getValue(), 'original\n');
  assert.equal(first.getValue(), 'original\n');
});

test('flush sends each latest buffer before its acknowledgement without moving focus or selection', () => {
  const f = fixture();
  f.open('first');
  f.insert('first ');
  f.open('second', { readOnly: true });
  const selection = new Range(1, 2, 1, 4);
  f.editor.selection = selection;
  f.messages.length = 0;
  assert.equal(f.receive('flush', { requestId: 'close-1' }), true);
  assert.deepEqual(f.messages.map((message) => message.type), ['changed', 'changed', 'flushed']);
  assert.equal(f.messages[0].text, 'first original\n');
  assert.equal(f.messages[1].id, 'second');
  assert.deepEqual(f.messages[2], { version: 1, type: 'flushed', requestId: 'close-1' });
  assert.equal(f.app.getActive().id, 'second');
  assert.equal(f.editor.selection, selection);
  assert.equal(f.editor.focused, 0);
  f.receive('activate', { id: 'first' });
  assert.equal(f.editor.readOnly, true);
  assert.equal(f.app.save(), false);
  assert.equal(f.app.commandContext().executeEdits('pending-close', []), false);
  f.receive('resume');
  assert.equal(f.editor.readOnly, false);
  assert.equal(f.app.save(), true);
});

test('trusted command registration receives the active selection, groups edits into undo and supports disposal', () => {
  const f = fixture();
  const model = f.open('file');
  const registry = createCommandRegistry(() => f.app.commandContext());
  registerBundledCommands(registry);
  assert.equal(registry.canRun('selection.uppercase'), false);
  f.editor.selection = new Range(1, 1, 1, 9);
  assert.equal(registry.executeCommand('selection.uppercase'), true);
  assert.equal(model.getValue(), 'ORIGINAL\n');
  assert.equal(f.editor.undoStops, 2);
  model.undo();
  assert.equal(model.getValue(), 'original\n');
  assert.equal(f.app.isDirty('file'), false);
  f.editor.selection = new Range(1, 1, 1, 1);
  registry.executeCommand('insert.timestamp');
  assert.match(model.getValue(), /^\d{4}-\d{2}-\d{2}T/);
  model.undo();
  assert.equal(model.getValue(), 'original\n');
  const terminal = registry.registerCommand({ id: 'terminal', title: 'Terminal', run: ({ focusTerminal }) => focusTerminal() });
  registry.executeCommand('terminal');
  assert.deepEqual(f.messages.at(-1), { version: 1, type: 'focus-terminal' });
  assert.throws(() => registry.registerCommand({ id: 'terminal', title: 'Duplicate', run() {} }), /already registered/);
  terminal.dispose();
  assert.equal(registry.executeCommand('terminal'), false);
  f.open('readonly', { readOnly: true });
  assert.equal(registry.executeCommand('insert.timestamp'), false);
});

test('oversized buffers cannot save or confirm a partial flush, and undo restores synchronization', () => {
  const f = fixture();
  const model = f.open('file');
  f.insert('x'.repeat(maxTextLength));
  assert.equal(f.messages.at(-1).type, 'sync-error');
  assert.equal(f.app.save(), false);
  f.messages.length = 0;
  f.receive('flush', { requestId: 'too-large' });
  assert.deepEqual(f.messages.map((message) => message.type), ['sync-error']);
  f.receive('resume');
  model.undo();
  assert.equal(f.messages.at(-1).type, 'changed');
  assert.equal(f.messages.at(-1).text, 'original\n');
  assert.equal(f.app.save(), true);
  f.messages.length = 0;
  f.receive('flush', { requestId: 'recovered' });
  assert.equal(f.messages.at(-1).type, 'flushed');
});

test('document errors follow their buffer and clear after editing or a successful save', () => {
  const f = fixture();
  f.open('conflict');
  f.receive('error', { id: 'conflict', message: 'The file changed on disk.' });
  assert.equal(f.states.at(-1).error, 'The file changed on disk.');
  assert.equal(f.app.getActive().error, 'The file changed on disk.');
  f.open('alpha');
  assert.equal(f.states.at(-1).error, null);
  const stateCount = f.states.length;
  f.receive('error', { id: 'conflict', message: 'Another save conflict.' });
  assert.equal(f.states.length, stateCount);
  assert.equal(f.app.getActive().error, null);
  f.receive('activate', { id: 'conflict' });
  assert.equal(f.states.at(-1).error, 'Another save conflict.');
  f.insert('editing ');
  assert.equal(f.app.getActive().error, null);
  f.app.save();
  const revision = f.app.getActive().model.getVersionId();
  f.receive('error', { id: 'conflict', message: 'Save failed.' });
  assert.equal(f.app.getActive().error, 'Save failed.');
  f.receive('saved', { id: 'conflict', revision });
  assert.equal(f.app.getActive().error, null);
  f.receive('error', { id: 'conflict', message: 'Discarded conflict.' });
  f.receive('close', { id: 'conflict' });
  f.receive('activate', { id: 'alpha' });
  assert.equal(f.states.at(-1).error, null);
  assert.equal(f.app.getActive().error, null);
  f.receive('error', { id: 'conflict', message: 'Late error for a closed document.' });
  assert.equal(f.app.getActive().error, null);
  assert.equal(f.errors.length, 0);
});

test('protocol validates all message fields before allocating or changing models', () => {
  const f = fixture();
  const valid = { version: 1, type: 'open', id: 'file', workspaceId: 'workspace', path: 'file.ts', text: '', readOnly: false };
  assert.equal(validateHostMessage(valid), true);
  for (const message of [null, [], 'open', { ...valid, version: 2 }, { ...valid, text: 12 }, { ...valid, id: '' }, { ...valid, readOnly: 'false' }, { ...valid, dirty: 1 }, { ...valid, eol: 'mixed' }, { ...valid, text: 'x'.repeat(maxTextLength + 1) }, { version: 1, type: 'saved', id: 'file', revision: 1.5 }, { version: 1, type: 'saved', id: 'file', revision: -1 }, { version: 1, type: 'load-plugin', url: 'https://example.org/plugin.js' }]) {
    assert.equal(f.app.receive(message), false);
  }
  assert.equal(f.liveModels.size, 0);
  assert.equal(f.receive('activate', { id: 'missing' }), false);
  assert.match(f.errors.at(-1), /unknown document/);
  f.receive('focus');
  assert.equal(f.editor.focused, 1);
  f.receive('error', { id: '', message: 'failed to open file' });
  assert.equal(f.errors.at(-1), 'failed to open file');
});
