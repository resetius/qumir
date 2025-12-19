/**
 * Static documentation generator for Qumir
 *
 * Usage:
 *   npm install marked
 *   node build-docs.js
 *
 * Place this script in service/ if desired.
 *
 * Source markdown: docs/ru/*.md
 * Output HTML: service/static/docs-static/*.html
 * Template: service/static/docs.html
 */
import fs from 'fs';
import path from 'path';
import { marked } from 'marked';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Папка с markdown-файлами
const DOCS_SRC = path.join(__dirname, '../docs/ru');
// Папка для готовых html
const DOCS_OUT = path.join(__dirname, '../service/static/docs-static');

// Читаем шаблон docs.html
const TEMPLATE = fs.readFileSync(path.join(__dirname, '../service/static/docs.html'), 'utf8');

// Вырезаем <main>...</main> из шаблона
const MAIN_RE = /<main[^>]*id="docs-main"[^>]*>[\s\S]*?<\/main>/i;

function renderSidebar(active) {
  return `
    <nav class="docs-page-sidebar" id="docs-sidebar">
      <a href="index.html" class="${active === 'index.md' ? 'active' : ''}">Введение</a>
      <a href="syntax.html" class="${active === 'syntax.md' ? 'active' : ''}">Синтаксис языка</a>
      <a href="interpreter.html" class="${active === 'interpreter.md' ? 'active' : ''}">Интерпретатор</a>
      <a href="compiler.html" class="${active === 'compiler.md' ? 'active' : ''}">Компилятор</a>
      <a href="turtle.html" class="${active === 'turtle.md' ? 'active' : ''}">Исполнитель Черепаха</a>
      <a href="robot.html" class="${active === 'robot.md' ? 'active' : ''}">Исполнитель Робот</a>
      <a href="examples.html" class="${active === 'examples.md' ? 'active' : ''}">Примеры программ</a>
      <a href="advanced_examples.html" class="${active === 'advanced_examples.md' ? 'active' : ''}">Продвинутые примеры</a>
    </nav>
  `;
}

function buildOne(mdFile) {
  const mdPath = path.join(DOCS_SRC, mdFile);
  const htmlFile = mdFile.replace(/\.md$/, '.html');
  const htmlPath = path.join(DOCS_OUT, htmlFile);

  const markdown = fs.readFileSync(mdPath, 'utf8');
  // Use default marked heading rendering (no custom id generation)
  marked.setOptions({
    breaks: false,
    gfm: true
  });
  const content = marked.parse(markdown);

  // Заголовки для страниц
  const titles = {
    'index.md': 'Документация — Qumir',
    'syntax.md': 'Документация — Qumir (Синтаксис языка)',
    'interpreter.md': 'Документация — Qumir (Интерпретатор)',
    'compiler.md': 'Документация — Qumir (Компилятор)',
    'turtle.md': 'Документация — Qumir (Исполнитель Черепаха)',
    'robot.md': 'Документация — Qumir (Исполнитель Робот)',
    'examples.md': 'Документация — Qumir (Примеры программ)',
    'advanced_examples.md': 'Документация — Qumir (Продвинутые примеры)'
  };
  const pageTitle = titles[mdFile] || `Документация — Qumir (${mdFile.replace(/\.md$/, '')})`;

  // Remove all sidebars and SPA JS from template, then insert one sidebar
  let outHtml = TEMPLATE
    // Set per-page <title>
    .replace(/<title>.*?<\/title>/, `<title>${pageTitle}</title>`)
    // Remove all nav.docs-page-sidebar blocks
    .replace(/<nav class="docs-page-sidebar"[^>]*>[\s\S]*?<\/nav>/g, '')
    // Remove only the inline SPA JS block (not external scripts)
    .replace(/<script>\s*const DOCS_BASE[\s\S]*?loadDoc\(getDocFromUrl\(\)\);\s*<\/script>/, '')
    // Fix styles.css path to root
    .replace(/<link rel="stylesheet" href="[^"]*styles\.css[^"]*">/, '<link rel="stylesheet" href="/styles.css">');
  // Insert sidebar before <main>
  outHtml = outHtml.replace(/(<div class="docs-page-layout">\s*)/, `$1${renderSidebar(mdFile)}\n`);
  // Insert main content
  outHtml = outHtml.replace(MAIN_RE, `<main class="docs-page-main" id="docs-main">${content}</main>`);
  // Ensure metrika.local.js is present after </footer>
  if (!outHtml.includes('metrika.local.js')) {
    outHtml = outHtml.replace(/(<\/footer>)/, `$1\n  <script src="/metrika.local.js"></script>`);
  }
  // Add KaTeX auto-render script for static pages (after body content)
  const scriptBlock = '  <script>\n' +
    '    // Auto-render KaTeX formulas when page loads\n' +
    '    if (window.renderMathInElement) {\n' +
    '      document.addEventListener(\'DOMContentLoaded\', function() {\n' +
    '        renderMathInElement(document.body, {\n' +
    '          delimiters: [\n' +
    '            {left: \'$$\', right: \'$$\', display: true},\n' +
    '            {left: \'$\', right: \'$\', display: false}\n' +
    '          ]\n' +
    '        });\n' +
    '      });\n' +
    '    }\n' +
    '\n' +
    '    // Handle anchor links in static HTML - scroll within main container\n' +
    '    document.addEventListener(\'DOMContentLoaded\', function() {\n' +
    '      const main = document.getElementById(\'docs-main\');\n' +
    '      if (!main) return;\n' +
    '\n' +
    '      document.addEventListener(\'click\', function(e) {\n' +
    '        const link = e.target.closest(\'a[href^="#"]\');\n' +
    '        if (!link) return;\n' +
    '\n' +
    '        const href = link.getAttribute(\'href\');\n' +
    '        if (!href || href === \'#\' || !href.startsWith(\'#\')) return;\n' +
    '\n' +
    '        e.preventDefault();\n' +
    '        e.stopPropagation();\n' +
    '\n' +
    '        const anchor = href.slice(1);\n' +
    '        const target = document.getElementById(anchor);\n' +
    '        if (!target) {\n' +
    '          console.warn(\'Anchor not found:\', anchor);\n' +
    '          return;\n' +
    '        }\n' +
    '\n' +
    '        // Find the element to scroll to (next sibling if anchor is in <p>)\n' +
    '        let scrollTarget = target;\n' +
    '        if (target.parentElement && target.parentElement.tagName === \'P\' && target.parentElement.nextElementSibling) {\n' +
    '          scrollTarget = target.parentElement.nextElementSibling;\n' +
    '        }\n' +
    '\n' +
    '        // Calculate scroll position relative to main container\n' +
    '        const mainTop = main.getBoundingClientRect().top;\n' +
    '        const targetTop = scrollTarget.getBoundingClientRect().top;\n' +
    '        const offset = main.scrollTop + (targetTop - mainTop) - 20;\n' +
    '\n' +
    '        main.scrollTo({ top: Math.max(0, offset), behavior: \'smooth\' });\n' +
    '      }, true);\n' +
    '    });\n' +
    '  </script>\n';
  outHtml = outHtml.replace(/(<\/body>)/, function(match) { return scriptBlock + match; });
  fs.writeFileSync(htmlPath, outHtml, 'utf8');
  console.log('Built:', htmlPath);
}

fs.mkdirSync(DOCS_OUT, { recursive: true });

const files = fs.readdirSync(DOCS_SRC).filter(f => f.endsWith('.md'));
if (files.length === 0) {
  console.error('No markdown files found in', DOCS_SRC);
} else {
  files.forEach(buildOne);
  console.log('All docs built!');
}
