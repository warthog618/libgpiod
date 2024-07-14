# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2022 Kent Gibson <warthog618@gmail.com>

# This file is part of libgpiod.
#
# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

import subprocess

# build python for version and docs
subprocess.run("cd ../../.. ; ./autogen.sh --enable-bindings-python; make", shell=True)
# remove shared libs to trigger static linking to libgpiod
subprocess.run("rm ../../../lib/.libs/libgpiod.so*", shell=True)
# build gpiod._ext to allow importing of local gpiod module
subprocess.run("cd .. ; python build_ext.py gpiod", shell=True)

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys
sys.path.insert(0, os.path.abspath('..'))

# -- Project information -----------------------------------------------------

import gpiod

project = "libgpiod"
copyright = "2022, Bartosz Golaszewski"
author = "Bartosz Golaszewski"
language = os.environ['READTHEDOCS_LANGUAGE']
version = os.environ['READTHEDOCS_VERSION']
if version != 'latest':
    import gpiod
    version = gpiod.__version__
release = version

# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = ['sphinx.ext.autodoc' ]

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = []

# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = "sphinx_rtd_theme"

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

html_extra_path = []

html_css_files = [
    'css/custom.css',
]

