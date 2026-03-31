from __future__ import annotations

from typing import Any


class PlaywrightBrowserManager:
    def __init__(self) -> None:
        self._playwright = None
        self._browser = None
        self._context = None
        self._page = None
        self._headless = True

    @property
    def available(self) -> bool:
        try:
            from playwright.sync_api import sync_playwright  # noqa: F401
        except Exception:
            return False
        return True

    def _reset_browser(self) -> None:
        if self._page is not None:
            try:
                self._page.close()
            except Exception:
                pass
        if self._context is not None:
            try:
                self._context.close()
            except Exception:
                pass
        if self._browser is not None:
            try:
                self._browser.close()
            except Exception:
                pass
        self._page = None
        self._context = None
        self._browser = None

    def _ensure_page(self, *, headless: bool):
        from playwright.sync_api import sync_playwright

        if self._playwright is None:
            self._playwright = sync_playwright().start()

        if self._browser is not None and self._headless != headless:
            self._reset_browser()

        if self._browser is None:
            self._browser = self._playwright.chromium.launch(headless=headless)
            self._context = self._browser.new_context()
            self._page = self._context.new_page()
            self._headless = headless

        return self._page

    def open_url(self, url: str, *, timeout_ms: int = 30000) -> dict[str, Any]:
        page = self._ensure_page(headless=False)
        page.goto(url, wait_until="domcontentloaded", timeout=timeout_ms)
        return {
            "url": page.url,
            "title": page.title(),
        }

    def fetch_text(self, url: str, *, timeout_ms: int = 30000) -> dict[str, Any]:
        page = self._ensure_page(headless=True)
        page.goto(url, wait_until="domcontentloaded", timeout=timeout_ms)
        text = page.locator("body").inner_text(timeout=timeout_ms)
        return {
            "url": page.url,
            "title": page.title(),
            "text": text,
        }

    def close(self) -> None:
        self._reset_browser()
        if self._playwright is not None:
            try:
                self._playwright.stop()
            except Exception:
                pass
            self._playwright = None
