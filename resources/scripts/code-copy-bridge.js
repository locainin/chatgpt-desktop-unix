(() => {
  // Copy code blocks with native clipboard
  // C++ swaps this placeholder at inject time
  const host = window.location.hostname || "";
  const trusted = /(^|\.)chatgpt\.com$/i.test(host)
    || /(^|\.)openai\.com$/i.test(host)
    || /(^|\.)oaistatic\.com$/i.test(host);
  if (!trusted) {
    return;
  }
  if (window.__chatgptDesktopCodeCopyInstalled) {
    return;
  }
  window.__chatgptDesktopCodeCopyInstalled = true;

  // Save prompt early so page script changes cannot fake the bridge
  const nativePrompt = (typeof window.prompt === "function")
    ? window.prompt.bind(window)
    : null;
  const copyPrefix = "__CHATGPT_DESKTOP_COPY_PREFIX_PLACEHOLDER__";

  const hasNearbyCodeBlock = (control) => {
    const container = control.closest("article,[data-testid*='conversation-turn'],li[data-message-author-role],div[data-message-author-role],div")
      || control.parentElement
      || document;
    return !!container.querySelector("pre code, pre");
  };

  const isProbablyCopyControl = (control) => {
    if (!(control instanceof Element)) {
      return false;
    }

    const testId = (control.getAttribute("data-testid") || "").toLowerCase();
    const ariaLabel = (control.getAttribute("aria-label") || "").toLowerCase();
    const text = (control.textContent || "").toLowerCase();
    const looksLikeCopy = testId.includes("copy")
      || ariaLabel.includes("copy")
      || text.includes("copy");
    return looksLikeCopy && hasNearbyCodeBlock(control);
  };

  const findControlFromEvent = (event) => {
    if (typeof event.composedPath === "function") {
      const path = event.composedPath();
      for (const node of path) {
        if (!(node instanceof Element)) {
          continue;
        }
        const isControl = node.tagName === "BUTTON"
          || (node.getAttribute("role") || "").toLowerCase() === "button";
        if (isControl && isProbablyCopyControl(node)) {
          return node;
        }
      }
    }

    if (event.target instanceof Element) {
      const candidate = event.target.closest("button,[role='button']");
      if (candidate instanceof Element && isProbablyCopyControl(candidate)) {
        return candidate;
      }
    }

    return null;
  };

  const findTurnContainer = (control) => {
    return control.closest("article,[data-testid*='conversation-turn'],li[data-message-author-role],div[data-message-author-role]")
      || document;
  };

  const findPreByAncestor = (control) => {
    let node = control;
    for (let index = 0; index < 10 && node; ++index, node = node.parentElement) {
      const preOrCode = node.querySelector?.("pre code, pre");
      if (preOrCode) {
        return preOrCode.closest("pre") || preOrCode;
      }
    }
    return null;
  };

  const findNearestVisiblePre = (control) => {
    const root = findTurnContainer(control);
    const pres = Array.from(root.querySelectorAll("pre"));
    if (pres.length === 0) {
      return null;
    }

    const controlRect = control.getBoundingClientRect();
    const controlCenterX = controlRect.left + controlRect.width / 2;
    const controlCenterY = controlRect.top + controlRect.height / 2;

    let best = null;
    let bestDistance = Number.POSITIVE_INFINITY;
    for (const pre of pres) {
      const rect = pre.getBoundingClientRect();
      if (rect.width === 0 || rect.height === 0) {
        continue;
      }

      const preCenterX = rect.left + rect.width / 2;
      const preCenterY = rect.top + rect.height / 2;
      const dx = controlCenterX - preCenterX;
      const dy = controlCenterY - preCenterY;
      const distance = dx * dx + dy * dy;
      if (distance < bestDistance) {
        bestDistance = distance;
        best = pre;
      }
    }
    return best;
  };

  const extractCodeText = (control) => {
    const pre = findPreByAncestor(control) || findNearestVisiblePre(control);
    if (!pre) {
      return "";
    }
    const code = pre.querySelector("code");
    const text = code ? (code.textContent || "") : (pre.textContent || "");
    return text.replace(/\r\n/g, "\n");
  };

  const encodeTextAsBase64 = (text) => {
    if (typeof text !== "string" || text.length === 0) {
      return "";
    }

    const utf8 = new TextEncoder().encode(text);
    let binary = "";
    const chunkSize = 0x4000;
    for (let start = 0; start < utf8.length; start += chunkSize) {
      const end = Math.min(start + chunkSize, utf8.length);
      let chunk = "";
      for (let index = start; index < end; ++index) {
        chunk += String.fromCharCode(utf8[index]);
      }
      binary += chunk;
    }
    return btoa(binary);
  };

  const sendNativeCopy = (text) => {
    const base64 = encodeTextAsBase64(text);
    if (!base64 || !nativePrompt) {
      return false;
    }

    try {
      // Native bridge returns "ok" when copy worked
      const response = nativePrompt(`${copyPrefix}${base64}`, "");
      return response === "ok";
    } catch (_) {
      return false;
    }
  };

  document.addEventListener("pointerdown", (event) => {
    const control = findControlFromEvent(event);
    if (!control) {
      return;
    }

    const codeText = extractCodeText(control);
    if (!codeText || !codeText.trim()) {
      return;
    }

    // Stop page copy logic only when native copy worked
    const wasCopied = sendNativeCopy(codeText);
    if (!wasCopied) {
      return;
    }

    event.preventDefault();
    event.stopImmediatePropagation();

    setTimeout(() => {
      sendNativeCopy(codeText);
    }, 150);
  }, true);
})();
