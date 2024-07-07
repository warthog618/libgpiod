#!/bin/sh
set -e
if [ -z "$READTHEDOCS" ]; then
    export READTHEDOCS_LANGUAGE=en
    export READTHEDOCS_VERSION=latest
    READTHEDOCS_OUTPUT=../_readthedocs
fi
BASEDIR="$(dirname "$0")"
cd "${BASEDIR}"
rm -rf _doxygen _build
python -m sphinx -T -d _build/doctrees . "${READTHEDOCS_OUTPUT}/html/${READTHEDOCS_LANGUAGE}/${READTHEDOCS_VERSION}"
