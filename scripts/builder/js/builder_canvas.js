/**
 * builder_canvas.js - Interactive node canvas, pan/zoom, wire bezier routing, and magnetized drag-and-drop.
 */

class BuilderCanvas {
  constructor(containerEl, viewportEl, svgEl, app) {
    this.container = containerEl;
    this.viewport = viewportEl;
    this.svg = svgEl;
    this.app = app;

    this.panX = 100;
    this.panY = 60;
    this.zoom = 1.0;

    this.isPanning = false;
    this.panStartX = 0;
    this.panStartY = 0;

    this.draggingNode = null;
    this.nodeDragOffsetX = 0;
    this.nodeDragOffsetY = 0;

    // Wire creation state
    this.activeDraftWire = null;
    this.magnetizedSocket = null;

    this.initEvents();
    this.applyTransform();
  }

  initEvents() {
    this.container.addEventListener('mousedown', (e) => this.onMouseDown(e));
    window.addEventListener('mousemove', (e) => this.onMouseMove(e));
    window.addEventListener('mouseup', (e) => this.onMouseUp(e));
    this.container.addEventListener('wheel', (e) => this.onWheel(e), { passive: false });

    // Drag-over & drop from palette
    this.container.addEventListener('dragover', (e) => e.preventDefault());
    this.container.addEventListener('drop', (e) => this.onDrop(e));
  }

  screenToWorld(clientX, clientY) {
    const rect = this.container.getBoundingClientRect();
    const x = (clientX - rect.left - this.panX) / this.zoom;
    const y = (clientY - rect.top - this.panY) / this.zoom;
    return { x, y };
  }

  worldToScreen(worldX, worldY) {
    const rect = this.container.getBoundingClientRect();
    const x = worldX * this.zoom + this.panX + rect.left;
    const y = worldY * this.zoom + this.panY + rect.top;
    return { x, y };
  }

  applyTransform() {
    this.viewport.style.transform = `translate(${this.panX}px, ${this.panY}px) scale(${this.zoom})`;
  }

  getSocketCenterInWorld(socketEl) {
    const rect = socketEl.getBoundingClientRect();
    return this.screenToWorld(rect.left + rect.width / 2, rect.top + rect.height / 2);
  }

  findNearbyInputSocket(mouseWorldX, mouseWorldY, maxDist = 35) {
    const inputSockets = document.querySelectorAll('.port-socket[data-port-type="input"]');
    let closest = null;
    let minDist = maxDist;

    for (const s of inputSockets) {
      if (this.activeDraftWire && s.dataset.nodeId === this.activeDraftWire.fromNodeId) {
        continue; // Don't snap to same node
      }
      const pt = this.getSocketCenterInWorld(s);
      const dist = Math.hypot(pt.x - mouseWorldX, pt.y - mouseWorldY);
      if (dist < minDist) {
        minDist = dist;
        closest = { el: s, pt: pt, dist: dist };
      }
    }
    return closest;
  }

  onMouseDown(e) {
    // If click on interactive elements (input, select, button, etc.), don't initiate node drag or pan
    if (e.target.closest('input, select, button, textarea, label')) {
      return;
    }

    // If click on socket, initiate wire creation
    const socketEl = e.target.closest('.port-socket');
    if (socketEl) {
      e.stopPropagation();
      e.preventDefault();
      const nodeId = socketEl.dataset.nodeId;
      const portName = socketEl.dataset.portName;
      const portIndex = socketEl.dataset.portIndex !== undefined ? parseInt(socketEl.dataset.portIndex, 10) : 0;
      const portType = socketEl.dataset.portType; // 'input' or 'output'

      if (portType === 'output') {
        const pt = this.getSocketCenterInWorld(socketEl);

        // Check if this output socket already has a connected wire being modified/reassigned
        const existingWire = this.app.wires.find(
          (w) =>
            w.fromNodeId === nodeId &&
            w.fromPort === portName &&
            (w.fromIndex || 0) === portIndex
        );

        this.activeDraftWire = {
          fromNodeId: nodeId,
          fromPort: portName,
          fromIndex: portIndex,
          startX: pt.x,
          startY: pt.y,
          currX: pt.x,
          currY: pt.y,
          reassigningWireId: existingWire ? existingWire.id : null,
        };
        // Re-render wires so the existing wire is temporarily hidden while its destination is dragged
        this.app.updateWires();
        this.renderDraftWire();
      }
      return;
    }

    // If click on a node
    const nodeEl = e.target.closest('.node-card');
    if (nodeEl) {
      const nodeId = nodeEl.dataset.nodeId;
      this.app.selectNode(nodeId);
      this.draggingNode = this.app.nodes.find((n) => n.id === nodeId);
      if (this.draggingNode) {
        const mouseWorld = this.screenToWorld(e.clientX, e.clientY);
        this.nodeDragOffsetX = mouseWorld.x - this.draggingNode.x;
        this.nodeDragOffsetY = mouseWorld.y - this.draggingNode.y;
      }
      return;
    }

    // If click on background, initiate panning or deselect
    if (e.target === this.container || e.target === this.svg || e.target === this.viewport) {
      this.app.selectNode(null);
      this.app.selectWire(null);
      this.isPanning = true;
      this.panStartX = e.clientX - this.panX;
      this.panStartY = e.clientY - this.panY;
    }
  }

  onMouseMove(e) {
    if (this.isPanning) {
      this.panX = e.clientX - this.panStartX;
      this.panY = e.clientY - this.panStartY;
      this.applyTransform();
      return;
    }

    if (this.draggingNode) {
      const mouseWorld = this.screenToWorld(e.clientX, e.clientY);
      this.draggingNode.x = Math.round(mouseWorld.x - this.nodeDragOffsetX);
      this.draggingNode.y = Math.round(mouseWorld.y - this.nodeDragOffsetY);
      this.app.updateWires();
      return;
    }

    if (this.activeDraftWire) {
      const mouseWorld = this.screenToWorld(e.clientX, e.clientY);
      
      // Check for magnetic snap to nearby input sockets
      const snap = this.findNearbyInputSocket(mouseWorld.x, mouseWorld.y, 40);

      // Clear previous magnetized socket class
      if (this.magnetizedSocket && (!snap || this.magnetizedSocket !== snap.el)) {
        this.magnetizedSocket.classList.remove('magnetized');
        this.magnetizedSocket = null;
      }

      if (snap) {
        this.magnetizedSocket = snap.el;
        this.magnetizedSocket.classList.add('magnetized');
        // Magnetized coordinate lock!
        this.activeDraftWire.currX = snap.pt.x;
        this.activeDraftWire.currY = snap.pt.y;
      } else {
        this.activeDraftWire.currX = mouseWorld.x;
        this.activeDraftWire.currY = mouseWorld.y;
      }

      this.renderDraftWire();
    }
  }

  onMouseUp(e) {
    this.isPanning = false;
    this.draggingNode = null;

    if (this.activeDraftWire) {
      let targetSocket = this.magnetizedSocket;
      if (!targetSocket) {
        targetSocket = e.target.closest('.port-socket');
      }

      if (targetSocket && targetSocket.dataset.portType === 'input') {
        const toNodeId = targetSocket.dataset.nodeId;
        const toPortName = targetSocket.dataset.portName;
        const toPortIndex = targetSocket.dataset.portIndex !== undefined ? parseInt(targetSocket.dataset.portIndex, 10) : 0;

        if (toNodeId !== this.activeDraftWire.fromNodeId) {
          this.app.addWire(
            this.activeDraftWire.fromNodeId,
            this.activeDraftWire.fromPort,
            this.activeDraftWire.fromIndex || 0,
            toNodeId,
            toPortName,
            toPortIndex
          );
        }
      }

      if (this.magnetizedSocket) {
        this.magnetizedSocket.classList.remove('magnetized');
        this.magnetizedSocket = null;
      }

      this.activeDraftWire = null;
      this.clearDraftWire();
      // Ensure wire elements and labels are restored / refreshed if drag was cancelled without a target
      this.app.updateWires();
    }
  }

  onWheel(e) {
    e.preventDefault();
    const zoomFactor = 1.1;
    const oldZoom = this.zoom;
    let newZoom = e.deltaY < 0 ? oldZoom * zoomFactor : oldZoom / zoomFactor;
    newZoom = Math.min(Math.max(0.3, newZoom), 2.5);

    const rect = this.container.getBoundingClientRect();
    const mouseX = e.clientX - rect.left;
    const mouseY = e.clientY - rect.top;

    this.panX = mouseX - (mouseX - this.panX) * (newZoom / oldZoom);
    this.panY = mouseY - (mouseY - this.panY) * (newZoom / oldZoom);
    this.zoom = newZoom;
    this.applyTransform();
  }

  onDrop(e) {
    e.preventDefault();
    const handlerType = e.dataTransfer.getData('text/plain');
    if (!handlerType) return;
    const pt = this.screenToWorld(e.clientX, e.clientY);
    this.app.createNodeFromSchema(handlerType, pt.x, pt.y);
  }

  zoomIn() {
    this.zoom = Math.min(this.zoom * 1.2, 2.5);
    this.applyTransform();
  }

  zoomOut() {
    this.zoom = Math.max(this.zoom / 1.2, 0.3);
    this.applyTransform();
  }

  resetZoom() {
    this.zoom = 1.0;
    this.panX = 100;
    this.panY = 60;
    this.applyTransform();
  }

  renderDraftWire() {
    let wireEl = document.getElementById('draft-wire');
    if (!wireEl) {
      wireEl = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      wireEl.setAttribute('id', 'draft-wire');
      wireEl.setAttribute('class', 'canvas-wire');
      wireEl.setAttribute(
        'style',
        'stroke: #38bdf8; stroke-width: 3px; stroke-dasharray: 6 3; pointer-events: none;'
      );
      this.svg.appendChild(wireEl);
    }
    const d = this.calculateBezier(
      this.activeDraftWire.startX,
      this.activeDraftWire.startY,
      this.activeDraftWire.currX,
      this.activeDraftWire.currY
    );
    wireEl.setAttribute('d', d);
  }

  clearDraftWire() {
    const wireEl = document.getElementById('draft-wire');
    if (wireEl) wireEl.remove();
  }

  calculateBezier(x1, y1, x2, y2) {
    const dx = Math.abs(x2 - x1) * 0.55;
    const cx1 = x1 + Math.max(dx, 40);
    const cy1 = y1;
    const cx2 = x2 - Math.max(dx, 40);
    const cy2 = y2;
    return `M ${x1} ${y1} C ${cx1} ${cy1}, ${cx2} ${cy2}, ${x2} ${y2}`;
  }
}

window.BuilderCanvas = BuilderCanvas;
