#!/bin/sh

[ -z "$VERSION" ] && VERSION=$(git describe --tags --match='v[0-9].*' 2>/dev/null) \
                  && VERSION=${VERSION#v}

[ -z "$VERSION" ] && VERSION=$(cat VERSION 2>/dev/null)

[ -z "$VERSION" ] && VERSION=0.0.0

[ "$1" = major  ] && printf "%s" "${VERSION%%.*}" \
                  && exit 0

printf "%s" "$VERSION" | tee VERSION
