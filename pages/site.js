/**
 * nbitq site -- the one shared style and page shell for every page in pages/.
 *
 * Built on bitwrench (https://github.com/deftio/bitwrench). Everything visual
 * comes from bw.loadStyles() and the BCCL components; the only custom CSS is
 * the bit-diagram SVG, and it takes its colours from the generated palette.
 *
 * Each page calls:
 *
 *     nbitqSite.page('how-it-works.html', { title, subtitle, content: [...] });
 *
 * Copyright (c) 2026 deftio / M. A. Chatterjee, BSD-2-Clause.
 */
(function () {
  'use strict';

  var REPO = 'https://github.com/deftio/nbitq';

  var NAV = [
    { text: 'Home',            href: 'index.html' },
    { text: 'How it works',    href: 'how-it-works.html' },
    { text: 'Getting started', href: 'getting-started.html' },
    { text: 'API',             href: 'api.html' },
    { text: 'Docs (Markdown)', href: REPO + '/tree/main/docs' },
    { text: 'GitHub',          href: REPO }
  ];

  var styles = null;
  var p = null;

  // ---- Theme ---------------------------------------------------------------
  // One palette for the whole site. Dark mode is bitwrench's alternate palette,
  // toggled with bw.toggleThemeMode() and remembered per browser.
  function initStyles() {
    bw.loadStyles();
    // Favicon: the keycap-ten emoji as an SVG data URI, so there is no image
    // file to keep in step with anything.
    var icon = document.createElement('link');
    icon.rel = 'icon';
    icon.href = 'data:image/svg+xml,' + encodeURIComponent(
      "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>\uD83D\uDD1F</text></svg>");
    document.head.appendChild(icon);
    styles = bw.loadStyles({
      primary: '#1f5f8b',
      secondary: '#5a6b7c',
      spacing: 'normal',
      radius: 'md',
      elevation: 'sm'
    });
    p = styles.palette;

    var saved = null;
    try { saved = localStorage.getItem('nbitq-theme'); } catch (e) { /* private window etc. */ }
    if (saved === 'alternate') bw.setThemeMode('alternate');

    // The only custom rules on the site: left-justified button rows and table
    // headers (BCCL centres both), and the bit diagram.
    bw.injectCSS(bw.css({
      '.bw_bccl_hero_actions': { 'justify-content': 'flex-start' },
      '.bw_bccl_table > thead > tr > *': { 'text-align': 'left' },
      '.nq_bits': { 'max-width': '100%', height: 'auto', display: 'block', margin: '0.75rem 0' },
      '.nq_bits text': { 'font-family': '"SF Mono", Monaco, "Cascadia Code", "Roboto Mono", Consolas, monospace' },
      '.nq_bits .nq_cell': { stroke: p.light.border, 'stroke-width': '1' },
      '.nq_bits .nq_bit': { 'font-size': '13px', 'font-weight': '600', fill: p.dark.base },
      '.nq_bits .nq_idx': { 'font-size': '9px', fill: p.dark.hover },
      '.nq_bits .nq_byte': { 'font-size': '11px', 'font-weight': '600', fill: p.dark.base },
      '.nq_bits .nq_label': { 'font-size': '11px', fill: p.dark.base },
      '.nq_bits .nq_brace': { stroke: p.dark.hover, 'stroke-width': '1', fill: 'none' },
      '.nq_bits .nq_f0': { fill: p.primary.light },
      '.nq_bits .nq_f1': { fill: p.secondary.light },
      '.nq_bits .nq_f2': { fill: p.success.light },
      '.nq_bits .nq_f3': { fill: p.info.light },
      '.nq_bits .nq_fx': { fill: p.surfaceAlt }
    }), { id: 'nq_site_css' });
    return styles;
  }

  function toggleTheme() {
    var r = bw.toggleThemeMode();
    try { localStorage.setItem('nbitq-theme', r && r.mode === 'alternate' ? 'alternate' : 'primary'); } catch (e) { /* ignore */ }
  }

  // ---- Chrome --------------------------------------------------------------
  function nav(current) {
    return bw.makeNavbar({
      brand: 'nbitq',
      brandHref: 'index.html',
      dark: true,
      items: NAV.map(function (i) {
        return { text: i.text, href: i.href, active: i.href === current };
      })
    });
  }

  function hero(title, subtitle, actions) {
    return bw.makeHero({
      title: title,
      subtitle: subtitle,
      size: actions ? 'lg' : 'sm',
      centered: false,
      actions: actions
    });
  }

  // An anchor that looks like a button; BCCL's makeButton makes a <button>.
  // `variant` uses makeButton's spelling: 'primary', 'light', 'outline-light', ...
  function linkButton(text, href, variant) {
    var v = variant || 'primary';
    var cls = v.indexOf('outline') === 0 ? 'bw_bccl_btn_outline bw_' + v.replace(/^outline[_-]/, '') : 'bw_' + v;
    return { t: 'a', a: { href: href, class: 'bw_bccl_btn ' + cls }, c: text };
  }

  function footer() {
    return {
      t: 'footer', a: { class: 'bw_container bw_py_5 bw_text_muted' },
      c: [
        { t: 'p', a: { class: 'bw_mb_2' }, c: [
          'nbitq is BSD-2-Clause. © 2026 deftio / M. A. Chatterjee. ',
          { t: 'a', a: { href: REPO }, c: 'Source on GitHub' }, ' · ',
          { t: 'a', a: { href: REPO + '/issues' }, c: 'Issues' }, ' · ',
          { t: 'a', a: { href: REPO + '/blob/main/SECURITY.md' }, c: 'Security' }
        ]},
        { t: 'p', a: { class: 'bw_mb_0' }, c: [
          'Built with ',
          { t: 'a', a: { href: 'https://github.com/deftio/bitwrench' }, c: 'bitwrench' }, '. ',
          bw.makeButton({ text: 'Toggle dark mode', variant: 'outline-secondary', size: 'sm', onclick: toggleTheme })
        ]}
      ]
    };
  }

  // ---- Content helpers -----------------------------------------------------
  // Plain left-aligned section. makeSection centres its heading, which suits a
  // landing page but not reference text, so this is raw layout TACO.
  function section(title, content, id) {
    return {
      t: 'section', a: { class: 'bw_container bw_py_4', id: id },
      c: [ title && { t: 'h2', a: { class: 'bw_mb_3' }, c: title }, content ]
    };
  }

  function code(lines) {
    var text = Array.isArray(lines) ? lines.join('\n') : lines;
    return { t: 'pre', a: { class: 'bw_code_pre bw_mb_3' }, c: { t: 'code', a: { class: 'bw_code_block' }, c: text } };
  }

  function table(columns, rows) {
    return bw.makeTable({
      columns: columns.map(function (c) { return typeof c === 'string' ? { key: c, label: c } : c; }),
      data: rows,
      sortable: false,
      striped: true,
      className: 'bw_mb_3'
    });
  }

  function callout(title, content, variant) {
    return bw.makeAlert({
      variant: variant || 'info',
      className: 'bw_mb_3',
      content: [ title && { t: 'strong', c: title + ' ' }, content ]
    });
  }

  function p_(content) { return { t: 'p', c: content }; }

  // "Next: a · b · c" at the end of a page, in place of a call-to-action box.
  function next(links) {
    var c = ['Next: '];
    links.forEach(function (l, i) {
      if (i) c.push(' · ');
      c.push({ t: 'a', a: { href: l[1] }, c: l[0] });
    });
    return { t: 'section', a: { class: 'bw_container bw_py_4' }, c: { t: 'p', c: c } };
  }
  function ul(items) { return { t: 'ul', c: items.map(function (i) { return { t: 'li', c: i }; }) }; }
  function c_(s) { return { t: 'code', c: s }; }

  // ---- Bit diagram ---------------------------------------------------------
  // spec: { bytes: [0x9D, 0xDC, 0x55],
  //         fields: [{ name: 'symbol = 19', from: 0, len: 5 }, ...] }
  // Bit 0 is the MSB of byte 0, exactly as the library numbers them.
  function bits(spec) {
    var W = 24, H = 28, GAP = 6, PAD = 4;
    var nbits = spec.bytes.length * 8;
    var top = 18, idxY = top + H + 12, braceY = idxY + 8, labelY = braceY + 18;
    var width = PAD * 2 + nbits * W + (spec.bytes.length - 1) * GAP;
    var height = labelY + 8;

    function x(bit) { return PAD + bit * W + Math.floor(bit / 8) * GAP; }
    function fieldOf(bit) {
      for (var i = 0; i < spec.fields.length; i++) {
        var f = spec.fields[i];
        if (bit >= f.from && bit < f.from + f.len) return i;
      }
      return -1;
    }

    // Widen the drawing if a label is wider than the field it names.
    var CH = 6.4; // approx px per character at the label font size
    var minX = 0, maxX = width;
    spec.fields.forEach(function (f) {
      var cx = (x(f.from) + x(f.from + f.len - 1) + W) / 2, half = f.name.length * CH / 2 + 4;
      minX = Math.min(minX, cx - half); maxX = Math.max(maxX, cx + half);
    });
    minX = Math.floor(minX); maxX = Math.ceil(maxX);

    var cells = [], idx = [], byteLabels = [], braces = [];
    for (var b = 0; b < nbits; b++) {
      var byte = spec.bytes[b >> 3];
      var v = (byte >> (7 - (b & 7))) & 1;
      var fi = fieldOf(b);
      cells.push({ t: 'rect', a: { x: x(b), y: top, width: W, height: H, class: 'nq_cell ' + (fi < 0 ? 'nq_fx' : 'nq_f' + (fi % 4)) } });
      cells.push({ t: 'text', a: { x: x(b) + W / 2, y: top + H / 2 + 5, 'text-anchor': 'middle', class: 'nq_bit' }, c: String(v) });
      idx.push({ t: 'text', a: { x: x(b) + W / 2, y: idxY, 'text-anchor': 'middle', class: 'nq_idx' }, c: String(b) });
    }
    spec.bytes.forEach(function (byte, i) {
      var hex = '0x' + (byte < 16 ? '0' : '') + byte.toString(16).toUpperCase();
      byteLabels.push({ t: 'text', a: { x: x(i * 8) + 4 * W, y: 12, 'text-anchor': 'middle', class: 'nq_byte' }, c: 'byte ' + i + ' = ' + hex });
    });
    spec.fields.forEach(function (f) {
      var x0 = x(f.from) + 2, x1 = x(f.from + f.len - 1) + W - 2;
      braces.push({ t: 'path', a: { class: 'nq_brace', d: 'M' + x0 + ',' + braceY + ' v5 H' + x1 + ' v-5' } });
      braces.push({ t: 'text', a: { x: (x0 + x1) / 2, y: labelY, 'text-anchor': 'middle', class: 'nq_label' }, c: f.name });
    });

    return {
      t: 'svg',
      a: { viewBox: minX + ' 0 ' + (maxX - minX) + ' ' + height, width: maxX - minX, height: height,
           class: 'nq_bits', role: 'img', 'aria-label': spec.alt || 'bit layout' },
      c: [cells, idx, byteLabels, braces]
    };
  }

  // ---- Page ----------------------------------------------------------------
  function page(current, opts) {
    initStyles();
    bw.mount('#app', {
      t: 'div',
      c: [
        nav(current),
        hero(opts.title, opts.subtitle, opts.actions),
        { t: 'main', c: opts.content },
        footer()
      ]
    });
  }

  window.nbitqSite = {
    page: page, section: section, code: code, table: table, callout: callout,
    bits: bits, linkButton: linkButton, next: next, p: p_, ul: ul, c: c_, REPO: REPO,
    palette: function () { return p; }
  };
})();
