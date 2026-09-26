#!/usr/bin/env python3
"""Commit message check.

Subject and body: only ASCII printable characters, Russian letters and №.
Co-authored-by trailers naming a bot or an AI assistant are forbidden.

Usage: check_commits.py REVISION_RANGE
       check_commits.py --message-file FILE
"""

import re
import subprocess
import sys
import unicodedata

from check_text import is_comment_char

CO_AUTHOR = re.compile(r'^\s*co-authored-by:(.*)$', re.IGNORECASE | re.MULTILINE)
BOT = re.compile(r'\[bot\]|\bbot\b|anthropic|claude|copilot|cursor|codex|openai|chatgpt|gemini|devin|aider',
                 re.IGNORECASE)


def check_message(name, message):
    errors = []
    seen = set()
    for line_no, line in enumerate(message.split('\n'), 1):
        for column, ch in enumerate(line, 1):
            if not is_comment_char(ch) and ch not in seen:
                seen.add(ch)
                errors.append(f'{name}:{line_no}:{column}: character not allowed'
                              f' U+{ord(ch):04X} {unicodedata.name(ch, "unnamed")}')
    for match in CO_AUTHOR.finditer(message):
        if BOT.search(match.group(1)):
            errors.append(f'{name}: bot co-authorship not allowed:{match.group(1)}')
    return errors


def git(*args):
    return subprocess.run(['git', *args], check=True, capture_output=True).stdout.decode('utf-8')


def main(argv):
    if len(argv) == 2 and argv[0] == '--message-file':
        with open(argv[1], encoding='utf-8') as f:
            lines = [l for l in f.read().split('\n') if not l.startswith('#')]
        errors = check_message('commit message', '\n'.join(lines).strip())
    elif len(argv) == 1:
        errors = []
        for sha in git('rev-list', argv[0]).split():
            errors.extend(check_message(sha[:12], git('log', '-1', '--format=%B', sha).strip()))
    else:
        print(__doc__)
        return 2

    for error in errors:
        print(error)
    if errors:
        print(f'{len(errors)} problem(s); commit messages allow only ASCII, Russian letters and №,'
              ' bot co-authors are forbidden')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
