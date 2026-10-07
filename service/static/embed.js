import { loadCodeMirror } from './vendor.js';
import { exampleData, openExample } from './example_data.js';

const CHANNEL = 'qumir-example';
const send = (type, details = {}) => {
  if (parent !== window) {
    parent.postMessage({ channel: CHANNEL, type, ...details }, location.origin);
  }
};

try {
  await loadCodeMirror();
  const app = await import('./app.js');
  let initial = null;
  let loading = false;
  const reset = document.getElementById('example-reset');
  const open = document.getElementById('example-open');
  const run = document.getElementById('btn-run');
  const restore = async () => {
    loading = true;
    reset.disabled = true;
    run.disabled = true;
    try {
      await app.loadEmbeddedExample(initial);
      run.disabled = false;
      open.disabled = false;
      send('loaded');
    } finally {
      reset.disabled = false;
      loading = false;
    }
  };
  window.addEventListener('message', async event => {
    if (event.origin !== location.origin || event.source !== parent || event.data?.channel !== CHANNEL) {
      return;
    }
    try {
      if (event.data.type === 'init' && !initial) {
        const io = event.data.options?.io ?? true;
        if (typeof io !== 'boolean') {
          throw new Error('Неверная настройка панели ввода/вывода');
        }
        document.getElementById('example-io').hidden = !io;
        document.getElementById('example-result').hidden = io;
        initial = exampleData(event.data.example);
        await restore();
      } else if (event.data.type === 'stop') {
        await app.stopEmbeddedExample();
      }
    } catch (error) {
      send('error', { message: error.message });
    }
  });
  reset.addEventListener('click', async () => {
    if (loading || !initial) {
      return;
    }
    try {
      await restore();
    } catch (error) {
      send('error', { message: error.message });
    }
  });
  open.addEventListener('click', () => {
    try {
      openExample(app.embeddedExampleData());
      send('event', { action: 'open' });
    } catch (error) {
      document.getElementById('status').textContent = error.message;
    }
  });
  document.addEventListener('qumir-embed-event', event => send('event', { action: event.detail }));
  let lastHeight = 0;
  new ResizeObserver(() => {
    const height = Math.ceil(document.body.getBoundingClientRect().height);
    if (height !== lastHeight) {
      lastHeight = height;
      send('height', { height });
    }
  }).observe(document.body);
  window.addEventListener('pagehide', () => { app.stopEmbeddedExample(); });
  send('ready');
} catch (error) {
  document.getElementById('status').textContent = 'Не удалось загрузить редактор: ' + error.message;
  send('error', { message: error.message });
}
