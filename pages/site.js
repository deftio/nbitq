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
  // Columns of the compiled-size tables; the rows are written by
  // tools/update_sizes.sh between the crossbuild-sizes markers.
  var SIZE_COLS = [
    { key: 'cpu', label: 'CPU' },
    { key: 'bits', label: 'bits' },
    { key: 'compiler', label: 'compiler' },
    { key: 'core', label: 'core (bytes)' },
    { key: 'u32', label: 'u32 cursor only (bytes)' },
    { key: 'nbitq32', label: 'nbitq32 (bytes)' }
  ];
  var VERSION = '0.2.2';   // kept in step by release.sh; shown beside the name

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
      // the version beside the name in the navbar: small, plain, legible
      '.nq_version': { 'font-size': '0.85em', 'font-weight': 'normal', 'opacity': '1', 'margin-left': '0.6em', 'letter-spacing': '0.02em' },
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
      '.nq_bits .nq_fx': { fill: p.surfaceAlt },
      '.nq_bits .nq_link': { stroke: p.dark.hover, 'stroke-width': '1', opacity: '0.6' },
      '.nq_bits .nq_title': { 'font-size': '11px', fill: p.dark.base },
      '.nq_bits .nq_drop': { opacity: '0.4' }
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
      brand: ['nbitq', { t: 'span', a: { class: 'nq_version' }, c: 'v' + VERSION }],
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
          'nbitq v' + VERSION + ' is BSD-2-Clause. © 2026 deftio / M. A. Chatterjee. ',
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

  // ── flow diagrams: where each bit of a value lands in the stream ──────────
  //
  // flow() draws rows of bit cells with a line from a cell in one row to a
  // cell in the next, so the reader can follow one bit of a value into the
  // byte it ends up in and back out. place(), extend() and utf8() build the
  // rows from a value, a width and a position by the same rules nbitq.c uses,
  // so the bytes in the captions are computed, not typed.
  //
  // row  = { title, cells: [{ v, cls, sub, drop }], subAbove, gaps: [cell],
  //          caps: [{ from, len, text }], offset }
  // link = { r, c, c2 }: cell c of row r to cell c2 of row r + 1.
  function flow(spec) {
    var W = spec.w || 24, H = 26, GAP = 6, PAD = 4, LINK = 30, CH = 6.4;
    var left = 0;
    spec.rows.forEach(function (row) { if (row.title) left = Math.max(left, row.title.length * CH + 10); });

    function x(row, c) {
      var g = 0;
      (row.gaps || []).forEach(function (at) { if (at <= c) g++; });
      return PAD + left + (row.offset || 0) + c * W + g * GAP;
    }

    var y = 4, geo = [], out = [], links = [], width = 0;
    spec.rows.forEach(function (row) {
      var top = y + (row.subAbove ? 12 : 0), cellY = top;
      row.cells.forEach(function (cell, c) {
        var cx = x(row, c);
        width = Math.max(width, cx + W + PAD);
        var cls = 'nq_cell nq_' + (cell.cls || 'fx') + (cell.drop ? ' nq_drop' : '');
        out.push({ t: 'rect', a: { x: cx, y: cellY, width: W, height: H, class: cls } });
        out.push({ t: 'text', a: { x: cx + W / 2, y: cellY + H / 2 + 5, 'text-anchor': 'middle', class: 'nq_bit' + (cell.drop ? ' nq_drop' : '') }, c: String(cell.v) });
        if (cell.sub !== undefined)
          out.push({ t: 'text', a: { x: cx + W / 2, y: row.subAbove ? cellY - 3 : cellY + H + 10, 'text-anchor': 'middle', class: 'nq_idx' }, c: String(cell.sub) });
      });
      if (row.title)
        out.push({ t: 'text', a: { x: PAD + left - 8, y: cellY + H / 2 + 4, 'text-anchor': 'end', class: 'nq_title' }, c: row.title });
      var bottom = cellY + H + (row.subAbove ? 0 : 12);
      (row.caps || []).forEach(function (cap) {
        var x0 = x(row, cap.from) + 2, x1 = x(row, cap.from + cap.len - 1) + W - 2, by = bottom + 4;
        out.push({ t: 'path', a: { class: 'nq_brace', d: 'M' + x0 + ',' + by + ' v5 H' + x1 + ' v-5' } });
        out.push({ t: 'text', a: { x: (x0 + x1) / 2, y: by + 18, 'text-anchor': 'middle', class: 'nq_label' }, c: cap.text });
        var half = cap.text.length * CH / 2;
        width = Math.max(width, (x0 + x1) / 2 + half + PAD);
      });
      if ((row.caps || []).length) bottom += 24;
      geo.push({ cellY: cellY, bottom: bottom });
      y = bottom + LINK;
    });
    (spec.links || []).forEach(function (l) {
      var a = spec.rows[l.r], b = spec.rows[l.r + 1];
      links.push({ t: 'line', a: { class: 'nq_link',
        x1: x(a, l.c) + W / 2, y1: geo[l.r].cellY + H,
        x2: x(b, l.c2) + W / 2, y2: geo[l.r + 1].cellY } });
    });
    var height = y - LINK + 4;
    return {
      t: 'svg',
      a: { viewBox: '0 0 ' + width + ' ' + height, width: width, height: height,
           class: 'nq_bits', role: 'img', 'aria-label': spec.alt || 'bit flow' },
      c: [links, out]
    };
  }

  function hex(b) { return (b < 16 ? '0' : '') + b.toString(16).toUpperCase(); }
  function bitOf(value, i) { return Math.floor(value / Math.pow(2, i)) % 2; }

  // The stream as nbitq.c sees it: stream bit p lives in byte p >> 3, at the
  // byte's bit 7 - (p & 7) MSB first or p & 7 LSB first. Drawn MSB on the
  // left, like a hex dump, whichever the order.
  function physical(order, p) { return { byte: p >> 3, bit: order === 'lsb' ? (p & 7) : 7 - (p & 7) }; }
  function cellOf(order, p) { var ph = physical(order, p); return ph.byte * 8 + 7 - ph.bit; }
  function streamIndex(order, byte, k) { return byte * 8 + (order === 'lsb' ? 7 - k : k); }

  function streamRow(order, before, after, written, title, colour) {
    var cells = [], gaps = [], caps = [];
    after.forEach(function (b, j) {
      if (j) gaps.push(j * 8);
      for (var k = 0; k < 8; k++) {
        var idx = streamIndex(order, j, k), w = written(idx);
        cells.push({ v: (b >> (7 - k)) & 1, cls: w === false ? 'fx' : (colour ? colour(w) : 'f0'), sub: idx });
      }
      caps.push({ from: j * 8, len: 8, text: 'byte ' + j + ': ' + (before ? hex(before[j]) + ' → ' : '') + hex(b) });
    });
    return { title: title, cells: cells, gaps: gaps, caps: caps };
  }

  // place(): o = { value, n, pos, order, before, show, alt }
  // The value's bits above, the bytes below, a line per bit. `show` draws
  // more bits of the value than the field holds, to show truncation.
  function place(o) {
    var order = o.order || 'msb', show = o.show || o.n, after = o.before.slice();
    var vcells = [], links = [];
    for (var i = show - 1; i >= 0; i--)
      vcells.push({ v: bitOf(o.value, i), cls: i < o.n ? 'f0' : 'fx', drop: i >= o.n, sub: 'v' + i });
    for (var j = 0; j < o.n; j++) {
      var p = o.pos + j, vb = order === 'lsb' ? j : o.n - 1 - j, ph = physical(order, p);
      after[ph.byte] = (after[ph.byte] & ~(1 << ph.bit)) | (bitOf(o.value, vb) << ph.bit);
      links.push({ r: 0, c: show - 1 - vb, c2: cellOf(order, p) });
    }
    var written = function (idx) { return idx >= o.pos && idx < o.pos + o.n ? 0 : false; };
    var srow = streamRow(order, o.before, after, written, 'stream, ' + (order === 'lsb' ? 'LSB' : 'MSB') + ' first');
    var vrow = { title: 'value ' + o.value + (show > o.n ? ', low ' + o.n + ' bits written' : ', ' + o.n + ' bits'), cells: vcells, subAbove: true };
    if (show > o.n) vrow.caps = [{ from: 0, len: show - o.n, text: 'not written' }];
    // Centre the value over the cells it lands in; when it is wider than
    // the field (truncation), move the stream under its low bits instead.
    var lo = 1e9, hi = -1e9;
    links.forEach(function (l) { lo = Math.min(lo, l.c2); hi = Math.max(hi, l.c2); });
    var W = 24, GAP = 6;
    var sx0 = lo * W + (lo >> 3) * GAP, sx1 = (hi + 1) * W + (hi >> 3) * GAP;
    var off = Math.round((sx0 + sx1) / 2 - show * W / 2);
    if (off >= 0) vrow.offset = off; else srow.offset = -off;
    return flow({ alt: o.alt, rows: [vrow, srow], links: links });
  }

  // extend(): o = { bits (string, MSB first), alt }
  // The field between the two readings of it: sign-extended above, zero-
  // extended below, both to 32 bits.
  function extend(o) {
    var n = o.bits.length, sign = o.bits.charAt(0) === '1', W = 32, CW = 16;
    function reg(fill, title, above) {
      var cells = [], gaps = [8, 16, 24];
      for (var i = W - 1; i >= 0; i--) {
        var inField = i < n;
        cells.push({ v: inField ? o.bits.charAt(n - 1 - i) : fill, cls: inField ? 'f0' : (fill ? 'f1' : 'fx'), sub: i });
      }
      return { title: title, cells: cells, gaps: gaps, subAbove: above };
    }
    var field = { title: n + '-bit field', cells: [], offset: (W - n) * CW + 3 * 6 };
    for (var i = n - 1; i >= 0; i--) field.cells.push({ v: o.bits.charAt(n - 1 - i), cls: 'f0' });
    var sval = parseInt(o.bits, 2) - (sign ? Math.pow(2, n) : 0), uval = parseInt(o.bits, 2);
    var sreg = reg(sign ? 1 : 0, 'int32_t ' + sval, true);
    var ureg = reg(0, 'uint32_t ' + uval, false);
    var links = [];
    for (var k = 0; k < n; k++) { links.push({ r: 0, c: W - n + k, c2: k }); links.push({ r: 1, c: k, c2: W - n + k }); }
    return flow({ alt: o.alt, w: CW, rows: [sreg, field, ureg], links: links });
  }

  // utf8(): o = { cp, pos, order, alt }
  // Code point bits, the UTF-8 bytes they fill, and those bytes in the stream.
  function utf8(o) {
    var cp = o.cp, order = o.order || 'msb', pos = o.pos || 0;
    var lead, nb, payload;
    if (cp < 0x80) { nb = 1; lead = 0x00; payload = 7; }
    else if (cp < 0x800) { nb = 2; lead = 0xC0; payload = 11; }
    else if (cp < 0x10000) { nb = 3; lead = 0xE0; payload = 16; }
    else { nb = 4; lead = 0xF0; payload = 21; }
    var bytes = [], slots = [];               // slots: payload cells of the byte row, high bit first
    if (nb === 1) { bytes.push(cp); for (var i = 1; i < 8; i++) slots.push(i); }
    else {
      var first = lead | (cp >> (6 * (nb - 1)));
      bytes.push(first);
      for (var i = nb + 1; i < 8; i++) slots.push(i);      // after the 1..10 prefix, nb + 1 bits
      for (var k = nb - 2; k >= 0; k--) {
        bytes.push(0x80 | ((cp >> (6 * k)) & 0x3F));
        for (var b = 2; b < 8; b++) slots.push((nb - 1 - k) * 8 + b);
      }
    }
    var cprow = { title: 'U+' + cp.toString(16).toUpperCase() + ', ' + payload + ' bits', cells: [], subAbove: true };
    for (var i = payload - 1; i >= 0; i--) cprow.cells.push({ v: bitOf(cp, i), cls: 'f3', sub: i });
    var brow = { title: 'UTF-8 bytes ' + bytes.map(hex).join(' '), cells: [], gaps: [] };
    bytes.forEach(function (b, j) {
      if (j) brow.gaps.push(j * 8);
      for (var k = 0; k < 8; k++) brow.cells.push({ v: (b >> (7 - k)) & 1, cls: slots.indexOf(j * 8 + k) >= 0 ? 'f' + (j % 3) : 'fx' });
    });
    var total = Math.ceil((pos + nb * 8) / 8), after = [];
    for (var j = 0; j < total; j++) after.push(o.before ? o.before[j] : 0);
    var links = [];
    slots.forEach(function (slot, i) { links.push({ r: 0, c: i, c2: slot }); });
    // Each byte is an 8-bit field in the stream's order: MSB first its top
    // bit goes to the lowest stream index, LSB first its bit 0 does.
    for (var k = 0; k < nb; k++)
      for (var b = 7; b >= 0; b--) {
        var p = pos + k * 8 + (order === 'lsb' ? b : 7 - b), ph = physical(order, p);
        after[ph.byte] = (after[ph.byte] & ~(1 << ph.bit)) | (((bytes[k] >> b) & 1) << ph.bit);
        links.push({ r: 1, c: k * 8 + 7 - b, c2: cellOf(order, p) });
      }
    var written = function (idx) { return idx >= pos && idx < pos + nb * 8 ? ((idx - pos) >> 3) : false; };
    var srow = streamRow(order, o.before || null, after, written, 'stream, ' + (order === 'lsb' ? 'LSB' : 'MSB') + ' first, from bit ' + pos,
                         function (k) { return 'f' + (k % 3); });
    // centre the upper rows over the stream
    var W = 20, GAP = 6;
    brow.offset = order === 'lsb' ? 0 : pos * W + (pos >> 3) * GAP;   // byte row cell 0 above stream bit pos
    cprow.offset = brow.offset + Math.round((nb * 8 * W + (nb - 1) * GAP - payload * W) / 2);
    return flow({ alt: o.alt, w: W, rows: [cprow, brow, srow], links: links });
  }

  window.nbitqSite = {
    page: page, section: section, code: code, table: table, callout: callout,
    bits: bits, flow: flow, place: place, extend: extend, utf8: utf8, linkButton: linkButton, next: next, p: p_, ul: ul, c: c_, REPO: REPO,
    SIZE_COLS: SIZE_COLS,
    palette: function () { return p; }
  };
})();
