#!/usr/bin/env python3
"""Set each GitHub release's title and notes from .github/release-notes/<tag>.md.

The first line of a file is the release title (a leading "# " is dropped); the rest
is the English release notes. The Korean title and notes a release had before its
first sync are kept below the English notes in a collapsed "한국어" section, so
running this again only replaces the English part.

usage: sync.py [--dry-run]
Needs the gh CLI with GH_TOKEN; GITHUB_REPOSITORY selects the repository.
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
START, END = '<!-- ko-notes -->', '<!-- /ko-notes -->'
KO_SECTION = re.compile(re.escape(START) + r'\n\n(.*?)\n\n' + re.escape(END), re.S)


def gh(*args, stdin=None):
    return subprocess.run(['gh', *args], check=True, text=True, input=stdin,
                          capture_output=True).stdout


def main():
    dry = '--dry-run' in sys.argv[1:]
    repo = os.environ.get('GITHUB_REPOSITORY', 'cailet0422/afe-port')
    files = sorted(HERE.glob('v*.md'))
    if not files:
        sys.exit('no release notes found')
    for f in files:
        tag = f.stem
        title_line, _, notes = f.read_text(encoding='utf-8').partition('\n')
        title, notes = title_line.removeprefix('# ').strip(), notes.strip()
        if not title or not notes:
            sys.exit(f'{f.name}: needs a title line and notes')

        cur = json.loads(gh('release', 'view', tag, '--repo', repo, '--json', 'name,body'))
        body = cur['body'].replace('\r\n', '\n')
        m = KO_SECTION.search(body)
        ko = m.group(1) if m else f"**{cur['name']}**\n\n{body.strip()}"
        # the Korean developer docs moved to docs/*.ko.md
        ko = ko.replace('/docs/README.md)에 있습니다', '/docs/README.ko.md)에 있습니다')
        new = (f'{notes}\n\n---\n\n<details>\n<summary>한국어</summary>\n\n'
               f'{START}\n\n{ko}\n\n{END}\n\n</details>\n')

        if new == body and title == cur['name']:
            print(f'{tag}: unchanged')
            continue
        print(f'{tag}: {"would update" if dry else "updating"} -> {title!r}'
              f' (Korean section {"kept" if m else "taken from the current notes"},'
              f' {len(ko)} chars)')
        if not dry:
            gh('release', 'edit', tag, '--repo', repo, '--title', title,
               '--notes-file', '-', stdin=new)


main()
