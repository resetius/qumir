/**
 * Static documentation generator for Qumir
 *
 * Usage:
 *   npm install marked
 *   node build-docs.js
 *
 * Place this script in service/ if desired.
 *
 * Source markdown: docs/ru/ (recursive .md files)
 * Output HTML: service/static/docs-static/ (recursive .html files)
 * Template: service/static/docs.html
 */
import fs from 'fs';
import path from 'path';
import { marked } from 'marked';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// Парсинг аргументов командной строки
const args = process.argv.slice(2);
let canonicalPrefix = null;

for (const arg of args) {
  if (arg.startsWith('--canonical_prefix=')) {
    canonicalPrefix = arg.split('=')[1];
    break;
  }
}

// Папка с markdown-файлами
const DOCS_SRC = path.join(__dirname, '../docs/ru');
// Папка для готовых html
const DOCS_OUT = path.join(__dirname, '../service/static/docs-static');

// Читаем шаблон docs.html
const TEMPLATE = fs.readFileSync(path.join(__dirname, '../service/static/docs.html'), 'utf8');

// Вырезаем <main>...</main> из шаблона
const MAIN_RE = /<main[^>]*id="docs-main"[^>]*>[\s\S]*?<\/main>/i;

function renderSidebar(active, prefix) {
  // prefix: '' для корневых страниц, '../' для страниц в подпапках
  const isExamplesSection = active === 'examples.md' || active.startsWith('examples/');
  return `
    <nav class="docs-page-sidebar" id="docs-sidebar">
      <a href="${prefix}index.html" class="${active === 'index.md' ? 'active' : ''}">Введение</a>
      <a href="${prefix}syntax.html" class="${active === 'syntax.md' ? 'active' : ''}">Синтаксис языка</a>
      <a href="${prefix}interpreter.html" class="${active === 'interpreter.md' ? 'active' : ''}">Интерпретатор</a>
      <a href="${prefix}compiler.html" class="${active === 'compiler.md' ? 'active' : ''}">Компилятор</a>
      <a href="${prefix}turtle.html" class="${active === 'turtle.md' ? 'active' : ''}">Исполнитель Черепаха</a>
      <a href="${prefix}drawer.html" class="${active === 'drawer.md' ? 'active' : ''}">Исполнитель Чертежник</a>
      <a href="${prefix}painter.html" class="${active === 'painter.md' ? 'active' : ''}">Исполнитель Рисователь</a>
      <a href="${prefix}complex.html" class="${active === 'complex.md' ? 'active' : ''}">Комплексные числа</a>
      <a href="${prefix}robot.html" class="${active === 'robot.md' ? 'active' : ''}">Исполнитель Робот</a>
      <a href="${prefix}files.html" class="${active === 'files.md' ? 'active' : ''}">Работа с файлами</a>
      <a href="${prefix}examples.html" class="${isExamplesSection ? 'active' : ''}">Библиотека примеров</a>
      <a href="${prefix}about.html" class="${active === 'about.md' ? 'active' : ''}">О проекте</a>
    </nav>
  `;
}

// Рекурсивный сбор .md файлов
function collectMdFiles(dir, base) {
  const results = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (entry.isDirectory()) {
      results.push(...collectMdFiles(path.join(dir, entry.name), path.join(base, entry.name)));
    } else if (entry.isFile() && entry.name.endsWith('.md')) {
      results.push(path.join(base, entry.name));
    }
  }
  return results;
}

function buildOne(mdFile) {
  const mdPath = path.join(DOCS_SRC, mdFile);
  const htmlFile = mdFile.replace(/\.md$/, '.html');
  const htmlPath = path.join(DOCS_OUT, htmlFile);

  // Определяем глубину вложенности для relative paths
  const depth = mdFile.split('/').length - 1;
  const prefix = depth > 0 ? '../'.repeat(depth) : '';

  const markdown = fs.readFileSync(mdPath, 'utf8');
  // Use default marked heading rendering (no custom id generation)
  marked.setOptions({
    breaks: false,
    gfm: true
  });
  const content = marked.parse(markdown);

  // Replace .md links with .html links in the generated content
  const contentWithFixedLinks = content.replace(/href="([^"]+)\.md"/g, 'href="$1.html"');

  // Заголовки для страниц
  const titles = {
    'index.md': 'Документация — Qumir',
    'syntax.md': 'Документация — Qumir (Синтаксис языка)',
    'interpreter.md': 'Документация — Qumir (Интерпретатор)',
    'compiler.md': 'Документация — Qumir (Компилятор)',
    'turtle.md': 'Документация — Qumir (Исполнитель Черепаха)',
    'drawer.md': 'Документация — Qumir (Исполнитель Чертежник)',
    'robot.md': 'Документация — Qumir (Исполнитель Робот)',
    'files.md': 'Документация — Qumir (Работа с файлами)',
    'examples.md': 'Документация — Qumir (Библиотека примеров)',
    'about.md': 'О проекте Qumir'
  };
  // Для страниц в подпапках — извлекаем заголовок из первого # в markdown
  let pageTitle = titles[mdFile];
  if (!pageTitle) {
    const headingMatch = markdown.match(/^#\s+(.+)$/m);
    if (headingMatch) {
      pageTitle = `Документация — Qumir (${headingMatch[1]})`;
    } else {
      pageTitle = `Документация — Qumir (${path.basename(mdFile, '.md')})`;
    }
  }

  // Описания для страниц (meta description)
  const descriptions = {
    'index.md': 'Полная документация по языку программирования КуМир: синтаксис, компилятор, интерпретатор, исполнители Черепаха, Чертежник и Робот. Примеры программ и руководства.',
    'syntax.md': 'Подробное описание синтаксиса языка КуМир: переменные, типы данных, операторы, функции и алгоритмы. Руководство по программированию на КуМир.',
    'interpreter.md': 'Интерпретатор КуМир: установка, использование, режимы работы. Запуск программ на КуМир через интерпретатор.',
    'compiler.md': 'Компилятор КуМир: компиляция в WebAssembly, оптимизация, генерация кода. Использование компилятора для создания исполняемых программ.',
    'turtle.md': 'Исполнитель Черепаха в КуМир: команды управления, рисование графики, создание фракталов и геометрических фигур.',
    'drawer.md': 'Исполнитель Чертежник в КуМир: рисование по координатам, графики функций, геометрические фигуры и диаграммы.',
    'robot.md': 'Исполнитель Робот в КуМир: команды управления, решение задач на лабиринты, алгоритмы обхода и поиска путей.',
    'files.md': 'Работа с файлами в КуМир: чтение и запись файлов, файловый ввод-вывод в браузере. Единственная онлайн реализация с поддержкой файлов.',
    'examples.md': 'Библиотека примеров программ на КуМир: алгоритмы сортировки, математические вычисления, работа с черепахой, чертежником и роботом.',
    'about.md': 'О проекте Qumir: история создания, архитектура компилятора на C++ и LLVM, исполнение в браузере через WebAssembly. Единственная онлайн реализация КуМир.'
  };
  let pageDescription = descriptions[mdFile];
  if (!pageDescription) {
    const headingMatch = markdown.match(/^#\s+(.+)$/m);
    const topic = headingMatch ? headingMatch[1] : path.basename(mdFile, '.md');
    pageDescription = `${topic} — пример программы на языке КуМир с пояснениями и разбором кода.`;
  }

  // Remove all sidebars and SPA JS from template, then insert one sidebar
  let outHtml = TEMPLATE
    // Set per-page <title>
    .replace(/<title>.*?<\/title>/, `<title>${pageTitle}</title>`)
    // Set per-page <meta name="description">
    .replace(/<meta name="description"[^>]*>/, `<meta name="description" content="${pageDescription}">`)
    // Remove all nav.docs-page-sidebar blocks
    .replace(/<nav class="docs-page-sidebar"[^>]*>[\s\S]*?<\/nav>/g, '')
    // Remove only the inline SPA JS block (not external scripts)
    .replace(/<script>\s*const DOCS_BASE[\s\S]*?loadDoc\(getDocFromUrl\(\)\);\s*<\/script>/, '')
    // Fix styles.css path to root
    .replace(/<link rel="stylesheet" href="[^"]*styles\.css[^"]*">/, '<link rel="stylesheet" href="/styles.css">');
  // Insert sidebar before <main>
  outHtml = outHtml.replace(/(<div class="docs-page-layout">\s*)/, `$1${renderSidebar(mdFile, prefix)}\n`);
  // Insert main content
  outHtml = outHtml.replace(MAIN_RE, `<main class="docs-page-main" id="docs-main">${contentWithFixedLinks}</main>`);
  // Ensure metrika.local.js is present after </footer>
  if (!outHtml.includes('metrika.local.js')) {
    outHtml = outHtml.replace(/(<\/footer>)/, `$1\n  <script src="/metrika.local.js"></script>`);
  }
  // Add canonical link if canonicalPrefix is provided
  if (canonicalPrefix) {
    const canonicalUrl = `${canonicalPrefix}/docs-static/${htmlFile}`;
    outHtml = outHtml.replace(/<\/head>/, `  <link rel="canonical" href="${canonicalUrl}">\n</head>`);
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

  // Создаём директорию для вложенных файлов
  fs.mkdirSync(path.dirname(htmlPath), { recursive: true });

  fs.writeFileSync(htmlPath, outHtml, 'utf8');
  console.log('Built:', htmlPath);
}

fs.mkdirSync(DOCS_OUT, { recursive: true });

const files = collectMdFiles(DOCS_SRC, '');
if (files.length === 0) {
  console.error('No markdown files found in', DOCS_SRC);
} else {
  files.forEach(buildOne);
  console.log('All docs built!');
}
