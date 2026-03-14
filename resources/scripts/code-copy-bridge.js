(() => {
  // Copy code blocks with native clipboard
  // C++ swaps this placeholder at inject time
  const trustedOrigins = globalThis.__chatgptDesktopTrustedOrigins;
  if (!trustedOrigins?.isTrustedLocation(window.location)) {
    return;
  }
  if (window.__chatgptDesktopCodeCopyInstalled) {
    return;
  }
  // Install once so repeated script injection does not duplicate handlers
  window.__chatgptDesktopCodeCopyInstalled = true;

  // Save prompt early so page script changes cannot fake the bridge
  const nativePrompt = (typeof window.prompt === "function")
    ? window.prompt.bind(window)
    : null;
  const copyPrefix = "__CHATGPT_DESKTOP_COPY_PREFIX_PLACEHOLDER__";
  const maxClipboardBytes = 8 * 1024 * 1024;
  const maxBase64Chars = Math.ceil(maxClipboardBytes / 3) * 4;

  const hasNearbyCodeBlock = (control) => {
    // Stay close to the clicked control so unrelated code blocks are ignored
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
      // Walk the real event path first so nested icons still resolve to the button
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
      // Fallback for browsers or events without a composed path
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
    // Walk upward a short distance first because the matching pre is usually nearby
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

      // Pick the closest visible pre to the clicked control
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
    // Use the nearest pre block and normalize newlines before native copy
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
    if (utf8.length === 0 || utf8.length > maxClipboardBytes) {
      // Keep one hard size cap so giant copies do not blow up memory use
      return "";
    }

    // Chunk length must be divisible by 3 so partial base64 blocks do not break
    const chunkSize = 3 * 4096;
    let base64 = "";
    for (let start = 0; start < utf8.length; start += chunkSize) {
      const end = Math.min(start + chunkSize, utf8.length);
      let chunk = "";
      for (let index = start; index < end; ++index) {
        chunk += String.fromCharCode(utf8[index]);
      }
      base64 += btoa(chunk);
      if (base64.length > maxBase64Chars) {
        return "";
      }
    }
    return base64;
  };

  const sendNativeCopy = (text) => {
    const base64 = encodeTextAsBase64(text);
    if (!base64 || !nativePrompt) {
      return false;
    }

    try {
      // Native bridge returns ok only when the app accepted the copy
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
      // Retry once after the page click settles so clipboard state sticks
      sendNativeCopy(codeText);
    }, 150);
  }, true);
})();
