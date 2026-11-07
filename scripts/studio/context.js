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
        banner.textContent = `Runner ${state}${details ? ` — ${details}` : ""}`;
        banner.dataset.state = status.state || "idle";
      } catch (error) {
        banner.textContent = "Runner status unavailable";
        banner.dataset.state = "unavailable";
      }
    };

    update();
    window.setInterval(update, 2000);
  }

  const context = readContext();
  setContext(context.chain, context.backend);
  addRunStatus();

  window.ProxyStudioContext = { set: setContext };
})();
