// The bridge accepts data only. Extensions are trusted commands bundled with
// this page; there is deliberately no script URL, eval or plugin loader API.
export const protocolVersion = 1;
export const maxTextLength = 3 * 1024 * 1024;
const maxSerializedLength = 16 * 1024 * 1024;

const isId = (value) => typeof value === 'string' && value.length > 0 && value.length <= 4096;
const isRevision = (value) => Number.isSafeInteger(value) && value > 0;
const isText = (value) => typeof value === 'string' && value.length <= maxTextLength;

export function validateHostMessage(message) {
  if (!message || typeof message !== 'object' || Array.isArray(message) || message.version !== protocolVersion) return false;
  switch (message.type) {
    case 'open':
      return isId(message.id) && isId(message.workspaceId) && isId(message.path) && isText(message.text) &&
        typeof message.readOnly === 'boolean' && (message.dirty === undefined || typeof message.dirty === 'boolean') &&
        (message.eol === undefined || ['lf', 'crlf'].includes(message.eol)) &&
        (message.reason === undefined || isText(message.reason));
    case 'activate':
    case 'close':
      return isId(message.id);
    case 'saved':
      return isId(message.id) && isRevision(message.revision);
    case 'error':
      return typeof message.id === 'string' && message.id.length <= 4096 && isText(message.message);
    case 'focus':
    case 'save-active':
    case 'resume':
      return true;
    case 'flush':
      return isId(message.requestId);
    case 'theme':
      return ['vs', 'vs-dark', 'hc-black', 'hc-light'].includes(message.theme);
    default:
      return false;
  }
}

export function createWorkspace({ monaco, editor, postMessage, onState = () => {}, onError = () => {}, onTheme = () => {} }) {
  const documents = new Map();
  let activeId = null;
  let saveSequence = 0;
  let suspended = false;
  const send = (message) => postMessage({ version: protocolVersion, ...message });
  const dirty = (document) => document.model.getAlternativeVersionId() !== document.savedRevision;
  const getActive = () => documents.get(activeId) ?? null;
  const notify = () => onState(getActive(), getActive() ? dirty(getActive()) : false);
  const documentError = (document, message) => {
    document.error = message;
    if (document.id === activeId) notify();
  };
  // getVersionId increases on undo/redo too. Alternative versions identify
  // saved undo positions but cannot order snapshots crossing the native bridge.
  const snapshot = (document, type) => ({ type, id: document.id, text: document.model.getValue(), dirty: dirty(document), revision: document.model.getVersionId() });
  const checkSnapshot = (document, message) => {
    if (message.text.length <= maxTextLength && JSON.stringify({ version: protocolVersion, ...message }).length <= maxSerializedLength) return true;
    const error = '文件缓冲区过大，无法与终端同步或保存。请撤销或缩小文本后再关闭。';
    send({ type: 'sync-error', id: document.id, message: error });
    documentError(document, error);
    return false;
  };
  const changed = (document) => {
    const message = snapshot(document, 'changed');
    const synchronized = checkSnapshot(document, message);
    if (synchronized) {
      document.error = null;
      send(message);
    }
    if (document.id === activeId) notify();
    return synchronized;
  };
  const rememberView = () => {
    const document = getActive();
    if (document) document.view = editor.saveViewState();
  };

  function open(message) {
    const existing = documents.get(message.id);
    if (existing) {
      // A repeated open is a host synchronization message, not a buffer reset.
      if (existing.path !== message.path || existing.workspaceId !== message.workspaceId) throw Error('document identity changed');
      return;
    }
    const filename = message.path.split(/[\\/]/).at(-1);
    const uri = monaco.Uri.parse(`inmemory://workspace/${encodeURIComponent(message.workspaceId)}/${encodeURIComponent(message.id)}/${encodeURIComponent(filename)}`);
    const model = monaco.editor.createModel(message.text, undefined, uri);
    const eol = message.eol ?? (message.text.match(/\r?\n/)?.[0] === '\r\n' ? 'crlf' : 'lf');
    model.setEOL(eol === 'crlf' ? monaco.editor.EndOfLineSequence.CRLF : monaco.editor.EndOfLineSequence.LF);
    const document = {
      id: message.id, workspaceId: message.workspaceId, path: message.path, readOnly: message.readOnly,
      reason: message.reason, error: null, model, view: null, savedRevision: message.dirty ? null : model.getAlternativeVersionId(),
      pendingSaves: new Map(), acknowledgedSequence: 0,
    };
    document.listener = model.onDidChangeContent(() => changed(document));
    documents.set(message.id, document);
    // Synchronize Monaco's revision after EOL initialization, including buffers
    // restored after a controller reload, before the native save dialog uses it.
    changed(document);
  }

  function activate(id) {
    const document = documents.get(id);
    if (!document) throw Error('unknown document');
    if (activeId !== id) {
      rememberView();
      activeId = id;
      editor.setModel(document.model);
      editor.restoreViewState(document.view);
    }
    editor.updateOptions({ readOnly: suspended || document.readOnly });
    notify();
  }

  function close(id) {
    const document = documents.get(id);
    if (!document) return;
    if (activeId === id) {
      activeId = null;
      editor.setModel(null);
    }
    document.listener.dispose();
    document.model.dispose();
    document.pendingSaves.clear();
    document.view = null;
    documents.delete(id);
    notify();
  }

  function saved(id, revision) {
    const document = documents.get(id);
    if (!document) return;
    // The native unsaved-changes dialog can also save the buffer it already
    // received. Only the exact current revision may establish this baseline.
    const pending = document.pendingSaves.get(revision) ??
      (revision === document.model.getVersionId() ? { sequence: ++saveSequence, alternativeRevision: document.model.getAlternativeVersionId() } : undefined);
    if (pending === undefined) return;
    document.pendingSaves.delete(revision);
    // Saving is asynchronous: acknowledge the exact requested snapshot even
    // when the user has typed again. Undoing to that snapshot becomes clean.
    if (pending.sequence <= document.acknowledgedSequence) return;
    document.acknowledgedSequence = pending.sequence;
    document.savedRevision = pending.alternativeRevision;
    document.error = null;
    if (activeId === id) notify();
  }

  function receive(message) {
    if (!validateHostMessage(message)) return false;
    try {
      switch (message.type) {
        case 'open': open(message); break;
        case 'activate': activate(message.id); break;
        case 'close': close(message.id); break;
        case 'saved': saved(message.id, message.revision); break;
        case 'error': {
          const document = documents.get(message.id);
          if (document) documentError(document, message.message);
          else if (!message.id) onError(message.message);
          break;
        }
        case 'focus': editor.focus(); break;
        case 'save-active': save(); break;
        case 'flush':
          suspended = true;
          editor.updateOptions({ readOnly: true });
          notify();
          {
            let synchronized = true;
            for (const document of documents.values()) synchronized = changed(document) && synchronized;
            // Do not confirm a partial snapshot. The native timeout cancels
            // closing and sends resume so the user can shrink/undo the text.
            if (synchronized) send({ type: 'flushed', requestId: message.requestId });
          }
          break;
        case 'resume':
          suspended = false;
          editor.updateOptions({ readOnly: getActive()?.readOnly ?? false });
          notify();
          break;
        case 'theme': monaco.editor.setTheme(message.theme); onTheme(message.theme); break;
      }
    } catch (error) {
      onError(String(error));
      return false;
    }
    return true;
  }

  function save() {
    const document = getActive();
    if (!document || document.readOnly || suspended) return false;
    const revision = document.model.getVersionId();
    const message = { type: 'save', id: document.id, text: document.model.getValue(), revision };
    if (!checkSnapshot(document, message)) return false;
    document.pendingSaves.set(revision, { sequence: ++saveSequence, alternativeRevision: document.model.getAlternativeVersionId() });
    send(message);
    return true;
  }

  function commandContext() {
    const document = getActive();
    return Object.freeze({
      model: document?.model ?? null, selection: document ? editor.getSelection() : null,
      readOnly: !document || document.readOnly || suspended,
      executeEdits(source, edits) {
        if (!document || document.readOnly || suspended || document !== getActive()) return false;
        editor.pushUndoStop();
        const result = editor.executeEdits(source, edits);
        editor.pushUndoStop();
        editor.focus();
        return result;
      },
      focusTerminal: () => send({ type: 'focus-terminal' }),
    });
  }

  return {
    receive, save, commandContext, getActive, isDirty: (id) => documents.has(id) && dirty(documents.get(id)),
    dispose() { for (const id of [...documents.keys()]) close(id); },
  };
}

export function createCommandRegistry(getContext) {
  const commands = new Map();
  return Object.freeze({
    registerCommand({ id, title, run, when = () => true }) {
      if (!isId(id) || typeof title !== 'string' || typeof run !== 'function' || typeof when !== 'function') throw Error('invalid command');
      if (commands.has(id)) throw Error(`command already registered: ${id}`);
      const command = { id, title, run, when };
      commands.set(id, command);
      return { dispose() { if (commands.get(id) === command) commands.delete(id); } };
    },
    listCommands: () => [...commands.values()].map(({ id, title }) => ({ id, title })),
    canRun(id) { const command = commands.get(id); return !!command && command.when(getContext()); },
    executeCommand(id) {
      const command = commands.get(id);
      const context = getContext();
      if (!command || !command.when(context)) return false;
      return command.run(context);
    },
  });
}

export function registerBundledCommands(registry) {
  const editable = ({ model, readOnly }) => !!model && !readOnly;
  registry.registerCommand({
    id: 'selection.uppercase', title: '选区转大写', when: (context) => editable(context) && !!context.selection && !context.selection.isEmpty(),
    run: ({ model, selection, executeEdits }) => executeEdits('extension:selection.uppercase', [{ range: selection, text: model.getValueInRange(selection).toUpperCase(), forceMoveMarkers: true }]),
  });
  registry.registerCommand({
    id: 'insert.timestamp', title: '插入时间', when: editable,
    run: ({ selection, executeEdits }) => executeEdits('extension:insert.timestamp', [{ range: selection, text: new Date().toISOString(), forceMoveMarkers: true }]),
  });
}
