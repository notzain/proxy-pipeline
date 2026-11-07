(() => {
  const storageKeys = {
    chain: "proxy.studio.chain",
    backend: "proxy.studio.backend",
  };

  function readContext() {
    const params = new URLSearchParams(window.location.search);
    let chain = params.get("chain") || "";
    let backend = params.get("backend") || "";
    try {
      chain ||= sessionStorage.getItem(storageKeys.chain) || "";
      backend ||= sessionStorage.getItem(storageKeys.backend) || "";
    } catch (error) {
      // The current page can still use URL context when storage is disabled.
    }
    return { chain, backend };
  }

  function updateWorkspaceLinks(context) {
    if (!context.chain) return;
    document.querySelectorAll('a[href^="/builder"], a[href^="/dashboard"], a[href^="/telemetry"]').forEach((link) => {
      const target = new URL(link.href, window.location.origin);
      target.searchParams.set("chain", context.chain);
      if (context.backend) target.searchParams.set("backend", context.backend);
      link.href = `${target.pathname}${target.search}${target.hash}`;
    });
  }

  function setContext(chain, backend) {
    const context = { chain: chain || "", backend: backend || "" };
    try {
      if (context.chain) sessionStorage.setItem(storageKeys.chain, context.chain);
      else sessionStorage.removeItem(storageKeys.chain);
      if (context.backend) sessionStorage.setItem(storageKeys.backend, context.backend);
      else sessionStorage.removeItem(storageKeys.backend);
    } catch (error) {
      // URL context remains available when storage is disabled.
    }
    updateWorkspaceLinks(context);
  }

  function addSharedInteractionSupport() {
    const style = document.createElement("style");
    style.textContent = `
      button:focus-visible, a:focus-visible, input:focus-visible,
      select:focus-visible, textarea:focus-visible, [tabindex]:focus-visible {
        outline: 2px solid #7dd3fc;
        outline-offset: 3px;
      }
      .workspace-badge-btn[aria-expanded="true"] .badge-caret {
        transform: rotate(180deg);
      }
      .studio-run-context {
        display: flex;
        flex-wrap: wrap;
        gap: 4px 8px;
        min-height: 28px;
        align-items: center;
        line-height: 1.4;
      }
    `;
    document.head.append(style);

    document.querySelectorAll(".workspace-dropdown").forEach((dropdown) => {
      const button = dropdown.querySelector(".workspace-badge-btn");
      const menu = dropdown.querySelector(".workspace-menu");
      if (!button || !menu) return;

      menu.id ||= "studio-workspace-menu";
      button.setAttribute("aria-controls", menu.id);
      button.setAttribute("aria-expanded", "false");
    });

    window.toggleWorkspaceDropdown = (event) => {
      event.stopPropagation();
      const dropdown = event.currentTarget.closest(".workspace-dropdown");
      if (!dropdown) return;
      const isOpen = dropdown.classList.toggle("open");
      dropdown.querySelector(".workspace-badge-btn")?.setAttribute("aria-expanded", String(isOpen));
    };

    document.addEventListener("click", () => {
      document.querySelectorAll(".workspace-dropdown.open").forEach((dropdown) => {
        dropdown.classList.remove("open");
        dropdown.querySelector(".workspace-badge-btn")?.setAttribute("aria-expanded", "false");
      });
    });

    document.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      const openDropdown = document.querySelector(".workspace-dropdown.open");
      if (!openDropdown) return;
      openDropdown.classList.remove("open");
      const button = openDropdown.querySelector(".workspace-badge-btn");
      button?.setAttribute("aria-expanded", "false");
      button?.focus();
    });
  }

  function addRunStatus() {
    const header = document.querySelector("header");
    if (!header) return;

    const banner = document.createElement("div");
    banner.className = "studio-run-context";
    banner.setAttribute("role", "status");
    banner.setAttribute("aria-live", "polite");
    banner.style.cssText = "padding:4px 16px;background:#111c2f;color:#cbd5e1;border-bottom:1px solid #334155;font:11px ui-monospace,monospace";
    banner.textContent = "Runner status: connecting…";
    header.insertAdjacentElement("afterend", banner);

    const update = async () => {
      try {
        const response = await fetch("/api/runner/status", { cache: "no-store" });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const status = await response.json();
        const context = readContext();
        const chain = status.chainName || (status.configPath && status.configPath.split(/[\\/]/).pop()) || context.chain;
        const backend = status.backend || context.backend;
        const state = String(status.state || "idle").toUpperCase();
        const details = [chain, backend && backend.toUpperCase()].filter(Boolean).join(" · ");
        const message = `Runner ${state}${details ? ` — ${details}` : ""}`;
        if (banner.textContent !== message) banner.textContent = message;
        banner.dataset.state = status.state || "idle";
      } catch (error) {
        if (banner.textContent !== "Runner status unavailable") {
          banner.textContent = "Runner status unavailable";
        }
        banner.dataset.state = "unavailable";
      }
    };

    update();
    window.setInterval(update, 2000);
  }

  const context = readContext();
  setContext(context.chain, context.backend);
  addSharedInteractionSupport();
  addRunStatus();

  window.ProxyStudioContext = { set: setContext };
})();
