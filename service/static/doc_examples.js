import { readExample } from './example_data.js';

const CHANNEL = 'qumir-example';
const EVENTS = new Set(['run', 'run_ok', 'run_compile_error', 'run_error', 'run_stopped', 'open']);

class QumirExample extends HTMLElement {
  connectedCallback() {
    queueMicrotask(() => {
      if (!this.isConnected || this.observer || this.frame) {
        return;
      }
      try {
        if (!this.initial) {
          if (Array.from(document.querySelectorAll('qumir-example')).filter(node => node.id === this.id).length !== 1) {
            throw new Error('У каждого примера в статье должен быть уникальный id');
          }
          const io = this.getAttribute('io');
          if (io !== null && io !== 'true' && io !== 'false') {
            throw new Error('Атрибут io должен быть true или false');
          }
          this.options = { io: io !== 'false' };
          this.initial = readExample(this);
          this.source = document.createElement('div');
          this.source.className = 'qumir-example-source';
          this.source.append(...this.childNodes);
          this.append(this.source);
          this.notice = document.createElement('div');
          this.notice.className = 'qumir-example-notice';
          this.notice.setAttribute('role', 'status');
          this.append(this.notice);
        }
        if (!('IntersectionObserver' in window)) {
          this.mount();
          return;
        }
        this.observer = new IntersectionObserver(entries => {
          if (entries.some(entry => entry.isIntersecting)) {
            this.mount();
          }
        }, { rootMargin: '200px' });
        this.observer.observe(this);
      } catch (error) {
        const notice = document.createElement('p');
        notice.className = 'qumir-example-notice';
        notice.setAttribute('role', 'status');
        notice.textContent = 'Не удалось подключить пример: ' + error.message;
        this.append(notice);
      }
    });
  }

  mount() {
    if (!this.isConnected || this.frame) {
      return;
    }
    this.observer?.disconnect();
    this.observer = null;
    this.notice.textContent = 'Подключаем редактор…';
    const frame = document.createElement('iframe');
    frame.title = 'Интерактивный пример ' + this.id;
    frame.className = 'qumir-example-frame';
    frame.height = '560';
    this.frame = frame;
    this.receive = event => {
      if (event.origin !== location.origin || event.source !== frame.contentWindow || event.data?.channel !== CHANNEL) {
        return;
      }
      const message = event.data;
      if (message.type === 'ready') {
        this.send('init', { example: this.initial, options: this.options });
      } else if (message.type === 'loaded') {
        clearTimeout(this.timer);
        this.dataset.ready = 'true';
        this.notice.textContent = '';
      } else if (message.type === 'height' && Number.isFinite(message.height)) {
        frame.height = String(Math.max(200, Math.min(1400, Math.ceil(message.height))));
      } else if (message.type === 'event' && EVENTS.has(message.action)) {
        this.dispatchEvent(new CustomEvent('qumir-example-event', {
          bubbles: true,
          detail: { action: message.action, exampleId: this.id, doc: this.closest('[data-doc-path]')?.dataset.docPath || location.pathname },
        }));
      } else if (message.type === 'error') {
        this.fail(message.message || 'Не удалось подключить редактор');
      }
    };
    window.addEventListener('message', this.receive);
    this.timer = setTimeout(() => this.fail('Редактор не загрузился. Проверьте соединение'), 20000);
    frame.src = '/embed.html';
    this.append(frame);
  }

  send(type, details = {}) {
    this.frame?.contentWindow?.postMessage({ channel: CHANNEL, type, ...details }, location.origin);
  }

  stop() {
    this.send('stop');
  }

  dispose() {
    this.stop();
    this.observer?.disconnect();
    this.observer = null;
    clearTimeout(this.timer);
    window.removeEventListener('message', this.receive);
    this.receive = null;
    this.frame?.remove();
    this.frame = null;
    delete this.dataset.ready;
  }

  fail(message) {
    this.dispose();
    this.notice.textContent = message + '. ';
    const retry = document.createElement('button');
    retry.type = 'button';
    retry.textContent = 'Повторить';
    retry.addEventListener('click', () => this.mount(), { once: true });
    this.notice.append(retry);
  }

  disconnectedCallback() {
    this.dispose();
  }
}

if (!customElements.get('qumir-example')) {
  customElements.define('qumir-example', QumirExample);
}
