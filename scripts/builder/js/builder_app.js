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
    currentChainPath: null,
    saveFilename: 'custom_chain.toml',
    savedDefinitionSnapshot: null,
    showSaveModal: false,
    saving: false,
    saveError: '',
    overwriteRequired: false,
    overwriteConfirmed: false,
    runningSimulation: false,
    runError: '',
    allowUnload: false,
    showExportModal: false,
    showChainViewerModal: false,
    chainViewerTitle: 'Generated Pipeline Definition',
    chainViewerError: '',
    chainViewerToml: '',
    chainViewerLines: [],
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
    showDiagnosticsDrawer: false,

    // Slide-out complex parameter drawer state
    drawerNode: null,
    drawerParamKey: null,
    drawerParamDef: null,

    validation: {
      valid: true,
      errors: [],
      warnings: [],
    },

    statusMsg: '',
    canvasInstance: null,

    get hasUnsavedChanges() {
      return this.savedDefinitionSnapshot !== null
        && this.getDefinitionSnapshot() !== this.savedDefinitionSnapshot;
    },

    async init() {
      await this.loadSchemas();
      this.initCanvas();
      await this.loadInitialChain();
      this.validateChain();
      this.savedDefinitionSnapshot = this.getDefinitionSnapshot();

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

      window.addEventListener('beforeunload', (event) => {
        if (!this.hasUnsavedChanges || this.allowUnload) return;
        event.preventDefault();
        event.returnValue = '';
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
        const requestedChain = new URLSearchParams(window.location.search).get('chain');
        const endpoint = requestedChain
          ? `/api/chain?path=${encodeURIComponent(requestedChain)}`
          : '/api/chain';
        const res = await fetch(endpoint);
        if (!res.ok) {
          const data = await res.json();
          throw new Error(data.error || `Request failed (${res.status})`);
        }
        const chainDef = await res.json();
        if (requestedChain || (chainDef && chainDef.handlers && chainDef.handlers.length > 0)) {
          this.importChainDefinition(chainDef);
          if (requestedChain) {
            this.currentChainPath = requestedChain;
            window.ProxyStudioContext?.set(requestedChain, this.backend);
            this.flashStatus(`Loaded ${requestedChain}`);
          }
        }
      } catch (err) {
        const requestedChain = new URLSearchParams(window.location.search).get('chain');
        if (requestedChain) {
          this.flashStatus(`Could not load chain: ${err.message}`);
        }
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
            config[k] = JSON.parse(JSON.stringify(p.default));
          } else if (p.type === 'array') {
            config[k] = [];
          } else if (p.type === 'object') {
            config[k] = {};
          }
        }
      }

      const newNode = {
        id: 'node_' + Math.random().toString(36).substr(2, 9),
        name: candidateName,
        type: handlerType,
        x: x || 250,
        y: y || 150,
        collapsed: false,
        config: config,
        inputs: {},
        outputs: {},
      };

      // Initialize socket structures empty (do not populate default/placeholder channel names)
      if (schema.ports && schema.ports.inputs) {
        for (const p of schema.ports.inputs) {
          if (p.cardinality === 'multiple') {
            newNode.inputs[p.name] = [''];
          } else {
            newNode.inputs[p.name] = '';
          }
        }
      }
      if (schema.ports && schema.ports.outputs) {
        for (const p of schema.ports.outputs) {
          if (p.cardinality === 'multiple') {
            newNode.outputs[p.name] = [''];
          } else {
            newNode.outputs[p.name] = '';
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

    toggleNodeCollapse(node) {
      if (!node) return;
      node.collapsed = !node.collapsed;
      setTimeout(() => {
        this.updateWires();
      }, 40);
    },

    getNodeErrors(node) {
      if (!node) return [];
      const nodeDiags = this.formattedDiagnostics.filter((d) => d.nodeName === node.name && d.type === 'error');
      return nodeDiags.map((d) => d.message);
    },

    getNodeWarnings(node) {
      if (!node) return [];
      const nodeDiags = this.formattedDiagnostics.filter((d) => d.nodeName === node.name && d.type === 'warning');
      return nodeDiags.map((d) => d.message);
    },

    get formattedDiagnostics() {
      const items = [];
      const rawErrors = (this.validation && this.validation.errors) || [];
      const rawWarnings = (this.validation && this.validation.warnings) || [];

      // Build mapping from channel name -> { nodeName, portLabel } for both outputs and inputs
      const channelToOutputPort = new Map();
      const channelToInputPort = new Map();

      for (const node of this.nodes) {
        const outPorts = this.getNodeOutputPorts(node);
        for (const p of outPorts) {
          if (p.channel) {
            channelToOutputPort.set(p.channel, { nodeName: node.name, label: `'${p.label}'` });
          }
        }
        const inPorts = this.getNodeInputPorts(node);
        for (const p of inPorts) {
          if (p.channel) {
            channelToInputPort.set(p.channel, { nodeName: node.name, label: `'${p.label}'` });
          }
        }
      }

      const processMsg = (rawMsg, type) => {
        let msg = rawMsg;
        let nodeName = 'Pipeline';

        // 1. Check for Handler 'name' prefix pattern: Handler 'xxx' missing required ...
        const handlerMatch = msg.match(/^Handler '([^']+)'\s*(.*)/i);
        if (handlerMatch) {
          nodeName = handlerMatch[1];
          let rest = handlerMatch[2];
          // Transform port name in rest if present (e.g. "missing required input binding: input_channel" or "input_channels")
          rest = rest.replace(/input_channels?/g, 'input socket');
          rest = rest.replace(/output_channels?/g, 'output socket');
          // Capitalize first character
          msg = rest ? rest.charAt(0).toUpperCase() + rest.slice(1) : msg;
          return { nodeName, message: msg, type };
        }

        // 2. Check for Duplicate handler name 'xxx' pattern
        const dupHandlerMatch = msg.match(/^Duplicate handler name '([^']+)'/i);
        if (dupHandlerMatch) {
          return { nodeName: dupHandlerMatch[1], message: `Duplicate handler instance name '${dupHandlerMatch[1]}'`, type };
        }

        // 3. Check for Output channel 'xxx' produced by ['yyy'] is not consumed by any handler...
        const outUnconsumedMatch = msg.match(/^Output channel '([^']+)' produced by (?:\[?'?([^'\]]+)'?\]?)\s*(.*)/i);
        if (outUnconsumedMatch) {
          const ch = outUnconsumedMatch[1];
          const producer = outUnconsumedMatch[2];
          const rest = outUnconsumedMatch[3];
          nodeName = producer;
          const portInfo = channelToOutputPort.get(ch);
          const portLabel = portInfo ? portInfo.label : `'${ch}'`;
          msg = `${portLabel} is not consumed by any downstream handler`;
          return { nodeName, message: msg, type };
        }

        // 4. Check for Input channel 'xxx' consumed by ['yyy'] has no producing handler...
        const inUnproducedMatch = msg.match(/^Input channel '([^']+)' consumed by (?:\[?'?([^'\]]+)'?\]?)\s*(.*)/i);
        if (inUnproducedMatch) {
          const ch = inUnproducedMatch[1];
          const consumer = inUnproducedMatch[2];
          nodeName = consumer;
          const portInfo = channelToInputPort.get(ch);
          const portLabel = portInfo ? portInfo.label : `'${ch}'`;
          msg = `${portLabel} has no upstream producing handler connected`;
          return { nodeName, message: msg, type };
        }

        // 5. Check for Channel wire 'xxx' is produced by multiple handlers ([...])
        const multiProduceMatch = msg.match(/^Channel wire '([^']+)' is produced by multiple handlers \(([^)]+)\)/i);
        if (multiProduceMatch) {
          const ch = multiProduceMatch[1];
          const producers = multiProduceMatch[2];
          const portInfo = channelToOutputPort.get(ch);
          const portLabel = portInfo ? portInfo.label : `'${ch}'`;
          return {
            nodeName: producers.split(',')[0].replace(/['\s\[\]]/g, '') || 'Channel Wire',
            message: `Wire ${portLabel} is produced by multiple handlers (${producers}). Wire names must be unique.`,
            type,
          };
        }

        // 6. Generic node name match if any node's name appears in single quotes
        for (const n of this.nodes) {
          if (msg.includes(`'${n.name}'`)) {
            nodeName = n.name;
            break;
          }
        }

        return { nodeName, message: msg, type };
      };

      rawErrors.forEach((e) => items.push(processMsg(e, 'error')));
      rawWarnings.forEach((w) => items.push(processMsg(w, 'warning')));

      return items;
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
      const wire = this.wires.find((w) => w.id === wireId);
      if (wire) {
        // Disconnect channel bindings from the connected ports on both ends
        const fromNode = this.nodes.find((n) => n.id === wire.fromNodeId);
        if (fromNode && fromNode.outputs && fromNode.outputs[wire.fromPort] !== undefined) {
          if (Array.isArray(fromNode.outputs[wire.fromPort])) {
            fromNode.outputs[wire.fromPort][wire.fromIndex || 0] = '';
          } else {
            fromNode.outputs[wire.fromPort] = '';
          }
        }
        const toNode = this.nodes.find((n) => n.id === wire.toNodeId);
        if (toNode && toNode.inputs && toNode.inputs[wire.toPort] !== undefined) {
          if (Array.isArray(toNode.inputs[wire.toPort])) {
            toNode.inputs[wire.toPort][wire.toIndex || 0] = '';
          } else {
            toNode.inputs[wire.toPort] = '';
          }
        }
      }

      this.wires = this.wires.filter((w) => w.id !== wireId);
      if (this.selectedWireId === wireId) {
        this.selectedWireId = null;
      }
      this.updateWires();
      this.validateChain();
    },

    isPortLinked(nodeId, direction, portName, portIndex = 0) {
      if (direction === 'output') {
        return this.wires.some(
          (w) => w.fromNodeId === nodeId && w.fromPort === portName && (w.fromIndex || 0) === portIndex
        );
      }
      return this.wires.some(
        (w) => w.toNodeId === nodeId && w.toPort === portName && (w.toIndex || 0) === portIndex
      );
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

    openParamDrawer(node, paramKey, paramDef) {
      this.drawerNode = node;
      this.drawerParamKey = paramKey;
      this.drawerParamDef = paramDef;
      if (paramDef.type === 'array' && !Array.isArray(node.config[paramKey])) {
        node.config[paramKey] = [];
      } else if (paramDef.type === 'object' && (!node.config[paramKey] || typeof node.config[paramKey] !== 'object')) {
        node.config[paramKey] = {};
      }
    },

    closeParamDrawer() {
      this.drawerNode = null;
      this.drawerParamKey = null;
      this.drawerParamDef = null;
      this.validateChain();
    },

    isFieldVisible(propDef, itemScope) {
      if (!propDef || !propDef['ui:visible_if']) return true;
      const cond = propDef['ui:visible_if'];
      if (typeof cond !== 'object') return true;
      if (!itemScope) return true;
      for (const [k, expectedVal] of Object.entries(cond)) {
        if (String(itemScope[k]) !== String(expectedVal)) {
          return false;
        }
      }
      return true;
    },

    getSchemaProperties(schemaObj) {
      if (!schemaObj || !schemaObj.properties) return [];
      return Object.entries(schemaObj.properties).map(([key, def]) => ({
        key,
        def,
      }));
    },

    getAvailableTargetChannels(node) {
      const channels = new Set();
      // Only include output channels from the current node and other nodes across the chain (excluding inputs)
      if (node && node.outputs) {
        for (const val of Object.values(node.outputs)) {
          if (Array.isArray(val)) {
            val.forEach((c) => { if (c && String(c).trim()) channels.add(String(c).trim()); });
          } else if (val && String(val).trim()) {
            String(val).split(',').forEach((c) => { if (c && c.trim()) channels.add(c.trim()); });
          }
        }
      }
      for (const n of this.nodes) {
        if (n.outputs) {
          for (const val of Object.values(n.outputs)) {
            if (Array.isArray(val)) {
              val.forEach((c) => { if (c && String(c).trim()) channels.add(String(c).trim()); });
            } else if (val && String(val).trim()) {
              String(val).split(',').forEach((c) => { if (c && c.trim()) channels.add(c.trim()); });
            }
          }
        }
      }
      return Array.from(channels).sort();
    },

    addArrayItem(node, paramKey, paramDef) {
      if (!node || !node.config) return;
      if (!Array.isArray(node.config[paramKey])) {
        node.config[paramKey] = [];
      }
      const newItem = {};
      if (paramDef.items && paramDef.items.properties) {
        for (const [k, p] of Object.entries(paramDef.items.properties)) {
          if (p.default !== undefined) {
            newItem[k] = JSON.parse(JSON.stringify(p.default));
          } else if (p.type === 'integer' || p.type === 'int' || p.type === 'number') {
            newItem[k] = 0;
          } else {
            newItem[k] = '';
          }
        }
      }
      node.config[paramKey].push(newItem);
      this.validateChain();
    },

    removeArrayItem(arr, index) {
      if (!Array.isArray(arr)) return;
      arr.splice(index, 1);
      this.validateChain();
    },

    moveArrayItem(arr, index, delta) {
      if (!Array.isArray(arr)) return;
      const targetIdx = index + delta;
      if (targetIdx < 0 || targetIdx >= arr.length) return;
      const temp = arr[index];
      arr[index] = arr[targetIdx];
      arr[targetIdx] = temp;
      this.validateChain();
    },

    addChannelItem(node, direction, portName) {
      if (!node || !node[direction] || !Array.isArray(node[direction][portName])) return;
      const arr = node[direction][portName];
      arr.push('');
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
        pathEl.setAttribute('tabindex', '0');
        pathEl.setAttribute('role', 'button');
        pathEl.setAttribute('aria-label', `Select connection ${wire.channel || wire.id}`);
        pathEl.addEventListener('keydown', (e) => {
          if (e.key !== 'Enter' && e.key !== ' ') return;
          e.preventDefault();
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

          // If wire is selected, render an interactive disconnect/delete button '✕'
          if (isSelected) {
            const delRadius = 8;
            const delCenterX = midX + estWidth / 2 + 10;
            const delCenterY = midY;

            const delGroup = document.createElementNS('http://www.w3.org/2000/svg', 'g');
            delGroup.setAttribute('class', 'wire-delete-btn');
            delGroup.setAttribute('cursor', 'pointer');
            delGroup.setAttribute('pointer-events', 'auto');
            delGroup.setAttribute('tabindex', '0');
            delGroup.setAttribute('role', 'button');
            delGroup.setAttribute('aria-label', `Delete connection ${wire.channel || wire.id}`);

            const delCircle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
            delCircle.setAttribute('cx', delCenterX);
            delCircle.setAttribute('cy', delCenterY);
            delCircle.setAttribute('r', delRadius);
            delCircle.setAttribute('class', 'wire-delete-circle');

            const delCross = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            delCross.setAttribute('x', delCenterX);
            delCross.setAttribute('y', delCenterY);
            delCross.setAttribute('class', 'wire-delete-text');
            delCross.textContent = '✕';

            delGroup.appendChild(delCircle);
            delGroup.appendChild(delCross);

            delGroup.addEventListener('click', (e) => {
              e.stopPropagation();
              this.deleteWire(wire.id);
            });
            delGroup.addEventListener('keydown', (e) => {
              if (e.key !== 'Enter' && e.key !== ' ') return;
              e.preventDefault();
              e.stopPropagation();
              this.deleteWire(wire.id);
            });

            labelGroup.appendChild(delGroup);
          }

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

    getDefinitionSnapshot() {
      return JSON.stringify(this.exportChainDefinition());
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
            collapsed: false,
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
          const complexArrays = [];
          for (const [ck, cv] of Object.entries(h.config)) {
            if (Array.isArray(cv)) {
              complexArrays.push([ck, cv]);
            } else if (cv !== null && typeof cv === 'object') {
              // inline table
              out += `"${ck}" = ${JSON.stringify(cv)}\n`;
            } else if (typeof cv === 'boolean') {
              out += `"${ck}" = ${cv}\n`;
            } else if (typeof cv === 'number') {
              out += `"${ck}" = ${cv}\n`;
            } else {
              // multiline string or standard string
              const sVal = String(cv);
              if (sVal.includes('\n')) {
                out += `"${ck}" = """\n${sVal}\n"""\n`;
              } else {
                out += `"${ck}" = "${sVal.replace(/"/g, '\\"')}"\n`;
              }
            }
          }

          // Nested arrays of tables (e.g. [[handlers.config.rules]])
          for (const [arrKey, arrItems] of complexArrays) {
            for (const item of arrItems) {
              out += `\n[[handlers.config.${arrKey}]]\n`;
              for (const [itemKey, itemVal] of Object.entries(item)) {
                if (itemVal === undefined || itemVal === null) continue;
                if (typeof itemVal === 'boolean') {
                  out += `${itemKey} = ${itemVal}\n`;
                } else if (typeof itemVal === 'number') {
                  out += `${itemKey} = ${itemVal}\n`;
                } else {
                  const sVal = String(itemVal);
                  if (sVal.includes('\n')) {
                    out += `${itemKey} = """\n${sVal}\n"""\n`;
                  } else {
                    out += `${itemKey} = "${sVal.replace(/"/g, '\\"')}"\n`;
                  }
                }
              }
            }
          }
        }
      }
      return out;
    },

    escapeHtml(str) {
      if (!str) return '';
      return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    },

    highlightTomlLine(line) {
      if (!line) return '&nbsp;';
      const trimmed = line.trim();
      if (!trimmed) return '&nbsp;';

      // Comments
      if (trimmed.startsWith('#')) {
        return `<span class="toml-tok-comment">${this.escapeHtml(line)}</span>`;
      }

      // [[table.array]]
      if (trimmed.startsWith('[[') && trimmed.endsWith(']]')) {
        return `<span class="toml-tok-table-array">${this.escapeHtml(line)}</span>`;
      }

      // [table]
      if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
        return `<span class="toml-tok-table">${this.escapeHtml(line)}</span>`;
      }

      // Key = Value
      const eqIdx = line.indexOf('=');
      if (eqIdx !== -1) {
        const keyPart = line.substring(0, eqIdx);
        const valPart = line.substring(eqIdx + 1);
        const trimmedVal = valPart.trim();

        let valHtml = '';
        if (trimmedVal === 'true' || trimmedVal === 'false') {
          valHtml = `<span class="toml-tok-bool">${this.escapeHtml(valPart)}</span>`;
        } else if (!isNaN(Number(trimmedVal)) && trimmedVal !== '') {
          valHtml = `<span class="toml-tok-num">${this.escapeHtml(valPart)}</span>`;
        } else if (trimmedVal.startsWith('"') || trimmedVal.startsWith("'")) {
          valHtml = `<span class="toml-tok-str">${this.escapeHtml(valPart)}</span>`;
        } else {
          valHtml = `<span class="toml-tok-val">${this.escapeHtml(valPart)}</span>`;
        }

        return `<span class="toml-tok-key">${this.escapeHtml(keyPart)}</span><span class="toml-tok-punct">=</span>${valHtml}`;
      }

      return this.escapeHtml(line);
    },

    openChainViewer(title, tomlStr, errorMsg = '') {
      this.chainViewerTitle = title || 'Generated Pipeline Definition';
      this.chainViewerError = errorMsg || '';
      this.chainViewerToml = tomlStr || '';

      // Parse error line number if present: e.g. "at line 22, column 85"
      let errLineNum = null;
      if (errorMsg) {
        const m = errorMsg.match(/line\s+(\d+)/i);
        if (m) {
          errLineNum = parseInt(m[1], 10);
        }
      }

      const rawLines = (tomlStr || '').split('\n');
      this.chainViewerLines = rawLines.map((l, idx) => {
        const lineNum = idx + 1;
        return {
          num: lineNum,
          raw: l,
          html: this.highlightTomlLine(l),
          isErrorLine: errLineNum !== null && lineNum === errLineNum,
        };
      });

      this.showChainViewerModal = true;
    },

    closeChainViewer() {
      this.showChainViewerModal = false;
    },

    copyChainViewerText() {
      if (this.chainViewerToml) {
        navigator.clipboard.writeText(this.chainViewerToml);
        this.flashStatus('Copied generated TOML to clipboard!');
      }
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
      if (this.currentChainPath) {
        this.saveFilename = this.currentChainPath.split(/[\\/]/).pop();
      } else {
        this.onChainNameChanged();
      }
      this.saveError = '';
      this.overwriteRequired = Boolean(this.currentChainPath);
      this.overwriteConfirmed = false;
      this.showSaveModal = true;
    },

    closeSaveModal() {
      if (this.saving) return;
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
      if (this.saving || (this.overwriteRequired && !this.overwriteConfirmed)) return;
      this.saving = true;
      this.saveError = '';
      const chainDef = this.exportChainDefinition();
      chainDef.filename = this.saveFilename || 'custom_chain.toml';
      chainDef.chainPath = this.currentChainPath;
      chainDef.overwrite = this.overwriteConfirmed;
      try {
        const res = await fetch('/api/chain/save', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(chainDef),
        });
        const data = await res.json();
        if (!res.ok) {
          if (res.status === 409) {
            this.overwriteRequired = true;
          }
          throw new Error(data.message || `Save failed (HTTP ${res.status})`);
        }
        this.currentChainPath = data.path;
        window.ProxyStudioContext?.set(this.currentChainPath, this.backend);
        const nextUrl = new URL(window.location.href);
        nextUrl.searchParams.set('chain', this.currentChainPath);
        history.replaceState(null, '', nextUrl);
        this.savedDefinitionSnapshot = this.getDefinitionSnapshot();
        this.showSaveModal = false;
        this.flashStatus(`Saved to ${data.path || 'disk'}`);
      } catch (err) {
        this.saveError = err.message || String(err);
      } finally {
        this.saving = false;
      }
    },

    async saveToDisk() {
      this.openSaveModal();
    },

    async runSimulation() {
      if (this.runningSimulation) return;
      this.runningSimulation = true;
      this.runError = '';
      const chainDef = this.exportChainDefinition();
      chainDef.chainPath = this.currentChainPath;
      const localToml = this.toTomlString(chainDef);
      try {
        const res = await fetch('/api/chain/run', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(chainDef),
        });
        const data = await res.json();
        if (res.ok) {
          this.allowUnload = true;
          this.flashStatus('Chain runner launched in simulation mode!');
          if (data.url) {
            window.location.href = data.url;
          }
        } else {
          const errText = data.message || 'Unknown error';
          const tomlText = data.toml || localToml;
          this.runError = errText;
          this.openChainViewer('Simulation Launch Failed', tomlText, errText);
        }
      } catch (err) {
        this.runError = err.message || String(err);
        this.openChainViewer('Network / Request Failed', localToml, String(err));
      } finally {
        this.runningSimulation = false;
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
