/**
 * builder_app.js - Application state, schema binding, TOML export/import, and live validation.
 */

function createApp() {
  return {
    schemas: {},
    schemaCategories: {},
    collapsedCategories: {},
    searchQuery: '',
    chainName: 'custom_chain',
    saveFilename: 'custom_chain.toml',
    showSaveModal: false,
    showExportModal: false,
    backend: 'file',
    globalConfig: {
      'file.path': '/tmp/proxy_watch',
      'log.console': true,
    },

    nodes: [],
    wires: [],

    selectedNodeId: null,
    selectedNode: null,
    selectedSchema: null,
    selectedWireId: null,

    validation: {
      valid: true,
      errors: [],
      warnings: [],
    },

    statusMsg: '',
    canvasInstance: null,

    async init() {
      await this.loadSchemas();
      this.initCanvas();
      await this.loadInitialChain();
      this.validateChain();

      // Keyboard shortcuts
      window.addEventListener('keydown', (e) => {
        if (e.key === 'Delete' || e.key === 'Backspace') {
          // If input/textarea active, do not delete node
          if (['INPUT', 'TEXTAREA', 'SELECT'].includes(document.activeElement.tagName)) return;
          if (this.selectedNodeId) {
            this.deleteNode(this.selectedNodeId);
          } else if (this.selectedWireId) {
            this.deleteWire(this.selectedWireId);
          }
        }
      });
    },

    async loadSchemas() {
      try {
        const res = await fetch('/api/schemas');
        if (res.ok) {
          this.schemas = await res.json();
          this.categorizeSchemas();
        }
      } catch (err) {
        console.error('Failed to load handler schemas:', err);
      }
    },

    categorizeSchemas() {
      const cats = {};
      for (const [name, schema] of Object.entries(this.schemas)) {
        const cat = schema.category || 'Processors';
        if (!cats[cat]) cats[cat] = [];
        cats[cat].push(schema);
      }
      this.schemaCategories = cats;
    },

    get filteredSchemaCategories() {
      const q = (this.searchQuery || '').trim().toLowerCase();
      if (!q) return this.schemaCategories;

      const filtered = {};
      for (const [cat, handlers] of Object.entries(this.schemaCategories)) {
        const matches = handlers.filter((h) => {
          const name = (h.name || '').toLowerCase();
          const disp = (h.displayName || '').toLowerCase();
          const role = (h.role || '').toLowerCase();
          const desc = (h.description || '').toLowerCase();
          return name.includes(q) || disp.includes(q) || role.includes(q) || desc.includes(q);
        });
        if (matches.length > 0) {
          filtered[cat] = matches;
        }
      }
      return filtered;
    },

    get filteredHandlersCount() {
      let count = 0;
      for (const handlers of Object.values(this.filteredSchemaCategories)) {
        count += handlers.length;
      }
      return count;
    },

    toggleCategory(category) {
      this.collapsedCategories[category] = !this.collapsedCategories[category];
    },

    initCanvas() {
      const container = document.getElementById('canvas-container');
      const viewport = document.getElementById('canvas-viewport');
      const svg = document.getElementById('canvas-svg');
      this.canvasInstance = new BuilderCanvas(container, viewport, svg, this);
    },

    async loadInitialChain() {
      try {
        const res = await fetch('/api/chain');
        if (res.ok) {
          const chainDef = await res.json();
          if (chainDef && chainDef.handlers && chainDef.handlers.length > 0) {
            this.importChainDefinition(chainDef);
          }
        }
      } catch (err) {
        // No chain preloaded
      }
    },

    createNodeFromSchema(handlerType, x, y) {
      const schema = this.schemas[handlerType];
      if (!schema) return;

      const baseName = handlerType.replace('_handler', '');
      let counter = 1;
      let candidateName = `${baseName}_${counter}`;
      while (this.nodes.some((n) => n.name === candidateName)) {
        counter++;
        candidateName = `${baseName}_${counter}`;
      }

      // Initialize config with schema defaults
      const config = {};
      if (schema.parameters) {
        for (const [k, p] of Object.entries(schema.parameters)) {
          if (p.default !== undefined) {
            config[k] = p.default;
          }
        }
      }

      const newNode = {
        id: 'node_' + Math.random().toString(36).substr(2, 9),
        name: candidateName,
        type: handlerType,
        x: x || 250,
        y: y || 150,
        config: config,
        inputs: {},
        outputs: {},
      };

      // Set default channel names from schema
      if (schema.ports && schema.ports.inputs) {
        for (const p of schema.ports.inputs) {
          if (p.cardinality === 'multiple') {
            const defs = (p.default || 'in_channel').split(',').map((s) => s.trim()).filter(Boolean);
            newNode.inputs[p.name] = defs.length > 0 ? defs : ['in_channel'];
          } else {
            newNode.inputs[p.name] = p.default || 'in_channel';
          }
        }
      }
      if (schema.ports && schema.ports.outputs) {
        for (const p of schema.ports.outputs) {
          if (p.cardinality === 'multiple') {
            const defs = (p.default || 'out_channel').split(',').map((s) => s.trim()).filter(Boolean);
            newNode.outputs[p.name] = defs.length > 0 ? defs : ['out_channel'];
          } else {
            newNode.outputs[p.name] = p.default || 'out_channel';
          }
        }
      }

      this.nodes.push(newNode);
      this.selectNode(newNode.id);
      this.validateChain();
    },

    selectNode(id) {
      this.selectedNodeId = id;
      this.selectedNode = this.nodes.find((n) => n.id === id) || null;
      this.selectedSchema = this.selectedNode ? (this.schemas[this.selectedNode.type] || null) : null;
      this.selectedWireId = null;
    },

    selectWire(id) {
      this.selectedWireId = id;
      this.selectedNodeId = null;
      this.selectedNode = null;
      this.selectedSchema = null;
      this.updateWires();
    },

    getSelectedWire() {
      return this.wires.find((w) => w.id === this.selectedWireId) || null;
    },

    updateWireChannelName(wireId, newName) {
      const wire = this.wires.find((w) => w.id === wireId);
      if (!wire) return;
      const cleanName = (newName || '').trim();

      // Check wire name uniqueness across all other wires
      const isDuplicate = this.wires.some((w) => w.id !== wireId && w.channel === cleanName);
      if (cleanName && isDuplicate) {
        this.flashStatus(`⚠️ Warning: Channel '${cleanName}' is already used by another wire!`);
      }

      wire.channel = newName;

      // Automatically update source node output
      const fromNode = this.nodes.find((n) => n.id === wire.fromNodeId);
      if (fromNode && fromNode.outputs) {
        const outVal = fromNode.outputs[wire.fromPort];
        if (Array.isArray(outVal)) {
          outVal[wire.fromIndex || 0] = newName;
        } else {
          fromNode.outputs[wire.fromPort] = newName;
        }
      }

      // Automatically update target node input
      const toNode = this.nodes.find((n) => n.id === wire.toNodeId);
      if (toNode && toNode.inputs) {
        const inVal = toNode.inputs[wire.toPort];
        if (Array.isArray(inVal)) {
          inVal[wire.toIndex || 0] = newName;
        } else {
          toNode.inputs[wire.toPort] = newName;
        }
      }

      this.updateWires();
      this.validateChain();
    },

    getSelectedNode() {
      return this.selectedNode;
    },

    getSelectedSchema() {
      return this.selectedSchema;
    },

    deleteNode(nodeId) {
      this.nodes = this.nodes.filter((n) => n.id !== nodeId);
      this.wires = this.wires.filter((w) => w.fromNodeId !== nodeId && w.toNodeId !== nodeId);
      if (this.selectedNodeId === nodeId) {
        this.selectNode(null);
      }
      this.updateWires();
      this.validateChain();
    },

    deleteWire(wireId) {
      this.wires = this.wires.filter((w) => w.id !== wireId);
      if (this.selectedWireId === wireId) {
        this.selectedWireId = null;
      }
      this.updateWires();
      this.validateChain();
    },

    getNodeInputPorts(node) {
      const items = [];
      for (const [portName, val] of Object.entries(node.inputs || {})) {
        if (Array.isArray(val)) {
          val.forEach((ch, idx) => {
            items.push({
              portName: portName,
              index: idx,
              channel: ch,
              label: `${portName.replace('_channel', '').replace('s', '')} #${idx + 1}`,
            });
          });
        } else {
          items.push({
            portName: portName,
            index: 0,
            channel: val,
            label: portName.replace('_channel', ''),
          });
        }
      }
      return items;
    },

    getNodeOutputPorts(node) {
      const items = [];
      for (const [portName, val] of Object.entries(node.outputs || {})) {
        if (Array.isArray(val)) {
          val.forEach((ch, idx) => {
            items.push({
              portName: portName,
              index: idx,
              channel: ch,
              label: `${portName.replace('_channel', '').replace('s', '')} #${idx + 1}`,
            });
          });
        } else {
          items.push({
            portName: portName,
            index: 0,
            channel: val,
            label: portName.replace('_channel', ''),
          });
        }
      }
      return items;
    },

    addChannelItem(node, direction, portName) {
      if (!node || !node[direction] || !Array.isArray(node[direction][portName])) return;
      const arr = node[direction][portName];
      const newName = `${node.name}_${portName}_${arr.length + 1}`;
      arr.push(newName);
      this.onChannelUpdated();
    },

    removeChannelItem(node, direction, portName, index) {
      if (!node || !node[direction] || !Array.isArray(node[direction][portName])) return;
      const arr = node[direction][portName];
      if (arr.length <= 1) return;
      arr.splice(index, 1);
      this.onChannelUpdated();
    },

    onChannelUpdated() {
      setTimeout(() => {
        this.updateWires();
        this.validateChain();
      }, 30);
    },

    generateUniqueChannelName(fromNode, toNode, fromPort, fromIndex) {
      const existingChannels = new Set();
      for (const w of this.wires) {
        if (w.channel) existingChannels.add(w.channel);
      }
      for (const n of this.nodes) {
        for (const dir of ['inputs', 'outputs']) {
          for (const val of Object.values(n[dir] || {})) {
            if (Array.isArray(val)) {
              val.forEach((c) => { if (c) existingChannels.add(c); });
            } else if (val) {
              existingChannels.add(val);
            }
          }
        }
      }

      const isMulti = Array.isArray(fromNode.outputs[fromPort]);
      const basePrefix = isMulti
        ? `${fromNode.name}_b${fromIndex + 1}_to_${toNode.name}`
        : `${fromNode.name}_to_${toNode.name}`;

      if (!existingChannels.has(basePrefix)) {
        return basePrefix;
      }

      let counter = 2;
      while (existingChannels.has(`${basePrefix}_${counter}`)) {
        counter++;
      }
      return `${basePrefix}_${counter}`;
    },

    addWire(fromNodeId, fromPort, fromIndex, toNodeId, toPort, toIndex) {
      const fromNode = this.nodes.find((n) => n.id === fromNodeId);
      const toNode = this.nodes.find((n) => n.id === toNodeId);
      if (!fromNode || !toNode) return;

      fromIndex = fromIndex || 0;
      toIndex = toIndex || 0;

      // Existing wire check (exact same link)
      const exists = this.wires.some(
        (w) =>
          w.fromNodeId === fromNodeId &&
          w.fromPort === fromPort &&
          (w.fromIndex || 0) === fromIndex &&
          w.toNodeId === toNodeId &&
          w.toPort === toPort &&
          (w.toIndex || 0) === toIndex
      );
      if (exists) return;

      // Single outgoing link rule: An output socket may only ever have 1 existing link.
      // If a link already originates from this output socket, modify/replace that previous link.
      const existingOutWireIdx = this.wires.findIndex(
        (w) =>
          w.fromNodeId === fromNodeId &&
          w.fromPort === fromPort &&
          (w.fromIndex || 0) === fromIndex
      );

      if (existingOutWireIdx !== -1) {
        const oldWire = this.wires[existingOutWireIdx];
        // Clear channel name on the old target input port if it was bound
        const oldToNode = this.nodes.find((n) => n.id === oldWire.toNodeId);
        if (oldToNode && oldToNode.inputs) {
          if (Array.isArray(oldToNode.inputs[oldWire.toPort])) {
            oldToNode.inputs[oldWire.toPort][oldWire.toIndex || 0] = '';
          } else {
            oldToNode.inputs[oldWire.toPort] = '';
          }
        }
        // Remove old wire
        this.wires.splice(existingOutWireIdx, 1);
      }

      // Also check if target input socket already had a wire connected; if so, disconnect it
      const existingInWireIdx = this.wires.findIndex(
        (w) =>
          w.toNodeId === toNodeId &&
          w.toPort === toPort &&
          (w.toIndex || 0) === toIndex
      );
      if (existingInWireIdx !== -1) {
        this.wires.splice(existingInWireIdx, 1);
      }

      // Always generate a fresh unique channel wire name when creating a new link
      const channelName = this.generateUniqueChannelName(fromNode, toNode, fromPort, fromIndex);

      if (Array.isArray(fromNode.outputs[fromPort])) {
        fromNode.outputs[fromPort][fromIndex] = channelName;
      } else {
        fromNode.outputs[fromPort] = channelName;
      }

      // Assign to target input port
      if (Array.isArray(toNode.inputs[toPort])) {
        toNode.inputs[toPort][toIndex] = channelName;
      } else {
        toNode.inputs[toPort] = channelName;
      }

      const newWire = {
        id: 'wire_' + Math.random().toString(36).substr(2, 9),
        fromNodeId,
        fromPort,
        fromIndex,
        toNodeId,
        toPort,
        toIndex,
        channel: channelName,
      };

      this.wires.push(newWire);
      this.selectWire(newWire.id);
      this.onChannelUpdated();
    },

    updateWires() {
      // Re-render wire bezier curves into SVG
      const svg = document.getElementById('canvas-svg');
      if (!svg) return;

      // Clear existing wire path elements and label groups except draft-wire
      const paths = svg.querySelectorAll('.canvas-wire:not(#draft-wire)');
      paths.forEach((p) => p.remove());
      const labels = svg.querySelectorAll('.wire-label-group');
      labels.forEach((l) => l.remove());

      for (const wire of this.wires) {
        // If wire is currently being modified/reassigned by user drag, hide its static SVG elements
        if (this.canvasInstance && this.canvasInstance.activeDraftWire && this.canvasInstance.activeDraftWire.reassigningWireId === wire.id) {
          continue;
        }

        const fromIdx = wire.fromIndex !== undefined ? wire.fromIndex : 0;
        const toIdx = wire.toIndex !== undefined ? wire.toIndex : 0;

        const fromSocket = document.querySelector(
          `.port-socket[data-node-id="${wire.fromNodeId}"][data-port-name="${wire.fromPort}"][data-port-index="${fromIdx}"][data-port-type="output"]`
        ) || document.querySelector(
          `.port-socket[data-node-id="${wire.fromNodeId}"][data-port-name="${wire.fromPort}"][data-port-type="output"]`
        );

        const toSocket = document.querySelector(
          `.port-socket[data-node-id="${wire.toNodeId}"][data-port-name="${wire.toPort}"][data-port-index="${toIdx}"][data-port-type="input"]`
        ) || document.querySelector(
          `.port-socket[data-node-id="${wire.toNodeId}"][data-port-name="${wire.toPort}"][data-port-type="input"]`
        );

        if (!fromSocket || !toSocket) continue;

        // Exact socket coordinates relative to world space
        const pt1 = this.canvasInstance.getSocketCenterInWorld(fromSocket);
        const pt2 = this.canvasInstance.getSocketCenterInWorld(toSocket);

        const d = this.canvasInstance.calculateBezier(pt1.x, pt1.y, pt2.x, pt2.y);

        const isSelected = this.selectedWireId === wire.id;

        const pathEl = document.createElementNS('http://www.w3.org/2000/svg', 'path');
        pathEl.setAttribute('d', d);
        pathEl.setAttribute(
          'class',
          'canvas-wire' + (isSelected ? ' selected' : '')
        );
        pathEl.dataset.wireId = wire.id;
        pathEl.addEventListener('click', (e) => {
          e.stopPropagation();
          this.selectWire(wire.id);
        });
        svg.appendChild(pathEl);

        // Render Channel Name Badge on the wire
        if (wire.channel) {
          const midX = (pt1.x + pt2.x) / 2;
          const midY = (pt1.y + pt2.y) / 2;

          const labelGroup = document.createElementNS('http://www.w3.org/2000/svg', 'g');
          labelGroup.setAttribute('class', 'wire-label-group' + (isSelected ? ' selected' : ''));
          labelGroup.dataset.wireId = wire.id;

          const textEl = document.createElementNS('http://www.w3.org/2000/svg', 'text');
          textEl.setAttribute('class', 'wire-label-text');
          textEl.setAttribute('x', midX);
          textEl.setAttribute('y', midY);
          textEl.textContent = wire.channel;

          // Estimate pill width based on text length
          const padX = 10;
          const padY = 4;
          const estWidth = wire.channel.length * 6.8 + padX * 2;
          const estHeight = 18 + padY;

          const rectEl = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
          rectEl.setAttribute('class', 'wire-label-rect');
          rectEl.setAttribute('x', midX - estWidth / 2);
          rectEl.setAttribute('y', midY - estHeight / 2);
          rectEl.setAttribute('width', estWidth);
          rectEl.setAttribute('height', estHeight);

          labelGroup.appendChild(rectEl);
          labelGroup.appendChild(textEl);

          labelGroup.addEventListener('click', (e) => {
            e.stopPropagation();
            this.selectWire(wire.id);
          });

          svg.appendChild(labelGroup);
        }
      }
    },

    exportChainDefinition() {
      const handlers = [];
      for (const n of this.nodes) {
        const h = {
          name: n.name,
          type: n.type,
          config: { ...n.config },
        };
        for (const [k, v] of Object.entries(n.inputs)) {
          if (Array.isArray(v)) {
            h[k] = v.filter(Boolean).join(',');
          } else if (v) {
            h[k] = v;
          }
        }
        for (const [k, v] of Object.entries(n.outputs)) {
          if (Array.isArray(v)) {
            h[k] = v.filter(Boolean).join(',');
          } else if (v) {
            h[k] = v;
          }
        }
        handlers.push(h);
      }

      return {
        name: this.chainName || 'custom_chain',
        backend: this.backend,
        config: { ...this.globalConfig },
        handlers: handlers,
      };
    },

    importChainDefinition(chainDef) {
      this.nodes = [];
      this.wires = [];
      if (chainDef.name) {
        this.chainName = chainDef.name;
        this.onChainNameChanged();
      }
      this.backend = chainDef.backend || 'file';
      this.globalConfig = { ...(chainDef.config || {}) };

      const handlers = chainDef.handlers || [];

      // Rank & crossing-minimized DAG layout (left-to-right, force-directed rank centering)
      const inDegrees = new Map();
      const outgoing = new Map();
      const normalizeChs = (val) => {
        if (!val) return [];
        if (Array.isArray(val)) return val.map((s) => String(s).trim()).filter(Boolean);
        return String(val).split(',').map((s) => s.trim()).filter(Boolean);
      };

      handlers.forEach((h) => {
        inDegrees.set(h.name, 0);
        outgoing.set(h.name, []);
      });

      handlers.forEach((h) => {
        const outList = normalizeChs(h.output_channels || h.output_channel);
        outList.forEach((outCh) => {
          handlers.forEach((consumer) => {
            const inList = normalizeChs(consumer.input_channels || consumer.input_channel);
            if (inList.includes(outCh)) {
              outgoing.get(h.name).push(consumer.name);
              inDegrees.set(consumer.name, (inDegrees.get(consumer.name) || 0) + 1);
            }
          });
        });
      });

      const ranks = new Map();
      const queue = [];
      handlers.forEach((h) => {
        if ((inDegrees.get(h.name) || 0) === 0) {
          ranks.set(h.name, 0);
          queue.push(h.name);
        }
      });
      if (queue.length === 0 && handlers.length > 0) {
        ranks.set(handlers[0].name, 0);
        queue.push(handlers[0].name);
      }
      while (queue.length > 0) {
        const u = queue.shift();
        const r = ranks.get(u) || 0;
        for (const v of outgoing.get(u) || []) {
          const nextR = Math.max(ranks.get(v) || 0, r + 1);
          ranks.set(v, nextR);
          queue.push(v);
        }
      }

      const columns = new Map();
      handlers.forEach((h) => {
        const r = ranks.get(h.name) || 0;
        if (!columns.has(r)) columns.set(r, []);
        columns.get(r).push(h);
      });

      // Barycenter crossing minimization
      const sortedRanks = Array.from(columns.keys()).sort((a, b) => a - b);
      for (let pass = 0; pass < 3; pass++) {
        for (let i = 1; i < sortedRanks.length; i++) {
          const r = sortedRanks[i];
          const prevRank = sortedRanks[i - 1];
          const prevHandlers = columns.get(prevRank);
          const prevRowMap = new Map();
          prevHandlers.forEach((h, idx) => prevRowMap.set(h.name, idx));

          const curHandlers = columns.get(r);
          curHandlers.sort((a, b) => {
            const aIn = normalizeChs(a.input_channels || a.input_channel);
            const bIn = normalizeChs(b.input_channels || b.input_channel);
            let aSum = 0, aCount = 0, bSum = 0, bCount = 0;
            handlers.forEach((src) => {
              if (prevRowMap.has(src.name)) {
                const srcOut = normalizeChs(src.output_channels || src.output_channel);
                if (srcOut.some((ch) => aIn.includes(ch))) {
                  aSum += prevRowMap.get(src.name);
                  aCount++;
                }
                if (srcOut.some((ch) => bIn.includes(ch))) {
                  bSum += prevRowMap.get(src.name);
                  bCount++;
                }
              }
            });
            const aAvg = aCount > 0 ? aSum / aCount : 999;
            const bAvg = bCount > 0 ? bSum / bCount : 999;
            return aAvg - bAvg;
          });
        }
      }

      const colWidth = 340;
      const rowHeight = 160;
      const startX = 100;
      const startY = 80;

      let maxColCount = 1;
      columns.forEach((colHandlers) => {
        if (colHandlers.length > maxColCount) maxColCount = colHandlers.length;
      });

      columns.forEach((colHandlers, r) => {
        const colX = startX + r * colWidth;
        const colOffset = ((maxColCount - colHandlers.length) * rowHeight) / 2;

        colHandlers.forEach((h, rowIdx) => {
          const nodeY = startY + colOffset + rowIdx * rowHeight;
          const node = {
            id: 'node_' + Math.random().toString(36).substr(2, 9),
            name: h.name,
            type: h.type,
            x: colX,
            y: nodeY,
            config: { ...(h.config || {}) },
            inputs: {},
            outputs: {},
          };

          if (h.input_channel) node.inputs['input_channel'] = h.input_channel;
          if (h.input_channels) {
            node.inputs['input_channels'] = Array.isArray(h.input_channels)
              ? h.input_channels
              : String(h.input_channels).split(',').map((s) => s.trim()).filter(Boolean);
          }
          if (h.output_channel) node.outputs['output_channel'] = h.output_channel;
          if (h.output_channels) {
            node.outputs['output_channels'] = Array.isArray(h.output_channels)
              ? h.output_channels
              : String(h.output_channels).split(',').map((s) => s.trim()).filter(Boolean);
          }

          this.nodes.push(node);
        });
      });

      // Synthesize wires based on matching channel names
      for (const srcNode of this.nodes) {
        for (const [outPort, outChs] of Object.entries(srcNode.outputs)) {
          const chList = Array.isArray(outChs)
            ? outChs
            : String(outChs).split(',').map((s) => s.trim()).filter(Boolean);
          chList.forEach((ch, outIdx) => {
            for (const dstNode of this.nodes) {
              if (dstNode.id === srcNode.id) continue;
              for (const [inPort, inChs] of Object.entries(dstNode.inputs)) {
                const inList = Array.isArray(inChs)
                  ? inChs
                  : String(inChs).split(',').map((s) => s.trim()).filter(Boolean);
                inList.forEach((targetCh, inIdx) => {
                  if (targetCh === ch) {
                    this.wires.push({
                      id: 'wire_' + Math.random().toString(36).substr(2, 9),
                      fromNodeId: srcNode.id,
                      fromPort: outPort,
                      fromIndex: outIdx,
                      toNodeId: dstNode.id,
                      toPort: inPort,
                      toIndex: inIdx,
                      channel: ch,
                    });
                  }
                });
              }
            }
          });
        }
      }

      setTimeout(() => {
        this.updateWires();
        this.validateChain();
      }, 50);
    },

    async validateChain() {
      const chainDef = this.exportChainDefinition();
      try {
        const res = await fetch('/api/chain/validate', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(chainDef),
        });
        if (res.ok) {
          this.validation = await res.json();
        }
      } catch (err) {
        // Fallback local quick validation
        const errors = [];
        const names = new Set();
        for (const n of this.nodes) {
          if (!n.name) errors.push('A handler node has no name specified');
          if (names.has(n.name)) errors.push(`Duplicate handler name '${n.name}'`);
          names.add(n.name);
        }
        this.validation = {
          valid: errors.length === 0,
          errors: errors,
          warnings: [],
        };
      }
    },

    toTomlString(obj) {
      let out = `# Generated by Proxy Visual Chain Builder\n`;
      if (obj.name) {
        out += `# Pipeline Name: ${obj.name}\n`;
      }
      out += `backend = "${obj.backend}"\n\n[config]\n`;
      for (const [k, v] of Object.entries(obj.config || {})) {
        if (typeof v === 'boolean') {
          out += `"${k}" = ${v}\n`;
        } else if (typeof v === 'number') {
          out += `"${k}" = ${v}\n`;
        } else {
          out += `"${k}" = "${v}"\n`;
        }
      }

      for (const h of obj.handlers || []) {
        out += `\n[[handlers]]\nname = "${h.name}"\ntype = "${h.type}"\n`;
        if (h.input_channel) out += `input_channel = "${h.input_channel}"\n`;
        if (h.input_channels) out += `input_channels = "${h.input_channels}"\n`;
        if (h.output_channel) out += `output_channel = "${h.output_channel}"\n`;
        if (h.output_channels) out += `output_channels = "${h.output_channels}"\n`;

        if (h.config && Object.keys(h.config).length > 0) {
          out += `[handlers.config]\n`;
          for (const [ck, cv] of Object.entries(h.config)) {
            if (typeof cv === 'boolean') {
              out += `"${ck}" = ${cv}\n`;
            } else if (typeof cv === 'number') {
              out += `"${ck}" = ${cv}\n`;
            } else {
              out += `"${ck}" = "${cv}"\n`;
            }
          }
        }
      }
      return out;
    },

    downloadFile(filename, text) {
      const el = document.createElement('a');
      el.setAttribute('href', 'data:text/plain;charset=utf-8,' + encodeURIComponent(text));
      el.setAttribute('download', filename);
      el.style.display = 'none';
      document.body.appendChild(el);
      el.click();
      document.body.removeChild(el);
    },

    exportToml() {
      const chainDef = this.exportChainDefinition();
      const tomlStr = this.toTomlString(chainDef);
      const filename = (this.chainName ? this.chainName.trim().replace(/[^a-zA-Z0-9_-]/g, '_') : 'chain') + '.toml';
      this.downloadFile(filename, tomlStr);
      this.flashStatus(`Exported ${filename} successfully!`);
    },

    exportJson() {
      const chainDef = this.exportChainDefinition();
      const jsonStr = JSON.stringify(chainDef, null, 2);
      const filename = (this.chainName ? this.chainName.trim().replace(/[^a-zA-Z0-9_-]/g, '_') : 'chain') + '.json';
      this.downloadFile(filename, jsonStr);
      this.flashStatus(`Exported ${filename} successfully!`);
    },

    onChainNameChanged() {
      const clean = (this.chainName || '').trim().replace(/[^a-zA-Z0-9_-]/g, '_');
      this.saveFilename = (clean || 'custom_chain') + '.toml';
      this.validateChain();
    },

    openSaveModal() {
      this.onChainNameChanged();
      this.showSaveModal = true;
    },

    closeSaveModal() {
      this.showSaveModal = false;
    },

    openExportModal() {
      this.showExportModal = true;
    },

    closeExportModal() {
      this.showExportModal = false;
    },

    resetChain() {
      if (this.nodes.length > 0) {
        if (!confirm('Are you sure you want to reset the chain? All nodes and wire connections will be removed.')) {
          return;
        }
      }
      this.nodes = [];
      this.wires = [];
      this.selectedNodeId = null;
      this.selectedNode = null;
      this.selectedSchema = null;
      this.selectedWireId = null;
      this.updateWires();
      this.validateChain();
      this.flashStatus('Chain reset to blank state.');
    },

    async confirmSaveToDisk() {
      this.closeSaveModal();
      const chainDef = this.exportChainDefinition();
      chainDef.filename = this.saveFilename || 'custom_chain.toml';
      try {
        const res = await fetch('/api/chain/save', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(chainDef),
        });
        const data = await res.json();
        if (res.ok) {
          this.flashStatus(`Saved to ${data.path || 'disk'}`);
        } else {
          alert('Save failed: ' + (data.message || 'Unknown error'));
        }
      } catch (err) {
        alert('Save request failed: ' + err);
      }
    },

    async saveToDisk() {
      this.openSaveModal();
    },

    async runSimulation() {
      const chainDef = this.exportChainDefinition();
      try {
        const res = await fetch('/api/chain/run', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(chainDef),
        });
        const data = await res.json();
        if (res.ok) {
          this.flashStatus('Chain runner launched in simulation mode!');
          if (data.url) {
            window.location.href = data.url;
          }
        } else {
          alert('Run failed: ' + (data.message || 'Unknown error'));
        }
      } catch (err) {
        alert('Run request failed: ' + err);
      }
    },

    flashStatus(msg) {
      this.statusMsg = msg;
      setTimeout(() => {
        if (this.statusMsg === msg) this.statusMsg = '';
      }, 3500);
    },

    onDragPaletteStart(e, handlerType) {
      e.dataTransfer.setData('text/plain', handlerType);
    },
  };
}

window.createApp = createApp;

