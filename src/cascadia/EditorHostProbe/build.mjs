import { build } from 'esbuild';
import { copyFile, mkdir } from 'node:fs/promises';

await mkdir('dist', { recursive: true });
await build({
  entryPoints: {
    editor: 'editor.js',
    'editor.worker': 'node_modules/monaco-editor/esm/vs/editor/editor.worker.js',
    'json.worker': 'node_modules/monaco-editor/esm/vs/language/json/json.worker.js',
    'ts.worker': 'node_modules/monaco-editor/esm/vs/language/typescript/ts.worker.js',
  },
  bundle: true,
  format: 'esm',
  outdir: 'dist',
  target: 'es2022',
  minify: true,
  loader: { '.ttf': 'file' },
  legalComments: 'external',
  logLevel: 'info',
});
await Promise.all([
  copyFile('index.html', 'dist/index.html'),
  copyFile('node_modules/monaco-editor/LICENSE', 'dist/LICENSE.monaco.txt'),
  copyFile('node_modules/monaco-editor/ThirdPartyNotices.txt', 'dist/ThirdPartyNotices.monaco.txt'),
]);
