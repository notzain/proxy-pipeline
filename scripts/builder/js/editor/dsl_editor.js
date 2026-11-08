/**
 * dsl_editor.js - Generic Schema-Driven LSP & Code Editor Engine
 *
 * Provides:
 * 1. Syntax Tokenization & Highlighting driven by handler property DSL specs.
 * 2. Intelligent Auto-completion (IntelliSense) for keywords, functions, and metadata scopes.
 * 3. Dynamic metadata resolution from /api/metadata.
 * 4. Inline diagnostic linting for expression balance and unclosed tokens.
 */

(function (global) {
  'use strict';

  let metadataCache = null;
  const payloadContractsCache = {};

  async function fetchPayloadContract(format, filePath) {
    const key = `${format}:${filePath}`;
    if (payloadContractsCache[key]) return payloadContractsCache[key];
    try {
      const res = await fetch(`/api/payload_contract?format=${encodeURIComponent(format)}&file=${encodeURIComponent(filePath)}`);
      if (res.ok) {
        payloadContractsCache[key] = await res.json();
        return payloadContractsCache[key];
      }
    } catch (e) {
      console.warn('[DSLEditor] Could not fetch payload contract:', e);
    }
    return null;
  }

  async function fetchMetadata() {
    if (metadataCache) return metadataCache;
    try {
      const res = await fetch('/api/metadata');
      if (res.ok) {
        metadataCache = await res.json();
      }
    } catch (e) {
      console.warn('[DSLEditor] Could not fetch /api/metadata:', e);
      metadataCache = { protocols: [], common_fields: [] };
    }
    return metadataCache || { protocols: [], common_fields: [] };
  }

  // Pre-fetch metadata in the background
  fetchMetadata();

  function escapeHtml(str) {
    if (!str) return '';
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  class DSLEditor {
    constructor(containerEl, options = {}) {
      this.container = containerEl;
      this.options = options;
      this.dsl = options.dsl || { type: 'expression' };
      this.value = options.value || '';
      this.onChange = options.onChange || (() => {});
      this.activeCompletionIdx = 0;
      this.completions = [];
      this.completionOpen = false;

      this.initDom();
      this.bindEvents();
      this.updateView();
    }

    setDsl(dsl) {
      this.dsl = dsl || { type: 'expression' };
      this.updateView();
    }

    setValue(val) {
      if (val === undefined || val === null) val = '';
      if (this.input.value !== val) {
        this.input.value = val;
        this.value = val;
        this.updateView();
      }
    }

    initDom() {
      this.container.innerHTML = `
        <div class="dsl-editor-container">
          <div class="dsl-editor-wrapper">
            <div class="dsl-editor-backdrop"></div>
            <textarea class="dsl-editor-input" spellcheck="false" rows="${this.options.rows || 2}" placeholder="${escapeHtml(this.options.placeholder || '')}"></textarea>
          </div>
          <div class="dsl-completion-popup" style="display: none;"></div>
          <div class="dsl-editor-diagnostics" style="display: none;"></div>
        </div>
      `;

      this.wrapper = this.container.querySelector('.dsl-editor-container');
      this.backdrop = this.container.querySelector('.dsl-editor-backdrop');
      this.input = this.container.querySelector('.dsl-editor-input');
      this.popup = this.container.querySelector('.dsl-completion-popup');
      this.diagBar = this.container.querySelector('.dsl-editor-diagnostics');

      this.input.value = this.value;
    }

    bindEvents() {
      this.input.addEventListener('input', () => {
        this.value = this.input.value;
        this.updateView();
        this.onChange(this.value);
        this.checkCompletions();
      });

      this.input.addEventListener('scroll', () => {
        this.backdrop.scrollTop = this.input.scrollTop;
        this.backdrop.scrollLeft = this.input.scrollLeft;
      });

      this.input.addEventListener('keydown', (e) => {
        if (this.completionOpen) {
          if (e.key === 'ArrowDown') {
            e.preventDefault();
            this.activeCompletionIdx = (this.activeCompletionIdx + 1) % this.completions.length;
            this.renderPopup();
            return;
          }
          if (e.key === 'ArrowUp') {
            e.preventDefault();
            this.activeCompletionIdx = (this.activeCompletionIdx - 1 + this.completions.length) % this.completions.length;
            this.renderPopup();
            return;
          }
          if (e.key === 'Enter' || e.key === 'Tab') {
            e.preventDefault();
            this.applyCompletion(this.completions[this.activeCompletionIdx]);
            return;
          }
          if (e.key === 'Escape') {
            e.preventDefault();
            this.closePopup();
            return;
          }
        }

        // Tab indent support
        if (e.key === 'Tab' && !this.completionOpen) {
          e.preventDefault();
          const start = this.input.selectionStart;
          const end = this.input.selectionEnd;
          this.input.value = this.input.value.substring(0, start) + '  ' + this.input.value.substring(end);
          this.input.selectionStart = this.input.selectionEnd = start + 2;
          this.input.dispatchEvent(new Event('input'));
        }
      });

      this.input.addEventListener('blur', () => {
        // Small delay to allow click on completion popup
        setTimeout(() => this.closePopup(), 150);
      });

      this.input.addEventListener('click', () => {
        this.checkCompletions();
      });
    }

    updateView() {
      // 1. Syntax Highlighting in Backdrop
      const highlighted = this.highlightSyntax(this.input.value);
      this.backdrop.innerHTML = highlighted + '\n';

      // 2. Linting & Diagnostics
      const diag = this.lint(this.input.value);
      if (diag) {
        this.wrapper.classList.add('has-error');
        this.diagBar.style.display = 'flex';
        this.diagBar.innerHTML = `<span>⚠ ${escapeHtml(diag)}</span>`;
      } else {
        this.wrapper.classList.remove('has-error');
        this.diagBar.style.display = 'none';
        this.diagBar.innerHTML = '';
      }
    }

    highlightSyntax(code) {
      if (!code) return '';

      const functions = (this.dsl.functions || []).map((f) => f.name);
      const keywords = this.dsl.keywords || ['function', 'return', 'end', 'if', 'then', 'else', 'local', 'nil', 'true', 'false'];
      const operators = this.dsl.operators || ['and', 'or', 'not', '&&', '||', '!', '==', '!=', '<', '<=', '>', '>='];
      const scopes = (this.dsl.scopes || []).map((s) => s.name);
      const globals = (this.dsl.globals || []).map((g) => g.name);

      // Simple regex tokenizer for expressions and scripts
      const tokenRegex = /(--.*?$|\/\/.*?$)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(\b\d+(?:\.\d+)?\b)|(\b[a-zA-Z_][a-zA-Z0-9_]*\b)|(==|!=|<=|>=|&&|\|\||[<>!+\-*/=(),.])/gm;

      return code.replace(tokenRegex, (match, comment, str, num, ident, punct) => {
        if (comment) {
          return `<span class="dsl-tok-comment">${escapeHtml(comment)}</span>`;
        }
        if (str) {
          return `<span class="dsl-tok-string">${escapeHtml(str)}</span>`;
        }
        if (num) {
          return `<span class="dsl-tok-number">${escapeHtml(num)}</span>`;
        }
        if (ident) {
          if (functions.includes(ident)) {
            return `<span class="dsl-tok-function">${escapeHtml(ident)}</span>`;
          }
          if (keywords.includes(ident)) {
            return `<span class="dsl-tok-keyword">${escapeHtml(ident)}</span>`;
          }
          if (operators.includes(ident)) {
            return `<span class="dsl-tok-operator">${escapeHtml(ident)}</span>`;
          }
          if (scopes.includes(ident) || globals.includes(ident)) {
            return `<span class="dsl-tok-scope">${escapeHtml(ident)}</span>`;
          }
          return `<span class="dsl-tok-member">${escapeHtml(ident)}</span>`;
        }
        if (punct) {
          if (operators.includes(punct)) {
            return `<span class="dsl-tok-operator">${escapeHtml(punct)}</span>`;
          }
          return escapeHtml(punct);
        }
        return escapeHtml(match);
      });
    }

    lint(code) {
      if (!code.trim()) return null;

      // Parenthesis check
      let parenCount = 0;
      let inString = false;
      let quoteChar = '';

      for (let i = 0; i < code.length; i++) {
        const c = code[i];
        if (inString) {
          if (c === quoteChar && code[i - 1] !== '\\') {
            inString = false;
          }
          continue;
        }
        if (c === '"' || c === "'") {
          inString = true;
          quoteChar = c;
          continue;
        }
        if (c === '(') parenCount++;
        if (c === ')') parenCount--;
        if (parenCount < 0) return 'Unexpected closing parenthesis ")"';
      }

      if (inString) return `Unclosed string literal starting with ${quoteChar}`;
      if (parenCount > 0) return 'Unclosed parenthesis "("';

      // Dangling logical operators at end of line (e.g. "x == 1 and")
      const trimmed = code.trim();
      if (/\b(and|or|not|&&|\|\|)\s*$/i.test(trimmed)) {
        return 'Incomplete expression: trailing logical operator';
      }

      return null;
    }

    async checkCompletions() {
      const pos = this.input.selectionStart;
      const text = this.input.value.substring(0, pos);

      // Check context before cursor
      // Match scoped pattern: e.g. "metadata." or "msg." or "msg.meta."
      const scopedMatch = text.match(/([a-zA-Z_][a-zA-Z0-9_]*(?:\.[a-zA-Z_][a-zA-Z0-9_]*)*)\.([a-zA-Z0-9_]*)$/);
      // Match bare identifier typing: e.g. "con"
      const wordMatch = text.match(/([a-zA-Z_][a-zA-Z0-9_]*)$/);

      let items = [];

      if (scopedMatch) {
        const scopePath = scopedMatch[1];
        const prefix = scopedMatch[2].toLowerCase();
        items = await this.getScopedCompletions(scopePath, prefix);
        this.replaceRange = [pos - prefix.length, pos];
      } else if (wordMatch) {
        const prefix = wordMatch[1].toLowerCase();
        items = await this.getRootCompletions(prefix);
        this.replaceRange = [pos - prefix.length, pos];
      } else {
        // Trigger root items if trailing space or empty
        this.closePopup();
        return;
      }

      if (items.length > 0) {
        this.completions = items;
        this.activeCompletionIdx = 0;
        this.openPopup();
      } else {
        this.closePopup();
      }
    }

    async getRootCompletions(prefix) {
      const res = [];

      // 1. Functions from DSL
      for (const fn of this.dsl.functions || []) {
        if (!prefix || fn.name.toLowerCase().startsWith(prefix)) {
          res.push({
            name: fn.name,
            insertText: fn.name + '()',
            type: fn.return || 'function',
            badge: 'fn',
            doc: fn.doc || fn.signature || ''
          });
        }
      }

      // 2. Scopes (e.g. metadata, payload, xml)
      for (const sc of this.dsl.scopes || []) {
        if (!prefix || sc.name.toLowerCase().startsWith(prefix)) {
          res.push({
            name: sc.name,
            insertText: sc.name,
            type: 'scope',
            badge: 'field',
            doc: sc.doc || 'Scope namespace'
          });
        }
      }

      // Also auto-suggest 'payload' scope if any upstream node declares a payloadContract
      const upstream = this.options.upstreamContributions || [];
      const hasPayloadContract = upstream.some((u) => u.payloadContract);
      if (hasPayloadContract) {
        if (!prefix || 'payload'.startsWith(prefix)) {
          res.push({
            name: 'payload',
            insertText: 'payload',
            type: 'scope',
            badge: 'field',
            doc: 'Verified message payload elements from upstream schema'
          });
        }
      }

      // 3. Script globals (e.g. msg)
      for (const g of this.dsl.globals || []) {
        if (!prefix || g.name.toLowerCase().startsWith(prefix)) {
          res.push({
            name: g.name,
            insertText: g.name,
            type: g.type || 'object',
            badge: 'var',
            doc: g.doc || 'Global object'
          });
        }
      }

      // 4. Keywords
      for (const kw of this.dsl.keywords || []) {
        if (kw.toLowerCase().startsWith(prefix)) {
          res.push({
            name: kw,
            insertText: kw,
            type: 'keyword',
            badge: 'kw',
            doc: `Keyword: ${kw}`
          });
        }
      }

      // 5. Lua Standard Library Globals & Modules (if DSL type is lua)
      if (this.dsl.type === 'lua' && window.LUA_STDLIB) {
        // Base library functions (e.g. assert, tostring, tonumber, print, pairs, ipairs)
        for (const fn of window.LUA_STDLIB.base || []) {
          if (!prefix || fn.name.toLowerCase().startsWith(prefix)) {
            res.push({
              name: fn.name,
              insertText: fn.name + '()',
              type: fn.return || 'function',
              badge: 'fn',
              doc: fn.doc || fn.signature || ''
            });
          }
        }
        // Standard modules (e.g. string, table, math)
        for (const [modName, modInfo] of Object.entries(window.LUA_STDLIB.modules || {})) {
          if (!prefix || modName.toLowerCase().startsWith(prefix)) {
            res.push({
              name: modName,
              insertText: modName,
              type: 'module',
              badge: 'mod',
              doc: modInfo.doc || `Lua ${modName} module`
            });
          }
        }
      }

      return res;
    }

    async getScopedCompletions(scopePath, prefix) {
      const res = [];
      const meta = await fetchMetadata();

      // Case 1: "metadata.<...>" (or "meta.<...>")
      if (scopePath === 'metadata' || scopePath === 'meta') {
        const upstream = this.options.upstreamContributions || [];
        const hasWires = Boolean(this.options.hasUpstreamWires);
        const protocols = meta.protocols || [];

        // If the node is connected in a chain, ONLY expose what is contributed by its upstream handlers
        if (hasWires) {
          for (const item of upstream) {
            const proto = item.contributions && item.contributions.protocol;
            if (proto) {
              if (!prefix || proto.name.toLowerCase().startsWith(prefix)) {
                res.push({
                  name: proto.name,
                  insertText: proto.name,
                  type: 'namespace',
                  badge: 'mod',
                  doc: `${proto.doc || `Protocol namespace ${proto.name}`} [From upstream node "${item.nodeName}"]`
                });
              }

              for (const f of proto.fields || []) {
                if (!prefix || f.name.toLowerCase().startsWith(prefix)) {
                  res.push({
                    name: f.name,
                    insertText: f.name,
                    type: f.type,
                    badge: 'field',
                    doc: `${f.doc || ''} [From "${item.nodeName}" (${proto.name})]`
                  });
                }
              }
            }
          }
          return res;
        }

        // Only when the node has NO upstream wires at all (unconnected on canvas),
        // list known namespaces as a design-time convenience
        for (const p of protocols) {
          if (!prefix || p.name.toLowerCase().startsWith(prefix)) {
            res.push({
              name: p.name,
              insertText: p.name,
              type: 'namespace',
              badge: 'mod',
              doc: `${p.doc || `Protocol namespace ${p.name}`} (ID: ${p.id})`
            });
          }
        }
        return res;
      }

      // Case 1b: Namespaced protocol subfield: e.g. "metadata.udp.<field>" or "meta.udp.<field>"
      const metaProtoMatch = scopePath.match(/^(?:metadata|meta|msg\.meta)\.([a-zA-Z0-9_]+)$/);
      if (metaProtoMatch) {
        const protoName = metaProtoMatch[1].toLowerCase();
        const upstream = this.options.upstreamContributions || [];

        // Check if an upstream node contributed this protocol
        for (const item of upstream) {
          const proto = item.contributions && item.contributions.protocol;
          if (proto && proto.name.toLowerCase() === protoName && proto.fields) {
            for (const f of proto.fields) {
              if (!prefix || f.name.toLowerCase().startsWith(prefix)) {
                res.push({
                  name: f.name,
                  insertText: f.name,
                  type: f.type,
                  badge: 'field',
                  doc: `${f.doc || ''} [Contributed by "${item.nodeName}"]`
                });
              }
            }
            return res;
          }
        }

        // Otherwise check global schema protocols
        const protoDef = (meta.protocols || []).find((p) => p.name.toLowerCase() === protoName);
        if (protoDef && protoDef.fields) {
          for (const f of protoDef.fields) {
            if (!prefix || f.name.toLowerCase().startsWith(prefix)) {
              res.push({
                name: f.name,
                insertText: f.name,
                type: f.type,
                badge: 'field',
                doc: `${f.doc || ''} [Protocol: ${protoName.toUpperCase()}]`
              });
            }
          }
          return res;
        }
      }

      // Case 2: Payload Schema Scope (e.g. "payload.<...>", "xml.<...>", "msg.xml.<...>")
      const isPayloadScope = scopePath === 'payload' || scopePath === 'xml' || scopePath.startsWith('payload.') || scopePath.startsWith('xml.') || scopePath === 'msg.xml' || scopePath.startsWith('msg.xml.');
      if (isPayloadScope) {
        const upstream = this.options.upstreamContributions || [];
        const payloadContributors = upstream.filter((u) => u.payloadContract);

        for (const item of payloadContributors) {
          const contract = item.payloadContract;
          const paramKey = contract.sourceParam;
          const filePath = item.config[paramKey] || '';
          if (filePath) {
            const schemaData = await fetchPayloadContract(contract.format, filePath);
            if (schemaData && schemaData.rootElements) {
              // Resolve nested path (e.g. payload.order.customer or payload.order)
              let currentElements = schemaData.rootElements;

              // Strip root scope prefix e.g. "payload." or "xml." or "msg.xml."
              let subPath = scopePath.replace(/^(payload|xml|msg\.xml)\.?/, '');
              if (subPath) {
                const parts = subPath.split('.');
                for (const part of parts) {
                  if (!part) continue;
                  // Handle array indexing like Order[0] -> Order
                  const cleanPart = part.replace(/\[\d*\]$/, '');
                  const found = currentElements.find((el) => el.name === cleanPart);
                  if (found && found.children) {
                    currentElements = found.children;
                  } else {
                    currentElements = [];
                    break;
                  }
                }
              }

              for (const elem of currentElements) {
                if (!prefix || elem.name.toLowerCase().startsWith(prefix)) {
                  const typeLabel = elem.isArray ? `${elem.itemType || 'element'}[]` : elem.type;
                  res.push({
                    name: elem.name,
                    insertText: elem.name,
                    type: typeLabel,
                    badge: elem.isArray ? 'mod' : 'field',
                    doc: `${elem.doc || ''} [From "${item.nodeName}" (${contract.format.toUpperCase()})]`
                  });
                }
              }
            }
          }
        }
        return res;
      }
      if (scopePath === 'msg') {
        const globalMsg = (this.dsl.globals || []).find((g) => g.name === 'msg');
        const members = globalMsg ? globalMsg.members : [
          { name: 'payload', type: 'string', doc: 'Raw message payload string' },
          { name: 'protocol', type: 'number', doc: 'Numeric protocol identifier' },
          { name: 'port', type: 'number', doc: 'Originating client UDP port' },
          { name: 'meta', type: 'object', doc: 'Parsed protocol metadata' }
        ];

        for (const m of members) {
          if (!prefix || m.name.toLowerCase().startsWith(prefix)) {
            res.push({
              name: m.name,
              insertText: m.name,
              type: m.type,
              badge: 'var',
              doc: m.doc || ''
            });
          }
        }
        return res;
      }

      if (scopePath === 'msg.meta') {
        const upstream = this.options.upstreamContributions || [];
        const hasWires = Boolean(this.options.hasUpstreamWires);
        const protocols = meta.protocols || [];

        // If the node is connected in a chain, ONLY expose what is contributed by its upstream handlers
        if (hasWires) {
          for (const item of upstream) {
            const proto = item.contributions && item.contributions.protocol;
            if (proto) {
              if (!prefix || proto.name.toLowerCase().startsWith(prefix)) {
                res.push({
                  name: proto.name,
                  insertText: proto.name,
                  type: 'namespace',
                  badge: 'mod',
                  doc: `${proto.doc || `Protocol namespace ${proto.name}`} [From upstream node "${item.nodeName}"]`
                });
              }

              for (const f of proto.fields || []) {
                if (!prefix || f.name.toLowerCase().startsWith(prefix)) {
                  res.push({
                    name: f.name,
                    insertText: f.name,
                    type: f.type,
                    badge: 'field',
                    doc: `${f.doc || ''} [From "${item.nodeName}" (${proto.name})]`
                  });
                }
              }
            }
          }
          return res;
        }

        // Only when unconnected, list known namespaces as a design-time fallback
        for (const p of protocols) {
          if (!prefix || p.name.toLowerCase().startsWith(prefix)) {
            res.push({
              name: p.name,
              insertText: p.name,
              type: 'namespace',
              badge: 'mod',
              doc: `${p.doc || `Protocol namespace ${p.name}`} (ID: ${p.id})`
            });
          }
        }
        return res;
      }

      // Case 3: Lua Standard Modules (e.g. string.sub, table.insert, math.max)
      if (this.dsl.type === 'lua' && window.LUA_STDLIB && window.LUA_STDLIB.modules) {
        const mod = window.LUA_STDLIB.modules[scopePath];
        if (mod && mod.members) {
          for (const m of mod.members) {
            if (!prefix || m.name.toLowerCase().startsWith(prefix)) {
              const isFn = m.return !== undefined && m.signature && m.signature.includes('(');
              res.push({
                name: m.name,
                insertText: isFn ? m.name + '()' : m.name,
                type: m.return || 'any',
                badge: isFn ? 'fn' : 'field',
                doc: `${m.signature || m.name} : ${m.return || ''}\n${m.doc || ''}`
              });
            }
          }
          return res;
        }
      }

      return res;
    }

    openPopup() {
      this.completionOpen = true;
      this.renderPopup();
      this.popup.style.display = 'block';
    }

    closePopup() {
      this.completionOpen = false;
      this.popup.style.display = 'none';
    }

    renderPopup() {
      if (!this.completions.length) {
        this.closePopup();
        return;
      }

      let html = '';
      this.completions.forEach((item, idx) => {
        const active = idx === this.activeCompletionIdx ? 'active' : '';
        const badgeClass = `dsl-badge-${item.badge || 'fn'}`;
        html += `
          <div class="dsl-completion-item ${active}" data-idx="${idx}">
            <div class="dsl-completion-left">
              <span class="dsl-completion-badge ${badgeClass}">${item.badge || 'tok'}</span>
              <span class="dsl-completion-name">${escapeHtml(item.name)}</span>
            </div>
            <span class="dsl-completion-type">${escapeHtml(item.type || '')}</span>
          </div>
        `;
      });

      const current = this.completions[this.activeCompletionIdx];
      if (current && current.doc) {
        html += `<div class="dsl-completion-doc">${escapeHtml(current.doc)}</div>`;
      }

      this.popup.innerHTML = html;

      // Click handling on popup items
      this.popup.querySelectorAll('.dsl-completion-item').forEach((el) => {
        el.addEventListener('mousedown', (e) => {
          e.preventDefault();
          const idx = parseInt(el.getAttribute('data-idx'), 10);
          this.applyCompletion(this.completions[idx]);
        });
      });
    }

    applyCompletion(item) {
      if (!item) return;
      const [start, end] = this.replaceRange || [this.input.selectionStart, this.input.selectionStart];
      const val = this.input.value;
      const insert = item.insertText || item.name;

      this.input.value = val.substring(0, start) + insert + val.substring(end);
      const newPos = start + insert.length;
      // If function, place cursor inside parenthesis e.g. "contains()" -> "contains(|)"
      if (insert.endsWith('()')) {
        this.input.selectionStart = this.input.selectionEnd = newPos - 1;
      } else {
        this.input.selectionStart = this.input.selectionEnd = newPos;
      }

      this.value = this.input.value;
      this.closePopup();
      this.updateView();
      this.onChange(this.value);
      this.input.focus();

      // If user accepted a completion that is followed by typing a trigger character (like '.'),
      // or if they typed a dot, checkCompletions automatically re-triggers as in VS Code.
      this.checkCompletions();
    }
  }

  global.DSLEditor = DSLEditor;
})(window);
