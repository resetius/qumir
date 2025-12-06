/**
 * Help/Documentation panel module
 * Loads markdown files from /docs/ and renders them using marked.js
 * Panel opens without blocking editor interaction
 */

const DOCS_BASE = '/docs/';

let docsDrawer = null;
let docsContent = null;
let docsNav = null;
let docsBackBtn = null;
let docsCloseBtn = null;
let helpBtn = null;

let currentDoc = 'index.md';
let history = [];
let docCache = {};

/**
 * Check if drawer is open
 */
function isOpen() {
  return docsDrawer && docsDrawer.classList.contains('open');
}

/**
 * Open the help drawer
 */
export function openDocs() {
  if (docsDrawer) {
    docsDrawer.classList.add('open');
    docsDrawer.setAttribute('aria-hidden', 'false');
    // Load initial doc if not loaded
    if (!docsContent.innerHTML || docsContent.querySelector('.docs-loading')) {
      loadDoc(currentDoc);
    }
  }
}

/**
 * Close the help drawer
 */
export function closeDocs() {
  if (docsDrawer) {
    docsDrawer.classList.remove('open');
    docsDrawer.setAttribute('aria-hidden', 'true');
  }
}

/**
 * Toggle the help drawer
 */
export function toggleDocs() {
  if (isOpen()) {
    closeDocs();
  } else {
    openDocs();
  }
}

/**
 * Load and render a markdown document
 */
async function loadDoc(filename, addToHistory = true) {
  if (!docsContent) return;

  // Update navigation
  updateNavActive(filename);

  // Show loading state
  docsContent.innerHTML = '<div class="docs-loading">Загрузка...</div>';

  try {
    let markdown;

    // Check cache first
    if (docCache[filename]) {
      markdown = docCache[filename];
    } else {
      const response = await fetch(DOCS_BASE + filename);
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      markdown = await response.text();
      docCache[filename] = markdown;
    }

    // Render markdown
    if (typeof marked !== 'undefined') {
      // Configure marked for proper rendering
      marked.setOptions({
        breaks: false,
        gfm: true,
        headerIds: true,
        mangle: false
      });
      docsContent.innerHTML = marked.parse(markdown);
    } else {
      // Fallback: show raw markdown
      docsContent.innerHTML = `<pre>${escapeHtml(markdown)}</pre>`;
    }

    // Handle internal links to other .md files
    docsContent.querySelectorAll('a').forEach(link => {
      const href = link.getAttribute('href');
      if (href && href.endsWith('.md') && !href.startsWith('http')) {
        link.addEventListener('click', (e) => {
          e.preventDefault();
          loadDoc(href, true);
        });
      }
    });

    // Scroll to top
    docsContent.scrollTop = 0;

    // Update history
    if (addToHistory && currentDoc !== filename) {
      history.push(currentDoc);
    }
    currentDoc = filename;
    updateBackButton();

  } catch (err) {
    console.error('Failed to load doc:', filename, err);
    docsContent.innerHTML = `<div class="docs-error">
      <p>Не удалось загрузить документ</p>
      <p><small>${escapeHtml(err.message)}</small></p>
    </div>`;
  }
}

/**
 * Go back in history
 */
function goBack() {
  if (history.length > 0) {
    const prev = history.pop();
    loadDoc(prev, false);
  }
}

/**
 * Update the active state of navigation links
 */
function updateNavActive(filename) {
  if (!docsNav) return;
  docsNav.querySelectorAll('.docs-nav-link').forEach(link => {
    const doc = link.getAttribute('data-doc');
    if (doc === filename) {
      link.classList.add('active');
    } else {
      link.classList.remove('active');
    }
  });
}

/**
 * Update back button state
 */
function updateBackButton() {
  if (docsBackBtn) {
    docsBackBtn.disabled = history.length === 0;
  }
}

/**
 * Escape HTML entities
 */
function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

/**
 * Initialize the help panel
 */
export function initDocs() {
  docsDrawer = document.getElementById('docs-drawer');
  docsContent = document.getElementById('docs-content');
  docsNav = document.getElementById('docs-nav');
  docsBackBtn = document.getElementById('docs-back');
  docsCloseBtn = document.getElementById('docs-close');
  helpBtn = document.getElementById('btn-help');

  if (!docsDrawer || !docsContent) {
    console.warn('Help panel elements not found');
    return;
  }

  // Help button in header
  if (helpBtn) {
    helpBtn.addEventListener('click', toggleDocs);
  }

  // Docs page button in footer - opens full docs page
  const docsPageBtn = document.getElementById('docs-page-btn');
  if (docsPageBtn) {
    docsPageBtn.addEventListener('click', () => {
      window.open('/docs.html', '_blank');
    });
  }

  // Close button
  if (docsCloseBtn) {
    docsCloseBtn.addEventListener('click', closeDocs);
  }

  // Back button
  if (docsBackBtn) {
    docsBackBtn.addEventListener('click', goBack);
  }

  // Navigation links
  if (docsNav) {
    docsNav.querySelectorAll('.docs-nav-link').forEach(link => {
      link.addEventListener('click', (e) => {
        e.preventDefault();
        const doc = link.getAttribute('data-doc');
        if (doc) {
          loadDoc(doc, true);
        }
      });
    });
  }

  // Escape key to close (optional, since panel doesn't block)
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && isOpen()) {
      closeDocs();
    }
  });
}
