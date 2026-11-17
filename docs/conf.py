# SPDX-License-Identifier: MIT
# Sphinx configuration: python -m sphinx -W -b html docs docs/_build/html

project = "libiio-conv"
author = "libiio-conv contributors"
copyright = "2026, libiio-conv contributors"

extensions = ["myst_parser"]
source_suffix = {".md": "markdown"}
root_doc = "index"

# Development notes (specs, plans) are not part of the published pages.
exclude_patterns = ["_build", "superpowers", "requirements.txt"]

# Anchors for "#heading" links, as GitHub generates them.
myst_heading_anchors = 3

html_theme = "alabaster"
html_title = "libiio-conv"
