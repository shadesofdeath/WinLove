"""Shared headless browser launcher for tools that rasterize SVG/HTML (brand, visual compare)."""


def launch(playwright):
    """Try Playwright's bundled Chromium, then locally installed Chrome / Edge."""
    errors = []
    for kwargs in ({}, {"channel": "chrome"}, {"channel": "msedge"}):
        try:
            return playwright.chromium.launch(**kwargs)
        except Exception as e:  # noqa: BLE001 - report all attempts together
            errors.append(f"{kwargs or 'bundled'}: {str(e).splitlines()[0]}")
    raise RuntimeError("No Chromium browser available:\n  " + "\n  ".join(errors))
