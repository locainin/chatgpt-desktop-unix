(() => {
  // Make long chats cheaper by lowering work on old offscreen turns
  const host = window.location.hostname || "";
  const trusted = /(^|\.)chatgpt\.com$/i.test(host)
    || /(^|\.)openai\.com$/i.test(host)
    || /(^|\.)oaistatic\.com$/i.test(host);
  if (!trusted) {
    return;
  }

  if (window.__chatgptDesktopLongChatPerfInstalled) {
    return;
  }
  window.__chatgptDesktopLongChatPerfInstalled = true;

  const styleId = "chatgpt-desktop-long-chat-perf-style";
  const optimizedClass = "__chatgptDesktopPerfOptimized";
  const recentClass = "__chatgptDesktopPerfRecent";
  const minMessageCount = 24;
  const keepRecentCount = 18;
  const viewportMargin = 1600;
  const maxNodes = 1000;
  const managedNodes = new Set();
  let updateScheduled = false;

  const ensureStyle = () => {
    if (document.getElementById(styleId)) {
      return;
    }

    const style = document.createElement("style");
    style.id = styleId;
    style.textContent = `
      html.__chatgptDesktopReducedMotion {
        scroll-behavior: auto !important;
      }
      html.__chatgptDesktopReducedMotion article,
      html.__chatgptDesktopReducedMotion li[data-message-author-role],
      html.__chatgptDesktopReducedMotion div[data-message-author-role] {
        animation: none !important;
        transition: none !important;
      }
      .${optimizedClass} {
        content-visibility: auto !important;
        contain: layout style paint !important;
        contain-intrinsic-size: 1px 900px !important;
      }
      .${recentClass} {
        content-visibility: visible !important;
        contain: none !important;
      }
    `;
    document.head.appendChild(style);
    document.documentElement.classList.add("__chatgptDesktopReducedMotion");
  };

  const collectMessageNodes = () => {
    const selectors = [
      "article[data-testid*='conversation-turn']",
      "li[data-message-author-role]",
      "div[data-message-author-role]",
      "article"
    ];
    const seen = new Set();
    const nodes = [];

    for (const selector of selectors) {
      const results = document.querySelectorAll(selector);
      for (const node of results) {
        if (!(node instanceof HTMLElement) || seen.has(node)) {
          continue;
        }
        seen.add(node);
        nodes.push(node);
      }
    }

    if (nodes.length <= maxNodes) {
      return nodes;
    }

    // Keep some old and new turns so both ends still get handled
    const tailCount = Math.floor(maxNodes * 0.35);
    const headCount = maxNodes - tailCount;
    return nodes.slice(0, headCount).concat(nodes.slice(nodes.length - tailCount));
  };

  const clearManagedNodes = () => {
    for (const node of managedNodes) {
      if (!(node instanceof HTMLElement)) {
        continue;
      }
      node.classList.remove(optimizedClass);
      node.classList.remove(recentClass);
    }
    managedNodes.clear();
  };

  const isNearViewport = (node) => {
    const rect = node.getBoundingClientRect();
    return rect.bottom >= -viewportMargin
      && rect.top <= (window.innerHeight + viewportMargin);
  };

  const updateOptimization = () => {
    for (const node of Array.from(managedNodes)) {
      if (!(node instanceof HTMLElement) || !node.isConnected) {
        managedNodes.delete(node);
      }
    }

    const hasChatTurns = !!document.querySelector(
      "article[data-testid*='conversation-turn'],li[data-message-author-role],div[data-message-author-role]"
    );
    if (!hasChatTurns) {
      clearManagedNodes();
      return;
    }

    const messages = collectMessageNodes();
    if (messages.length < minMessageCount) {
      clearManagedNodes();
      return;
    }

    const recentStart = Math.max(0, messages.length - keepRecentCount);
    for (let index = 0; index < messages.length; ++index) {
      const message = messages[index];
      managedNodes.add(message);
      const isRecent = index >= recentStart;
      const nearViewport = isNearViewport(message);
      const shouldOptimize = !isRecent && !nearViewport;
      message.classList.toggle(optimizedClass, shouldOptimize);
      message.classList.toggle(recentClass, !shouldOptimize);
    }
  };

  const scheduleUpdate = () => {
    if (updateScheduled) {
      return;
    }
    updateScheduled = true;

    const run = () => {
      updateScheduled = false;
      updateOptimization();
    };

    if (typeof window.requestIdleCallback === "function") {
      window.requestIdleCallback(run, { timeout: 250 });
      return;
    }

    window.setTimeout(run, 120);
  };

  ensureStyle();
  scheduleUpdate();

  const observer = new MutationObserver(() => {
    scheduleUpdate();
  });
  observer.observe(document.documentElement, { childList: true, subtree: true });

  window.addEventListener("scroll", scheduleUpdate, { passive: true });
  window.addEventListener("resize", scheduleUpdate, { passive: true });
})();
