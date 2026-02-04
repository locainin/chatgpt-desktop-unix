# chatgpt-desktop-unix

Unofficial ChatGPT desktop application built with Qt 6. Currently supports Linux; BSD and other Unix platforms coming soon.

## Install (AUR build from this repo)

From the repository root:

```bash
paru -B -i packaging/aur
```

Or with makepkg:

```bash
cd packaging/aur
makepkg -si --cleanbuild
```

## Launcher

The AUR package installs:

- `chatgpt-desktop-unix.desktop` into `$PREFIX/share/applications`
- `chatgpt-desktop-unix.png` into `$PREFIX/share/pixmaps`

The launcher entry is named **ChatGPT Desktop**.

## Persistence and Storage

Qt WebEngine stores cookies and local storage on disk. This project:

- Uses a dedicated, disk-backed `QWebEngineProfile`
- Forces persistent cookies
- Flushes on cookie changes and on shutdown

Default data locations:

- `$HOME/.local/share/chatgpt-desktop-unix` for persistent storage
- `$HOME/.cache/chatgpt-desktop-unix` for cache

## Privacy

This wrapper does not implement additional telemetry or logging. Network traffic is driven by the embedded web content and Qt WebEngine.

## Upcoming Plans

- Cookies support (to keep ChatGPT account logged in permanently) (done)
- Add support for [sora.chatgpt.com](https://sora.chatgpt.com) for generating videos  
- Add support for OpenAI API websites and Playground  
- Qt5 support  
- GTK4 support  
- GTK3 support  
- Support for BSD platforms  
- Publish to PPA for Ubuntu  
- Publish to openSUSE Build Service  
- Publish to COPR for Fedora  
- Publish to Debian repositories  
- Publish to Arch official repositories (currently in AUR)  
- Publish to FreeBSD FreshPorts  
- Publish to pkgsrc  
- Publish to OpenCSW (Solaris)  
- And more...
