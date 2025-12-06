// Onboarding tour for Qumir Playground
// Shows step-by-step tooltips for first-time visitors

const TOUR_COOKIE = 'q_tour_seen';
const TOUR_VERSION = 1; // Increment to show tour again after major UI changes

const TOUR_STEPS = [
  {
    target: '#project-toggle',
    title: 'Проекты',
    text: 'Здесь хранятся ваши программы. Можно создавать несколько проектов и переключаться между ними.',
    position: 'bottom'
  },
  {
    target: '.toolbar',
    title: 'Вставка конструкций',
    text: 'Кнопки для быстрой вставки циклов, условий и функций. Наведите на кнопку, чтобы увидеть подсказку.',
    position: 'bottom'
  },
  {
    target: '#examples',
    title: 'Примеры',
    text: 'Готовые программы для изучения: алгоритмы сортировки, черепашья графика, исполнитель Робот и многое другое.',
    position: 'bottom'
  },
  {
    target: '.pane.left',
    title: 'Редактор кода',
    text: 'Пишите программу на языке Кумир здесь. Подсветка синтаксиса и автоотступы помогут писать чистый код.',
    position: 'right'
  },
  {
    target: '#view',
    title: 'Просмотр компиляции',
    text: 'Выберите, что показывать: IR (внутреннее представление), LLVM, ассемблер или WebAssembly.',
    position: 'bottom'
  },
  {
    target: '.pane.right',
    title: 'Вывод',
    text: 'Результат компиляции или графика исполнителей Черепаха/Робот при использовании графических команд.',
    position: 'left'
  },
  {
    target: '#btn-run',
    title: 'Запуск',
    text: 'Нажмите Run, чтобы скомпилировать и выполнить программу. Результат появится в stdout.',
    position: 'top'
  },
  {
    target: '#io-select',
    title: 'Ввод/Вывод',
    text: 'stdout — вывод программы, stdin — ввод данных. Можно добавлять файлы для работы с ними из программы.',
    position: 'top'
  },
  {
    target: '#btn-share',
    title: 'Поделиться',
    text: 'Создаёт ссылку на вашу программу. Отправьте её другу или сохраните для себя!',
    position: 'bottom'
  }
];

let currentStep = 0;
let tourActive = false;
let tourOverlay = null;
let tourTooltip = null;
let tourHighlight = null;

function getCookie(name) {
  const n = encodeURIComponent(name) + '=';
  const parts = document.cookie.split(';');
  for (let p of parts) {
    p = p.trim();
    if (p.startsWith(n)) return decodeURIComponent(p.slice(n.length));
  }
  return null;
}

function setCookie(name, value, days = 365) {
  const expires = `max-age=${days * 24 * 60 * 60}`;
  document.cookie = `${encodeURIComponent(name)}=${encodeURIComponent(value)}; ${expires}; path=/`;
}

function shouldShowTour() {
  const seen = getCookie(TOUR_COOKIE);
  if (!seen) return true;
  const seenVersion = parseInt(seen, 10) || 0;
  return seenVersion < TOUR_VERSION;
}

function markTourSeen() {
  setCookie(TOUR_COOKIE, String(TOUR_VERSION));
}

function createTourElements() {
  // Overlay (semi-transparent backdrop)
  tourOverlay = document.createElement('div');
  tourOverlay.className = 'tour-overlay';
  tourOverlay.addEventListener('click', skipTour);
  document.body.appendChild(tourOverlay);

  // Highlight (cutout around target element)
  tourHighlight = document.createElement('div');
  tourHighlight.className = 'tour-highlight';
  document.body.appendChild(tourHighlight);

  // Tooltip
  tourTooltip = document.createElement('div');
  tourTooltip.className = 'tour-tooltip';
  tourTooltip.innerHTML = `
    <div class="tour-title"></div>
    <div class="tour-text"></div>
    <div class="tour-footer">
      <span class="tour-progress"></span>
      <div class="tour-buttons">
        <button class="tour-skip">Пропустить</button>
        <button class="tour-next">Далее</button>
      </div>
    </div>
  `;
  document.body.appendChild(tourTooltip);

  tourTooltip.querySelector('.tour-skip').addEventListener('click', skipTour);
  tourTooltip.querySelector('.tour-next').addEventListener('click', nextStep);

  // Close on Escape
  document.addEventListener('keydown', handleKeydown);
}

function handleKeydown(e) {
  if (!tourActive) return;
  if (e.key === 'Escape') {
    skipTour();
  } else if (e.key === 'Enter' || e.key === ' ' || e.key === 'ArrowRight') {
    e.preventDefault();
    nextStep();
  } else if (e.key === 'ArrowLeft' && currentStep > 0) {
    e.preventDefault();
    currentStep--;
    showStep(currentStep);
  }
}

function removeTourElements() {
  if (tourOverlay) { tourOverlay.remove(); tourOverlay = null; }
  if (tourHighlight) { tourHighlight.remove(); tourHighlight = null; }
  if (tourTooltip) { tourTooltip.remove(); tourTooltip = null; }
  document.removeEventListener('keydown', handleKeydown);
}

function showStep(index) {
  const step = TOUR_STEPS[index];
  if (!step) return;

  const target = document.querySelector(step.target);
  if (!target) {
    // Skip this step if target not found
    if (index < TOUR_STEPS.length - 1) {
      currentStep++;
      showStep(currentStep);
    } else {
      endTour();
    }
    return;
  }

  // Position highlight around target
  const rect = target.getBoundingClientRect();
  const padding = 6;
  tourHighlight.style.left = (rect.left - padding) + 'px';
  tourHighlight.style.top = (rect.top - padding) + 'px';
  tourHighlight.style.width = (rect.width + padding * 2) + 'px';
  tourHighlight.style.height = (rect.height + padding * 2) + 'px';

  // Update tooltip content
  tourTooltip.querySelector('.tour-title').textContent = step.title;
  tourTooltip.querySelector('.tour-text').textContent = step.text;
  tourTooltip.querySelector('.tour-progress').textContent = `${index + 1} / ${TOUR_STEPS.length}`;

  const nextBtn = tourTooltip.querySelector('.tour-next');
  nextBtn.textContent = index === TOUR_STEPS.length - 1 ? 'Готово' : 'Далее';

  // Position tooltip
  positionTooltip(rect, step.position);
}

function positionTooltip(targetRect, position) {
  const tooltip = tourTooltip;
  const gap = 12;

  // Reset positioning
  tooltip.style.left = '';
  tooltip.style.top = '';
  tooltip.style.right = '';
  tooltip.style.bottom = '';
  tooltip.className = 'tour-tooltip tour-' + position;

  // Get tooltip dimensions after content update
  const tooltipRect = tooltip.getBoundingClientRect();
  const vw = window.innerWidth;
  const vh = window.innerHeight;

  let left, top;

  switch (position) {
    case 'bottom':
      left = targetRect.left + targetRect.width / 2 - tooltipRect.width / 2;
      top = targetRect.bottom + gap;
      break;
    case 'top':
      left = targetRect.left + targetRect.width / 2 - tooltipRect.width / 2;
      top = targetRect.top - tooltipRect.height - gap;
      break;
    case 'left':
      left = targetRect.left - tooltipRect.width - gap;
      top = targetRect.top + targetRect.height / 2 - tooltipRect.height / 2;
      break;
    case 'right':
      left = targetRect.right + gap;
      top = targetRect.top + targetRect.height / 2 - tooltipRect.height / 2;
      break;
    default:
      left = targetRect.left;
      top = targetRect.bottom + gap;
  }

  // Keep tooltip within viewport
  if (left < 10) left = 10;
  if (left + tooltipRect.width > vw - 10) left = vw - tooltipRect.width - 10;
  if (top < 10) top = 10;
  if (top + tooltipRect.height > vh - 10) top = vh - tooltipRect.height - 10;

  tooltip.style.left = left + 'px';
  tooltip.style.top = top + 'px';
}

function nextStep() {
  currentStep++;
  if (currentStep >= TOUR_STEPS.length) {
    endTour();
  } else {
    showStep(currentStep);
  }
}

function skipTour() {
  endTour();
}

function endTour() {
  tourActive = false;
  markTourSeen();
  removeTourElements();
  document.body.classList.remove('tour-active');
}

export function startTour() {
  if (tourActive) return;
  tourActive = true;
  currentStep = 0;
  document.body.classList.add('tour-active');
  createTourElements();
  showStep(0);
}

export function resetTour() {
  setCookie(TOUR_COOKIE, '0');
}

export function initTour() {
  // Auto-start tour for first-time visitors (after a short delay for UI to settle)
  if (shouldShowTour()) {
    setTimeout(() => {
      startTour();
    }, 800);
  }
}
