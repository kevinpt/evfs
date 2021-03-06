# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
# import os
# import sys
# sys.path.insert(0, os.path.abspath('.'))

import re
def parse_build_version(fname):
  with open(fname, 'r') as fh:
    for ln in fh.readlines():
      m = re.match(r'^#define\s+\w+_VERSION\s+"(.*)"', ln)
      if(m):
        return m.group(1)

  return '1.0.0'


# -- Project information -----------------------------------------------------

project = 'EVFS'
copyright = '2020, Kevin Thibedeau'
author = 'Kevin Thibedeau'

# The full version, including alpha/beta/rc tags
release = parse_build_version('../include/evfs/evfs_build_config.h')
print('BUILD RELEASE:', release)

# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = ['sphinx.ext.githubpages', 'hawkmoth']

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']


# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = 'alabaster'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

html_theme_options = {
  'description': 'Embedded Virtual Filesystem',
  'show_powered_by': False,
  'logo_text_align': 'center',
  'font_family': 'Verdana, Geneva, sans-serif',
  'github_user': 'kevinpt',
  'github_repo': 'evfs',
  'github_button': True
}

html_sidebars = {
    '**': [
        'about.html',
        'relations.html', # needs 'show_related': True theme option to display
        'localtoc.html',
        'projects.html',
        'searchbox.html'
    ],
    
    'index': [
        'about.html',
        'download.html',
        'relations.html',
        'localtoc.html',
        'projects.html',
        'searchbox.html'
    ]
}

html_logo = 'images/evfs.png'

cautodoc_root = '../src'
