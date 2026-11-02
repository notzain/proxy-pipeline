/**
 * Canvas pipeline visualizer and particle kinematics engine.
 * Aligned with Visual Chain Builder node and link aesthetics.
 * Supports pan, zoom, and fixed-world layout coordinates.
 */
export class CanvasRenderer {
  constructor(canvasElement, store) {
    this.canvas = canvasElement;
    this.ctx = this.canvas.getContext("2d");
    this.store = store;

    this.handlersLayout = [];
    this.channelsLayout = [];
    this.activeParticles = [];

    // Pan & Zoom state matching BuilderCanvas
    this.panX = 60;
    this.panY = 40;
    this.zoom = 1.0;
    this.isPanning = false;
    this.panStartX = 0;
    this.panStartY = 0;
    this.hasInitialLayout = false;

    // Node dragging state
    this.draggingNode = null;
    this.nodeDragOffsetX = 0;
    this.nodeDragOffsetY = 0;

    this.resizeCanvas();
    window.addEventListener("resize", () => this.resizeCanvas());

    const wrap = this.canvas.parentElement;
    if (wrap) {
      new ResizeObserver(() => this.resizeCanvas()).observe(wrap);
    }

    this.setupInteractions();
    this.startLoop();
  }

  screenToWorld(screenX, screenY) {
    const rect = this.canvas.getBoundingClientRect();
    const x = (screenX - rect.left - this.panX) / this.zoom;
    const y = (screenY - rect.top - this.panY) / this.zoom;
    return { x, y };
  }

  worldToScreen(worldX, worldY) {
    const rect = this.canvas.getBoundingClientRect();
    const x = worldX * this.zoom + this.panX + rect.left;
    const y = worldY * this.zoom + this.panY + rect.top;
    return { x, y };
  }

  zoomIn() {
    this.zoom = Math.min(this.zoom * 1.2, 2.5);
  }

  zoomOut() {
    this.zoom = Math.max(this.zoom / 1.2, 0.35);
  }

  resetView() {
    this.zoom = 1.0;
    this.panX = 60;
    this.panY = 40;
  }

  resizeCanvas() {
    const wrap = this.canvas.parentElement;
    if (!wrap) return;
    const rect = wrap.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;

    this.canvas.width = Math.floor(rect.width * dpr);
    this.canvas.height = Math.floor(rect.height * dpr);
    this.canvas.style.width = `${rect.width}px`;
    this.canvas.style.height = `${rect.height}px`;

    this.ctx.setTransform(1, 0, 0, 1, 0, 0);
    this.ctx.scale(dpr, dpr);

    // Only do a fresh layout on initial run or when explicitly requested
    if (!this.hasInitialLayout) {
      this.layoutPipeline();
    }
  }

  getRoleTheme(role) {
    switch (role) {
      case "generator":
        return { bg: "#7c2d12", text: "#fdba74", label: "GENERATOR" };
      case "external_ingress":
        return { bg: "#14532d", text: "#86efac", label: "INGRESS" };
      case "external_egress":
        return { bg: "#1e3a8a", text: "#93c5fd", label: "EGRESS" };
      case "processor":
      default:
        return { bg: "#312e81", text: "#c4b5fd", label: "PROCESSOR" };
    }
  }

  calculateBezier(x1, y1, x2, y2) {
    const dx = Math.abs(x2 - x1) * 0.55;
    const cx1 = x1 + Math.max(dx, 40);
    const cy1 = y1;
    const cx2 = x2 - Math.max(dx, 40);
    const cy2 = y2;
    return { cx1, cy1, cx2, cy2 };
  }

  getBezierPoint(x1, y1, cx1, cy1, cx2, cy2, x2, y2, t) {
    const mt = 1 - t;
    const mt2 = mt * mt;
    const mt3 = mt2 * mt;
    const t2 = t * t;
    const t3 = t2 * t;

    const x = mt3 * x1 + 3 * mt2 * t * cx1 + 3 * mt * t2 * cx2 + t3 * x2;
    const y = mt3 * y1 + 3 * mt2 * t * cy1 + 3 * mt * t2 * cy2 + t3 * y2;
    return { x, y };
  }

  layoutPipeline() {
    const chain = this.store.state.chain;
    if (!chain || !chain.handlers) return;
    const handlers = chain.handlers;

    // Preserve custom dragged positions across state updates
    const existingPositions = new Map();
    if (this.handlersLayout && this.handlersLayout.length > 0) {
      this.handlersLayout.forEach((h) => {
        existingPositions.set(h.name, { x: h.x, y: h.y });
      });
    }

    this.handlersLayout = [];
    this.channelsLayout = [];

    const inDegrees = new Map();
    const outgoing = new Map();
    handlers.forEach((handler) => {
      inDegrees.set(handler.name, 0);
      outgoing.set(handler.name, []);
    });

    handlers.forEach((handler) => {
      const outList = handler.output_channels || (handler.output_channel ? String(handler.output_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
      outList.forEach((outCh) => {
        handlers.forEach((consumer) => {
          const inList = consumer.input_channels || (consumer.input_channel ? String(consumer.input_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
          if (inList.includes(outCh)) {
            outgoing.get(handler.name).push(consumer.name);
            inDegrees.set(consumer.name, (inDegrees.get(consumer.name) || 0) + 1);
          }
        });
      });
    });

    const ranks = new Map();
    const queue = [];
    handlers.forEach((handler) => {
      if ((inDegrees.get(handler.name) || 0) === 0) {
        ranks.set(handler.name, 0);
        queue.push(handler.name);
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
    handlers.forEach((handler) => {
      const r = ranks.get(handler.name) || 0;
      if (!columns.has(r)) columns.set(r, []);
      columns.get(r).push(handler);
    });

    // Use stable, generous fixed-world spacing so nodes do not get squashed when page / canvas is resized
    const nodeWidth = 220;
    const worldStartX = 180;
    const worldStartY = 60;
    const worldColSpacing = 320; // 320px between handler columns in world space
    const worldRowSpacing = 160; // 160px vertical separation

    columns.forEach((colHandlers, r) => {
      const colX = worldStartX + r * worldColSpacing;

      colHandlers.forEach((handler, rowIdx) => {
        const defaultNodeY = worldStartY + rowIdx * worldRowSpacing;
        const savedPos = existingPositions.get(handler.name);
        const posX = savedPos ? savedPos.x : colX;
        const posY = savedPos ? savedPos.y : defaultNodeY;
        const role = handler.role || (
          (!handler.input_channel && (!handler.input_channels || handler.input_channels.length === 0) && !!handler.output_channel)
            ? (handler.type === "udp_ingress_handler" ? "external_ingress" : "generator")
            : "processor"
        );
        const isGenerator = role === "generator";
        const isIngress = role === "external_ingress";
        const protocol = handler.protocol || (handler.type === "udp_ingress_handler" ? "udp" : "none");

        const inChs = handler.input_channels || (handler.input_channel ? String(handler.input_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
        const outChs = handler.output_channels || (handler.output_channel ? String(handler.output_channel).split(",").map(s => s.trim()).filter(Boolean) : []);

        const maxPortRows = Math.max(inChs.length, outChs.length, 1);
        const baseHeight = isGenerator ? 116 : 80;
        const calculatedHeight = Math.max(baseHeight, 60 + maxPortRows * 18);

        this.handlersLayout.push({
          name: handler.name,
          type: handler.type,
          role: role,
          protocol: protocol,
          isGenerator: isGenerator,
          isIngress: isIngress,
          x: posX,
          y: posY,
          width: nodeWidth,
          height: calculatedHeight,
          inCh: handler.input_channel,
          outCh: handler.output_channel,
          inChs: inChs,
          outChs: outChs,
          config: handler.config || {},
          inputSockets: [],
          outputSockets: [],
        });
      });
    });

    // Compute socket world positions on each handler card
    this.handlersLayout.forEach((h) => {
      const cardLeft = h.x - h.width / 2;
      const cardRight = h.x + h.width / 2;
      const portsStartY = (h.y - h.height / 2) + 52;
      const portSpacing = 18;

      h.inputSockets = [];
      if (h.inChs && h.inChs.length > 0) {
        h.inChs.forEach((ch, idx) => {
          h.inputSockets.push({
            channel: ch,
            index: idx,
            x: cardLeft,
            y: portsStartY + idx * portSpacing,
            label: h.inChs.length > 1 ? `in #${idx + 1}` : "input",
          });
        });
      }

      h.outputSockets = [];
      if (h.outChs && h.outChs.length > 0) {
        h.outChs.forEach((ch, idx) => {
          h.outputSockets.push({
            channel: ch,
            index: idx,
            x: cardRight,
            y: portsStartY + idx * portSpacing,
            label: h.outChs.length > 1 ? `out #${idx + 1}` : "output",
          });
        });
      }
    });

    // Construct DAG channels linking exact sockets
    handlers.forEach((handler) => {
      const fromNode = this.handlersLayout.find(item => item.name === handler.name);
      if (!fromNode) return;

      const outList = handler.output_channels || (handler.output_channel ? String(handler.output_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
      outList.forEach((outCh, outIdx) => {
        handlers.forEach((consumer) => {
          const inList = consumer.input_channels || (consumer.input_channel ? String(consumer.input_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
          const inIdx = inList.indexOf(outCh);
          if (inIdx !== -1) {
            const toNode = this.handlersLayout.find(item => item.name === consumer.name);
            if (fromNode && toNode) {
              const fromSocket = fromNode.outputSockets[outIdx] || { x: fromNode.x + fromNode.width / 2, y: fromNode.y };
              const toSocket = toNode.inputSockets[inIdx] || { x: toNode.x - toNode.width / 2, y: toNode.y };
              const bezier = this.calculateBezier(fromSocket.x, fromSocket.y, toSocket.x, toSocket.y);

              this.channelsLayout.push({
                name: outCh,
                from: fromNode,
                to: toNode,
                fromSocket,
                toSocket,
                bezier,
              });
            }
          }
        });
      });
    });

    this.hasInitialLayout = true;
  }

  updateNodeSocketsAndWires(node) {
    if (!node) return;
    const cardLeft = node.x - node.width / 2;
    const cardRight = node.x + node.width / 2;
    const portsStartY = (node.y - node.height / 2) + 52;
    const portSpacing = 18;

    if (node.inputSockets) {
      node.inputSockets.forEach((sock, idx) => {
        sock.x = cardLeft;
        sock.y = portsStartY + idx * portSpacing;
      });
    }
    if (node.outputSockets) {
      node.outputSockets.forEach((sock, idx) => {
        sock.x = cardRight;
        sock.y = portsStartY + idx * portSpacing;
      });
    }

    this.channelsLayout.forEach((ch) => {
      if (ch.from.name === node.name || ch.to.name === node.name) {
        ch.bezier = this.calculateBezier(ch.fromSocket.x, ch.fromSocket.y, ch.toSocket.x, ch.toSocket.y);
      }
    });

    // Update active particle endpoints if attached to this node
    this.activeParticles.forEach((p) => {
      if (p.targetNodeName === node.name) {
        if (p.isIngress) {
          const cloudW = 58;
          const cloudCenter = node.x - node.width / 2 - 16 - cloudW / 2;
          p.startX = cloudCenter - 24;
          p.startY = node.y;
          p.endX = cloudCenter;
          p.endY = node.y;
        } else if (p.targetEdge) {
          p.endX = p.targetEdge.toSocket.x;
          p.endY = p.targetEdge.toSocket.y;
          if (p.bezier) {
            p.bezier = p.targetEdge.bezier;
          }
        } else {
          p.endX = node.x;
          p.endY = node.y;
        }
      }
      if (p.sourceNodeName === node.name) {
        if (p.targetEdge) {
          p.startX = p.targetEdge.fromSocket.x;
          p.startY = p.targetEdge.fromSocket.y;
          if (p.bezier) {
            p.bezier = p.targetEdge.bezier;
          }
        } else {
          p.startX = node.x;
          p.startY = node.y;
        }
      }
    });
  }

  findIncomingChannelEdge(event) {
    if (!event || event.type === "ingress") return null;
    const handlerName = event.handler || event.target;
    const inCh = event.inputChannel || event.channel;
    if (!handlerName) return null;

    if (inCh) {
      const exact = this.channelsLayout.find(c => c.to.name === handlerName && c.name === inCh);
      if (exact) return exact;
    }
    if (event.source && event.source !== "ingress") {
      const bySource = this.channelsLayout.find(c => c.from.name === event.source && c.to.name === handlerName);
      if (bySource) return bySource;
    }
    return null;
  }

  findOutgoingChannelEdgeForChannel(handlerName, outCh) {
    if (!handlerName || !outCh) return null;
    return this.channelsLayout.find(c => c.from.name === handlerName && c.name === outCh) || null;
  }

  findOutgoingChannelEdges(event) {
    if (!event) return [];
    const handlerName = event.handler || event.source || event.target;
    if (!handlerName) return [];

    const edges = [];
    const traces = Array.isArray(event.traces) && event.traces.length > 0 ? event.traces : null;
    if (traces) {
      for (const tr of traces) {
        const oc = tr.outputChannel || tr.channel;
        const edge = this.findOutgoingChannelEdgeForChannel(handlerName, oc);
        if (edge && !edges.includes(edge)) {
          edges.push(edge);
        }
      }
    }

    if (edges.length === 0) {
      const outCh = event.outputChannel || event.channel;
      if (outCh) {
        for (const part of String(outCh).split(",")) {
          const edge = this.findOutgoingChannelEdgeForChannel(handlerName, part.trim());
          if (edge && !edges.includes(edge)) {
            edges.push(edge);
          }
        }
      }
    }
    return edges;
  }

  getEventPacketIds(event) {
    if (Array.isArray(event?.batchIds) && event.batchIds.length > 0) {
      return event.batchIds.map(String).sort();
    }
    return event?.msgId !== undefined && event?.msgId !== null ? [String(event.msgId)] : [];
  }

  triggerTransitionAnimation(event, specificTrace = null) {
    if (!event) return;
    const handlerName = event.handler || event.target;
    if (!handlerName) return;
    const handlerNode = this.handlersLayout.find(h => h.name === handlerName);
    if (!handlerNode) return;

    const isIngress = event.type === "ingress";
    const isForwarded = event.result === "Forwarded" || (specificTrace && specificTrace.result === "Forwarded");
    const isConsumed = (event.result === "Consumed" || (specificTrace && specificTrace.result === "Consumed"))
      || (event.type === "egress" && !isForwarded);
    const isDropped = event.type === "drop" || event.result === "Dropped" || (specificTrace && specificTrace.result === "Dropped");

    // If a specific sub-trace was requested, animate only that trace
    if (specificTrace) {
      const traceOutCh = specificTrace.outputChannel || specificTrace.channel;
      const edge = isForwarded
        ? this.findOutgoingChannelEdgeForChannel(handlerName, traceOutCh)
        : this.findIncomingChannelEdge(event);
      this._createAndSetParticles(event, handlerNode, [edge], [traceOutCh], isIngress, isForwarded, isConsumed, isDropped);
      return;
    }

    // If event has multiple traces that are forwarded, launch concurrent particles for each trace!
    const traces = Array.isArray(event.traces) && event.traces.length > 0 ? event.traces : null;
    if (traces && isForwarded && traces.length > 1) {
      const edges = [];
      const outChannels = [];
      for (const tr of traces) {
        const oc = tr.outputChannel || tr.channel;
        outChannels.push(oc);
        edges.push(this.findOutgoingChannelEdgeForChannel(handlerName, oc));
      }
      this._createAndSetParticles(event, handlerNode, edges, outChannels, isIngress, true, false, false);
      return;
    }

    // Single flow
    const defaultOutCh = event.outputChannel || event.channel || (traces && traces[0] ? (traces[0].outputChannel || traces[0].channel) : null);
    const edge = isForwarded
      ? this.findOutgoingChannelEdgeForChannel(handlerName, defaultOutCh)
      : this.findIncomingChannelEdge(event);
    this._createAndSetParticles(event, handlerNode, [edge], [defaultOutCh], isIngress, isForwarded, isConsumed, isDropped);
  }

  _createAndSetParticles(event, handlerNode, edges, outChannels, isIngress, isForwarded, isConsumed, isDropped) {
    const handlerName = handlerNode.name;
    const cloudW = 58;
    const cloudCenter = handlerNode.x - handlerNode.width / 2 - 16 - cloudW / 2;

    const previousEvent = this.lastAnimationEvent;
    const previousParticle = this.activeParticles[0];
    const packetIds = this.getEventPacketIds(event);
    const previousPacketIds = this.getEventPacketIds(previousEvent);
    const followsPreviousEvent = (
      previousEvent?.eventId !== undefined
      && event.eventId === previousEvent.eventId + 1
      && packetIds.length > 0
      && packetIds.length === previousPacketIds.length
      && packetIds.every((id, idx) => id === previousPacketIds[idx])
      && previousParticle
    );

    const newParticles = [];

    for (let i = 0; i < edges.length; i++) {
      const edge = edges[i];
      let startX;
      let startY;
      let endX;
      let endY;
      let bezier = null;

      if (isIngress) {
        startX = cloudCenter - 24;
        startY = handlerNode.y;
        endX = cloudCenter;
        endY = handlerNode.y;
      } else if (isForwarded) {
        if (edge) {
          startX = edge.fromSocket.x;
          startY = edge.fromSocket.y;
          endX = edge.toSocket.x;
          endY = edge.toSocket.y;
          bezier = edge.bezier;
        } else {
          const outSocket = handlerNode.outputSockets[i] || handlerNode.outputSockets[0] || { x: handlerNode.x + handlerNode.width / 2, y: handlerNode.y };
          startX = outSocket.x;
          startY = outSocket.y;
          endX = startX + 80;
          endY = startY;
        }
      } else if (isConsumed) {
        if (edge) {
          startX = edge.fromSocket.x;
          startY = edge.fromSocket.y;
          endX = edge.toSocket.x;
          endY = edge.toSocket.y;
          bezier = edge.bezier;
        } else {
          startX = cloudCenter;
          startY = handlerNode.y;
          endX = handlerNode.x;
          endY = handlerNode.y;
        }
      } else if (isDropped) {
        if (edge) {
          startX = edge.fromSocket.x;
          startY = edge.fromSocket.y;
          endX = edge.toSocket.x;
          endY = edge.toSocket.y;
          bezier = edge.bezier;
        } else {
          startX = cloudCenter;
          startY = handlerNode.y;
          endX = handlerNode.x;
          endY = handlerNode.y;
        }
      } else if (edge) {
        startX = edge.fromSocket.x;
        startY = edge.fromSocket.y;
        endX = edge.toSocket.x;
        endY = edge.toSocket.y;
        bezier = edge.bezier;
      } else if (!handlerNode.isGenerator) {
        const inSocket = handlerNode.inputSockets[0] || { x: handlerNode.x - handlerNode.width / 2, y: handlerNode.y };
        startX = cloudCenter;
        startY = handlerNode.y;
        endX = inSocket.x;
        endY = inSocket.y;
      } else {
        startX = handlerNode.x;
        startY = handlerNode.y;
        endX = handlerNode.x + handlerNode.width / 2 + 80;
        endY = handlerNode.y;
      }

      if (followsPreviousEvent && i === 0 && !isForwarded) {
        const elapsed = performance.now() - previousParticle.startTime;
        const linearProgress = Math.min(elapsed / previousParticle.durationMs, 1);
        const easedProgress = linearProgress * linearProgress * (3 - 2 * linearProgress);
        if (previousParticle.bezier) {
          const b = previousParticle.bezier;
          const pt = this.getBezierPoint(previousParticle.startX, previousParticle.startY, b.cx1, b.cy1, b.cx2, b.cy2, previousParticle.endX, previousParticle.endY, easedProgress);
          startX = pt.x;
          startY = pt.y;
        } else {
          startX = previousParticle.startX + (previousParticle.endX - previousParticle.startX) * easedProgress;
          startY = previousParticle.startY + (previousParticle.endY - previousParticle.startY) * easedProgress;
        }
        if (bezier) {
          bezier = this.calculateBezier(startX, startY, endX, endY);
        }
      }

      const transitColor = isIngress
        ? "#38bdf8"
        : (isForwarded || isDropped ? "#c084fc" : "#38bdf8");
      const arrivalColor = isIngress
        ? "#38bdf8"
        : (event.type === "drop" ? "#f87171" : (event.type === "egress" ? "#34d399" : "#c084fc"));

      const distance = Math.hypot(endX - startX, endY - startY);
      const durationMs = Math.min(750, Math.max(380, distance / 0.65));

      newParticles.push({
        startX,
        startY,
        endX,
        endY,
        bezier,
        targetEdge: edge,
        targetNodeName: edge ? edge.to.name : handlerName,
        sourceNodeName: edge ? edge.from.name : handlerName,
        progress: 0.0,
        startTime: performance.now(),
        durationMs,
        color: transitColor,
        arrivalColor: arrivalColor,
        dropColor: "#f87171",
        isIngress: isIngress,
        isConsumed: isConsumed,
        isDrop: event.type === "drop",
      });
    }

    this.activeParticles = newParticles;
    this.lastAnimationEvent = event;
  }

  setupInteractions() {
    let hasDragged = false;
    let downClientX = 0;
    let downClientY = 0;

    this.canvas.addEventListener("mousedown", (e) => {
      downClientX = e.clientX;
      downClientY = e.clientY;
      hasDragged = false;

      const worldPt = this.screenToWorld(e.clientX, e.clientY);

      // Check if clicking on a node
      let hitNode = null;
      for (let i = this.handlersLayout.length - 1; i >= 0; i--) {
        const h = this.handlersLayout[i];
        if (worldPt.x >= h.x - h.width / 2 && worldPt.x <= h.x + h.width / 2 &&
            worldPt.y >= h.y - h.height / 2 && worldPt.y <= h.y + h.height / 2) {
          // If clicked directly on generator trigger button, let click handler handle it
          if (h.isGenerator) {
            const btnW = 120;
            const btnH = 20;
            const btnX = h.x - btnW / 2;
            const btnY = (h.y + h.height / 2) - 28;
            if (worldPt.x >= btnX && worldPt.x <= btnX + btnW && worldPt.y >= btnY && worldPt.y <= btnY + btnH) {
              return; // Allow button click
            }
          }
          hitNode = h;
          break;
        }
      }

      if (hitNode) {
        this.draggingNode = hitNode;
        this.nodeDragOffsetX = worldPt.x - hitNode.x;
        this.nodeDragOffsetY = worldPt.y - hitNode.y;
        this.canvas.style.cursor = "grabbing";
      } else {
        // Pan with mouse drag on canvas background
        this.isPanning = true;
        this.panStartX = e.clientX - this.panX;
        this.panStartY = e.clientY - this.panY;
      }
    });

    window.addEventListener("mousemove", (e) => {
      const dist = Math.hypot(e.clientX - downClientX, e.clientY - downClientY);
      if (dist > 4) {
        hasDragged = true;
      }

      if (this.draggingNode) {
        const worldPt = this.screenToWorld(e.clientX, e.clientY);
        this.draggingNode.x = worldPt.x - this.nodeDragOffsetX;
        this.draggingNode.y = worldPt.y - this.nodeDragOffsetY;
        this.updateNodeSocketsAndWires(this.draggingNode);
      } else if (this.isPanning) {
        this.panX = e.clientX - this.panStartX;
        this.panY = e.clientY - this.panStartY;
      } else {
        // Update hover cursor
        const worldPt = this.screenToWorld(e.clientX, e.clientY);
        let onNode = false;
        for (const h of this.handlersLayout) {
          if (worldPt.x >= h.x - h.width / 2 && worldPt.x <= h.x + h.width / 2 &&
              worldPt.y >= h.y - h.height / 2 && worldPt.y <= h.y + h.height / 2) {
            onNode = true;
            break;
          }
        }
        this.canvas.style.cursor = onNode ? "grab" : "default";
      }
    });

    window.addEventListener("mouseup", () => {
      this.isPanning = false;
      if (this.draggingNode) {
        this.draggingNode = null;
        this.canvas.style.cursor = "default";
      }
    });

    // Zoom with mouse wheel (cursor centered)
    this.canvas.addEventListener("wheel", (e) => {
      e.preventDefault();
      const zoomFactor = 1.1;
      const oldZoom = this.zoom;
      let newZoom = e.deltaY < 0 ? oldZoom * zoomFactor : oldZoom / zoomFactor;
      newZoom = Math.min(Math.max(0.3, newZoom), 2.5);

      const rect = this.canvas.getBoundingClientRect();
      const mouseX = e.clientX - rect.left;
      const mouseY = e.clientY - rect.top;

      this.panX = mouseX - (mouseX - this.panX) * (newZoom / oldZoom);
      this.panY = mouseY - (mouseY - this.panY) * (newZoom / oldZoom);
      this.zoom = newZoom;
    }, { passive: false });

    // Click handler for node selection and generator triggers
    this.canvas.addEventListener("click", (evt) => {
      if (hasDragged) return; // Do not trigger click if user was panning

      const worldPt = this.screenToWorld(evt.clientX, evt.clientY);

      for (const h of this.handlersLayout) {
        if (worldPt.x >= h.x - h.width / 2 && worldPt.x <= h.x + h.width / 2 &&
            worldPt.y >= h.y - h.height / 2 && worldPt.y <= h.y + h.height / 2) {

          if (h.isGenerator) {
            const btnW = 120;
            const btnH = 20;
            const btnX = h.x - btnW / 2;
            const btnY = (h.y + h.height / 2) - 28;
            if (worldPt.x >= btnX && worldPt.x <= btnX + btnW && worldPt.y >= btnY && worldPt.y <= btnY + btnH) {
              this.store.triggerGenerator(h.name);
              return;
            }
          }

          this.store.inspectNode(h);
          return;
        }
      }
    });
  }

  startLoop() {
    const render = () => {
      this.draw();
      requestAnimationFrame(render);
    };
    requestAnimationFrame(render);
  }

  draw() {
    const ctx = this.ctx;
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

    ctx.save();
    // Apply pan and zoom viewport transform (matching builder)
    ctx.translate(this.panX, this.panY);
    ctx.scale(this.zoom, this.zoom);

    const lastEvent = this.store.state.lastEvent;
    const selectedNodeName = this.store.state.selectedNode ? this.store.state.selectedNode.name : null;
    const lastEventHandler = lastEvent ? (lastEvent.handler || lastEvent.target) : null;
    const activeIncomingEdge = this.findIncomingChannelEdge(lastEvent);
    const activeOutgoingEdges = this.findOutgoingChannelEdges(lastEvent);
    const activeEventEdges = lastEvent?.result === "Forwarded" ? activeOutgoingEdges : (activeIncomingEdge ? [activeIncomingEdge] : []);

    // 1. Draw Channel Connections (Cubic Bézier Wires & Pill Badges aligned with Chain Builder)
    this.channelsLayout.forEach((ch) => {
      const p1 = ch.fromSocket;
      const p2 = ch.toSocket;
      const b = ch.bezier;
      const isActiveEdge = activeEventEdges.includes(ch);

      // Draw wire curve
      ctx.save();
      if (isActiveEdge) {
        ctx.strokeStyle = "#34d399";
        ctx.lineWidth = 3.5;
        ctx.shadowColor = "rgba(52, 211, 153, 0.7)";
        ctx.shadowBlur = 12;
        ctx.setLineDash([6, 3]);
      } else {
        ctx.strokeStyle = "rgba(56, 189, 248, 0.45)";
        ctx.lineWidth = 2.5;
        ctx.setLineDash([]);
      }

      ctx.beginPath();
      ctx.moveTo(p1.x, p1.y);
      ctx.bezierCurveTo(b.cx1, b.cy1, b.cx2, b.cy2, p2.x, p2.y);
      ctx.stroke();
      ctx.restore();

      // Channel Name Pill Badge centered on wire curve
      const midPt = this.getBezierPoint(p1.x, p1.y, b.cx1, b.cy1, b.cx2, b.cy2, p2.x, p2.y, 0.5);
      const text = ch.name;
      ctx.font = "bold 11px monospace";
      const metrics = ctx.measureText(text);
      const pillWidth = metrics.width + 16;
      const pillHeight = 20;
      const pillX = midPt.x - pillWidth / 2;
      const pillY = midPt.y - pillHeight / 2;

      ctx.save();
      ctx.shadowColor = isActiveEdge ? "rgba(52, 211, 153, 0.4)" : "rgba(0, 0, 0, 0.6)";
      ctx.shadowBlur = 6;
      ctx.shadowOffsetY = 2;

      ctx.fillStyle = isActiveEdge ? "#0f231e" : "#0f172a";
      ctx.beginPath();
      ctx.roundRect(pillX, pillY, pillWidth, pillHeight, 5);
      ctx.fill();

      ctx.strokeStyle = isActiveEdge ? "#34d399" : "#38bdf8";
      ctx.lineWidth = isActiveEdge ? 1.5 : 1;
      ctx.stroke();

      ctx.shadowColor = "transparent";
      ctx.fillStyle = isActiveEdge ? "#6ee7b7" : "#f8fafc";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(text, midPt.x, midPt.y);
      ctx.restore();
    });

    // 2. Draw Handler Nodes (Modern Builder Card Aesthetics)
    this.handlersLayout.forEach((h) => {
      const x = h.x - h.width / 2;
      const y = h.y - h.height / 2;
      const isTarget = lastEvent && lastEvent.type !== "ingress" && lastEventHandler === h.name;
      const isSelected = selectedNodeName === h.name;

      // Ingress Queue Cloud Box (if UDP Ingress)
      const isIngressNode = h.isIngress || h.type === "udp_ingress_handler" || h.inCh === "udp_network" || (h.config && h.config["ingress.port"]);
      if (isIngressNode) {
        const cloudW = 58;
        const cloudH = h.height - 8;
        const cloudX = x - cloudW - 16;
        const cloudY = y + 4;

        ctx.save();
        const isIngressStaged = lastEvent && lastEvent.type === "ingress" && lastEventHandler === h.name;
        const isIngressEnteringNode = lastEvent && lastEvent.type !== "ingress" && lastEventHandler === h.name && !activeIncomingEdge;

        ctx.shadowColor = isIngressStaged ? "rgba(56, 189, 248, 0.8)" : (isIngressEnteringNode ? "rgba(192, 132, 252, 0.6)" : "rgba(56, 189, 248, 0.2)");
        ctx.shadowBlur = isIngressStaged ? 18 : (isIngressEnteringNode ? 12 : 6);

        ctx.fillStyle = isIngressStaged ? "rgba(12, 74, 110, 0.65)" : "rgba(15, 23, 42, 0.85)";
        ctx.beginPath();
        ctx.roundRect(cloudX, cloudY, cloudW, cloudH, [8, 4, 4, 8]);
        ctx.fill();

        ctx.strokeStyle = isIngressStaged ? "#38bdf8" : (isIngressEnteringNode ? "#c084fc" : "rgba(148, 163, 184, 0.45)");
        ctx.lineWidth = isIngressStaged ? 2.5 : 1.5;
        ctx.setLineDash([4, 3]);
        ctx.stroke();
        ctx.setLineDash([]);

        ctx.shadowBlur = 0;
        ctx.fillStyle = isIngressStaged ? "#7dd3fc" : "rgba(148, 163, 184, 0.85)";
        ctx.font = "bold 9px monospace";
        ctx.textAlign = "center";
        ctx.fillText("UDP IN", cloudX + cloudW / 2, cloudY + 18);
        ctx.font = "8px sans-serif";
        ctx.fillText("queue", cloudX + cloudW / 2, cloudY + 30);

        // Connector between UDP queue and ingress node
        const firstInSocket = h.inputSockets[0] || { x: x, y: h.y };
        ctx.strokeStyle = isIngressEnteringNode ? "#c084fc" : (isIngressStaged ? "#38bdf8" : "rgba(148, 163, 184, 0.35)");
        ctx.lineWidth = isIngressEnteringNode ? 3.5 : 1.5;
        if (isIngressEnteringNode) {
          ctx.shadowColor = "#c084fc";
          ctx.shadowBlur = 12;
          ctx.setLineDash([]);
        } else {
          ctx.setLineDash([2, 3]);
        }
        ctx.beginPath();
        ctx.moveTo(cloudX + cloudW, firstInSocket.y);
        ctx.lineTo(firstInSocket.x, firstInSocket.y);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.restore();
      }

      // Card Background & Shadow
      const activeParticle = this.activeParticles[0];
      const reachedNode = !activeParticle || activeParticle.progress >= 0.85;
      const dropReachedNode = lastEvent && lastEvent.type === "drop" && reachedNode;
      const isForwardedTarget = isTarget && lastEvent.type === "hop";
      const isConsumedTarget = isTarget && lastEvent.type === "egress";

      ctx.save();
      if (isSelected) {
        ctx.shadowColor = "rgba(56, 189, 248, 0.9)";
        ctx.shadowBlur = 22;
      } else if (isTarget) {
        ctx.shadowColor = dropReachedNode
          ? "rgba(248, 113, 113, 0.85)"
          : (isConsumedTarget ? "rgba(52, 211, 153, 0.85)" : (isForwardedTarget ? "rgba(192, 132, 252, 0.85)" : "rgba(56, 189, 248, 0.8)"));
        ctx.shadowBlur = 18;
      } else {
        ctx.shadowColor = "rgba(0, 0, 0, 0.5)";
        ctx.shadowBlur = 10;
        ctx.shadowOffsetY = 4;
      }

      // Card Body Fill
      ctx.fillStyle = isSelected
        ? "#0c4a6e"
        : (isTarget ? (dropReachedNode ? "#3b1824" : (isConsumedTarget ? "#064e3b" : (isForwardedTarget ? "#3b1d5e" : "#1e3a5f"))) : "#0f172a");
      ctx.beginPath();
      ctx.roundRect(x, y, h.width, h.height, 8);
      ctx.fill();

      // Card Border
      ctx.shadowColor = "transparent";
      ctx.strokeStyle = isSelected
        ? "#38bdf8"
        : (isTarget ? (dropReachedNode ? "#f87171" : (isConsumedTarget ? "#34d399" : (isForwardedTarget ? "#c084fc" : "#38bdf8"))) : "#334155");
      ctx.lineWidth = isSelected ? 2.5 : (isTarget ? 2 : 1);
      ctx.stroke();

      // Card Header Background
      const headerH = 28;
      ctx.fillStyle = "#1e293b";
      ctx.beginPath();
      ctx.roundRect(x, y, h.width, headerH, [7, 7, 0, 0]);
      ctx.fill();

      // Header bottom border
      ctx.strokeStyle = "#334155";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(x, y + headerH);
      ctx.lineTo(x + h.width, y + headerH);
      ctx.stroke();

      // Header Node Title
      ctx.fillStyle = "#f8fafc";
      ctx.font = "bold 12px sans-serif";
      ctx.textAlign = "left";
      ctx.textBaseline = "middle";
      const maxTitleW = h.width - 92;
      let title = h.name;
      if (ctx.measureText(title).width > maxTitleW) {
        title = title.substring(0, 14) + "...";
      }
      ctx.fillText(title, x + 10, y + headerH / 2);

      // Header Semantic Role Badge
      const roleTheme = this.getRoleTheme(h.role);
      const roleBadgeText = roleTheme.label;
      ctx.font = "bold 9px sans-serif";
      const roleW = ctx.measureText(roleBadgeText).width + 10;
      const roleH = 16;
      const roleX = x + h.width - roleW - 8;
      const roleY = y + (headerH - roleH) / 2;

      ctx.fillStyle = roleTheme.bg;
      ctx.beginPath();
      ctx.roundRect(roleX, roleY, roleW, roleH, 3);
      ctx.fill();

      ctx.fillStyle = roleTheme.text;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(roleBadgeText, roleX + roleW / 2, roleY + roleH / 2);

      // Node Body: Type Label
      ctx.fillStyle = "#94a3b8";
      ctx.font = "10px monospace";
      ctx.textAlign = "left";
      ctx.fillText(h.type, x + 10, y + headerH + 14);

      // Input Port Items & Sockets (Left)
      h.inputSockets.forEach((s) => {
        const socketR = 5;
        // Socket Circle
        ctx.fillStyle = s.channel ? "#10b981" : "#334155";
        ctx.beginPath();
        ctx.arc(s.x, s.y, socketR, 0, Math.PI * 2);
        ctx.fill();

        ctx.strokeStyle = s.channel ? "#34d399" : "#94a3b8";
        ctx.lineWidth = 1.5;
        ctx.stroke();

        // Port Label
        ctx.fillStyle = "#94a3b8";
        ctx.font = "10px sans-serif";
        ctx.textAlign = "left";
        ctx.textBaseline = "middle";
        ctx.fillText(s.label, s.x + 8, s.y);
      });

      // Output Port Items & Sockets (Right)
      h.outputSockets.forEach((s) => {
        const socketR = 5;
        // Socket Circle
        ctx.fillStyle = s.channel ? "#10b981" : "#334155";
        ctx.beginPath();
        ctx.arc(s.x, s.y, socketR, 0, Math.PI * 2);
        ctx.fill();

        ctx.strokeStyle = s.channel ? "#34d399" : "#94a3b8";
        ctx.lineWidth = 1.5;
        ctx.stroke();

        // Port Label
        ctx.fillStyle = "#94a3b8";
        ctx.font = "10px sans-serif";
        ctx.textAlign = "right";
        ctx.textBaseline = "middle";
        ctx.fillText(s.label, s.x - 8, s.y);
      });

      // Generator Interactive Trigger Button
      if (h.isGenerator) {
        const btnW = 120;
        const btnH = 20;
        const btnX = h.x - btnW / 2;
        const btnY = (y + h.height) - 28;

        ctx.fillStyle = "rgba(56, 189, 248, 0.18)";
        ctx.beginPath();
        ctx.roundRect(btnX, btnY, btnW, btnH, 4);
        ctx.fill();

        ctx.strokeStyle = "rgba(56, 189, 248, 0.65)";
        ctx.lineWidth = 1;
        ctx.stroke();

        ctx.fillStyle = "#38bdf8";
        ctx.font = "bold 9px sans-serif";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText("⚡ Trigger Emit", h.x, btnY + btnH / 2);
      }

      ctx.restore();
    });

    // 3. Fallback unlinked output forward stub (if Forwarded without consumer edge)
    if (lastEvent?.result === "Forwarded" && lastEvent.outputChannel && activeOutgoingEdges.length === 0) {
      const sourceNode = this.handlersLayout.find(h => h.name === lastEventHandler);
      if (sourceNode) {
        const startSocket = sourceNode.outputSockets[0] || { x: sourceNode.x + sourceNode.width / 2, y: sourceNode.y };
        const endX = startSocket.x + 80;
        ctx.save();
        ctx.strokeStyle = "#c084fc";
        ctx.lineWidth = 3;
        ctx.shadowColor = ctx.strokeStyle;
        ctx.shadowBlur = 12;
        ctx.beginPath();
        ctx.moveTo(startSocket.x, startSocket.y);
        ctx.lineTo(endX, startSocket.y);
        ctx.stroke();

        ctx.fillStyle = "#c084fc";
        ctx.font = "10px monospace";
        ctx.textAlign = "left";
        ctx.fillText(lastEvent.outputChannel, startSocket.x + 8, startSocket.y - 8);
        ctx.restore();
      }
    }

    // 4. Animate Particles along Bézier trajectories
    for (let i = this.activeParticles.length - 1; i >= 0; i--) {
      const p = this.activeParticles[i];
      if (p.progress < 1.0) {
        const elapsed = performance.now() - p.startTime;
        const linearProgress = Math.min(elapsed / p.durationMs, 1);
        p.progress = linearProgress * linearProgress * (3 - 2 * linearProgress);
      }

      let curX;
      let curY;
      if (p.bezier) {
        const b = p.bezier;
        const pt = this.getBezierPoint(p.startX, p.startY, b.cx1, b.cy1, b.cx2, b.cy2, p.endX, p.endY, p.progress);
        curX = pt.x;
        curY = pt.y;
      } else {
        curX = p.startX + (p.endX - p.startX) * p.progress;
        curY = p.startY + (p.endY - p.startY) * p.progress;
      }

      const reachedNode = p.progress >= 0.85;
      const reachedDropNode = p.isDrop && reachedNode;
      const activeColor = reachedNode ? (p.arrivalColor || p.color) : p.color;
      const consumeProgress = p.isConsumed ? Math.max(0, (p.progress - 0.72) / 0.28) : 0;
      const particleScale = p.isConsumed ? 1 - consumeProgress * 0.85 : 1;
      const particleAlpha = p.isConsumed ? 1 - consumeProgress : 1;

      // Particle Trail
      if (p.progress < 1.0) {
        ctx.save();
        ctx.globalAlpha = particleAlpha;
        ctx.strokeStyle = activeColor;
        ctx.lineWidth = 3 * particleScale;
        ctx.shadowColor = activeColor;
        ctx.shadowBlur = 12;
        // Draw curved trail along Bézier if bezier exists, else linear
        if (p.bezier) {
          const b = p.bezier;
          const trailT = Math.max(0, p.progress - 0.2);
          const steps = 8;
          ctx.beginPath();
          for (let step = 0; step <= steps; step++) {
            const t = trailT + (p.progress - trailT) * (step / steps);
            const pt = this.getBezierPoint(p.startX, p.startY, b.cx1, b.cy1, b.cx2, b.cy2, p.endX, p.endY, t);
            if (step === 0) {
              ctx.moveTo(pt.x, pt.y);
            } else {
              ctx.lineTo(pt.x, pt.y);
            }
          }
          ctx.stroke();
        } else {
          const trailT = Math.max(0, p.progress - 0.2);
          const trailStartX = p.startX + (p.endX - p.startX) * trailT;
          const trailStartY = p.startY + (p.endY - p.startY) * trailT;
          ctx.beginPath();
          ctx.moveTo(trailStartX, trailStartY);
          ctx.lineTo(curX, curY);
          ctx.stroke();
        }
        ctx.restore();
      }

      const headRadius = p.isIngress ? 8.5 : 7;

      if (!p.isConsumed || consumeProgress < 1) {
        ctx.save();
        ctx.globalAlpha = particleAlpha;
        ctx.shadowColor = activeColor;
        ctx.shadowBlur = p.isIngress ? 24 : 18;
        ctx.fillStyle = reachedDropNode ? "rgba(248, 113, 113, 0.95)" : "#ffffff";
        ctx.beginPath();
        ctx.arc(curX, curY, headRadius * particleScale, 0, Math.PI * 2);
        ctx.fill();

        ctx.fillStyle = activeColor;
        ctx.beginPath();
        ctx.arc(curX, curY, headRadius * 0.65 * particleScale, 0, Math.PI * 2);
        ctx.fill();

        if (p.isIngress && p.progress < 1.0) {
          ctx.strokeStyle = activeColor;
          ctx.lineWidth = 1.5;
          ctx.beginPath();
          ctx.arc(curX, curY, headRadius + 5, 0, Math.PI * 2);
          ctx.stroke();
        }
        ctx.restore();
      }

      if (p.isConsumed && consumeProgress > 0 && consumeProgress < 1) {
        ctx.save();
        ctx.globalAlpha = 1 - consumeProgress;
        ctx.strokeStyle = "#34d399";
        ctx.lineWidth = 2;
        ctx.shadowColor = "#34d399";
        ctx.shadowBlur = 12;
        ctx.beginPath();
        ctx.arc(curX, curY, headRadius + consumeProgress * 22, 0, Math.PI * 2);
        ctx.stroke();
        ctx.restore();
      }

      // Drop Crossmark (X)
      if (reachedDropNode) {
        ctx.save();
        if (p.progress < 1.0) {
          const ringProgress = (p.progress - 0.85) / 0.15;
          ctx.globalAlpha = Math.max(0, 1.0 - ringProgress);
          ctx.strokeStyle = "#f87171";
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.arc(curX, curY, (headRadius + 2) + ringProgress * 12, 0, Math.PI * 2);
          ctx.stroke();
        }

        ctx.globalAlpha = 1.0;
        ctx.strokeStyle = "#ffffff";
        ctx.lineWidth = 2.5;
        ctx.lineCap = "round";
        const crossSize = 5.5;
        ctx.beginPath();
        ctx.moveTo(curX - crossSize, curY - crossSize);
        ctx.lineTo(curX + crossSize, curY + crossSize);
        ctx.moveTo(curX + crossSize, curY - crossSize);
        ctx.lineTo(curX - crossSize, curY + crossSize);
        ctx.stroke();

        ctx.strokeStyle = "#ef4444";
        ctx.lineWidth = 2;
        const outerRingRadius = headRadius + 3.5;
        ctx.beginPath();
        ctx.arc(curX, curY, outerRingRadius, 0, Math.PI * 2);
        ctx.stroke();
        ctx.restore();
      }
    }

    // 5. Persistent Envelope Badge on current message location
    if (lastEvent) {
      const targetNode = this.handlersLayout.find(h => h.name === lastEventHandler);
      const activeParticle = this.activeParticles[0];
      const showDropState = lastEvent.type === "drop" && (!activeParticle || activeParticle.progress >= 0.85);

      if (targetNode && (lastEvent.type !== "drop" || showDropState)) {
        const cloudW = 58;
        const cloudCenter = targetNode.x - targetNode.width / 2 - 16 - cloudW / 2;
        const badgeX = lastEvent.type === "ingress" ? cloudCenter : targetNode.x;
        const badgeY = targetNode.y - targetNode.height / 2 - 16;

        ctx.save();
        ctx.shadowColor = showDropState
          ? "rgba(248, 113, 113, 0.75)"
          : (lastEvent.type === "egress" ? "rgba(52, 211, 153, 0.75)" : (lastEvent.type === "hop" ? "rgba(192, 132, 252, 0.75)" : "rgba(56, 189, 248, 0.7)"));
        ctx.shadowBlur = 12;
        ctx.fillStyle = showDropState
          ? "#991b1b"
          : (lastEvent.type === "egress" ? "#065f46" : (lastEvent.type === "hop" ? "#6b21a8" : "#0369a1"));
        ctx.font = "bold 10px monospace";

        const batchIds = Array.isArray(lastEvent.batchIds) ? lastEvent.batchIds : [];
        const packetLabel = batchIds.length > 1
          ? `P[${batchIds.join(",")}]`
          : `P${lastEvent.msgId}`;
        const actionLabel = lastEvent.result
          ? lastEvent.result.toUpperCase()
          : (lastEvent.type === "drop" ? "DROPPED" : (lastEvent.type === "egress" ? "CONSUMED" : (lastEvent.type === "ingress" ? "LOADED" : "HELD")));
        const statusText = `${packetLabel} ${actionLabel}`;
        const badgeWidth = Math.max(108, ctx.measureText(statusText).width + 16);

        ctx.beginPath();
        ctx.roundRect(badgeX - badgeWidth / 2, badgeY - 10, badgeWidth, 20, 6);
        ctx.fill();

        ctx.shadowBlur = 0;
        ctx.fillStyle = "#f8fafc";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText(statusText, badgeX, badgeY);
        ctx.restore();
      }
    }

    ctx.restore();
  }
}
