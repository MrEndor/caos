set shell := ["bash", "-eou", "pipefail", "-c"]

default:
    @just --list

serve:
    uv run mkdocs serve

build:
    uv run mkdocs build

clean:
    rm -rf site/

deploy:
    uv run mkdocs gh-deploy --force

check-diagrams:
    python3 scripts/check_boxes.py

check: check-diagrams build

install:
    uv sync
