"""classic_options_check.py LOG [--lang es] [--all] [--pages CONTROLS,GRAPHICS,...]

run_menus_test.sh cpsettings: Menu style Classic+'s option screens (classic/options.xml, menu_functions.c ce_opt_*)
as a run's log says them ("menus: ce options row: PAGE|NAME|LABEL|VALUE|state", one line a row each time a page
opens) checked against the settings panel's own table (port/vita/host/vita_settings.c settings[]) - read here from
the source, not through the game: each row's label is the panel row's label, its value one of that row's choices'
names, in capitals, in the run's language (port/vita/app0/lang/<lang>.txt, as T() has them) -, and every value set
("menus: ce options: set VAR=value (...); now X") the environment's at once (X the value). --all: every row of
port/linux/game/classic_options.h was shown; --pages: those pages' rows were. Exit 0 when all hold; each problem said.
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..')


def unescape(text):
    return text.replace('\\"', '"').replace('\\n', '\n').replace('\\t', '\t').replace('\\\\', '\\')


def translations(code):
    words = {}
    if code == 'en':
        return words
    with open(os.path.join(ROOT, 'port', 'vita', 'app0', 'lang', code + '.txt'), encoding='utf-8') as file:
        for line in file:
            match = re.match(r'^"((?:[^"\\]|\\.)*)" = "((?:[^"\\]|\\.)*)"', line)
            if match:
                words[unescape(match.group(1))] = unescape(match.group(2))
    return words


def capitals(text):
    """menu_functions.c h3_capitals: ASCII's and Latin-1's small letters (not the division sign)"""
    return ''.join(chr(ord(c) - 32) if ('a' <= c <= 'z') or ('à' <= c <= 'þ' and c != '÷') else c
                   for c in text)


def words(english, table):
    """ce_opt_words: T(), no leading spaces, capitals"""
    return capitals(table.get(english, english).lstrip(' '))


def strings_of(group):
    """a brace group's string literals, or None when it has anything else (a macro: VITA_BUTTON_NAMES)"""
    inner = group.strip()[1:-1].strip()
    if not inner:
        return []
    found = re.findall(r'"((?:[^"\\]|\\.)*)"', inner)
    rest = re.sub(r'"((?:[^"\\]|\\.)*)"', '', inner).replace(',', '').strip()
    return [unescape(text) for text in found] if not rest else None


def panel_rows():
    """vita_settings.c's settings[]: name (variable, or label of a row without one) -> (label, [names] or None)"""
    with open(os.path.join(ROOT, 'port', 'vita', 'host', 'vita_settings.c'), encoding='utf-8') as file:
        source = file.read()
    start = source.index('static struct setting settings[] = {')
    end = source.index('\n};', start)
    table = source[start:end]
    # (the shipped Menu style row, not the tests' hidden Halo 3 one)
    table = re.sub(r'#else\n\t/\* \(vita_settings_test\.c.*?#endif', '', table, flags=re.S)
    table = re.sub(r'/\*.*?\*/', '', table, flags=re.S)
    macros = {'GAME_FOLDER_ROW': 'HALO_GAME_FOLDER', 'PERFORMANCE_LOG': 'HALO_PERF_LOG'}
    rows = {}
    for match in re.finditer(r'\{\s*"((?:[^"\\]|\\.)*)",\s*(NULL|GAME_FOLDER_ROW|PERFORMANCE_LOG|"[A-Z_0-9]+")\s*,', table):
        label, variable = unescape(match.group(1)), match.group(2)
        variable = None if variable == 'NULL' else macros.get(variable, variable.strip('"'))
        # (restart, count, then the values' group and the names')
        rest = table[match.end():]
        groups = []
        at = 0
        while len(groups) < 2 and at < len(rest):
            brace = rest.find('{', at)
            if brace < 0:
                break
            depth, index = 0, brace
            while index < len(rest):
                depth += rest[index] == '{'
                depth -= rest[index] == '}'
                if depth == 0:
                    break
                index += 1
            groups.append(rest[brace:index + 1])
            at = index + 1
        names = strings_of(groups[1]) if len(groups) == 2 else None
        key = variable or label
        if key not in rows:
            rows[key] = (label, names)
    return rows


def classic_rows():
    """classic_options.h: every row's (page, name as the log says it)"""
    with open(os.path.join(ROOT, 'port', 'linux', 'game', 'classic_options.h'), encoding='utf-8') as file:
        source = file.read()
    pages = re.findall(r'\{ (?:"\w+"|NULL), CLASSIC_OPTIONS_\w+, N_\("([^"]+)"\) \}', source)
    order = re.findall(r'\tCLASSIC_OPTIONS_(\w+),\n', source[source.index('enum'):source.index('CLASSIC_OPTIONS_PAGES\n')])
    page_name = dict(zip(order, pages))
    rows = []
    for match in re.finditer(r'\{ CLASSIC_OPTIONS_(\w+), (NULL|CLASSIC_OPTIONS_PANEL|"[^"]+"), CLASSIC_OPTIONS_(\w+),', source):
        page, name, opens = match.groups()
        if name == 'NULL':
            name = page_name[opens]
        elif name == 'CLASSIC_OPTIONS_PANEL':
            name = 'SETTINGS PANEL'
        else:
            name = name.strip('"')
        rows.append((page_name[page], name))
    return rows, set(page_name.values())


def main():
    args = sys.argv[1:]
    log = args[0]
    lang = args[args.index('--lang') + 1] if '--lang' in args else 'en'
    wanted_pages = args[args.index('--pages') + 1].split(',') if '--pages' in args else []
    table = translations(lang)
    panel = panel_rows()
    rows, page_names = classic_rows()
    problems = []
    seen = set()
    checked = 0
    with open(log, encoding='utf-8', errors='replace') as file:
        lines = file.read().splitlines()
    for line in lines:
        match = re.search(r'menus: ce options row: ([^|]*)\|([^|]*)\|([^|]*)\|([^|]*)\|(.*)$', line)
        if match:
            page, name, label, value, state = match.groups()
            seen.add((page, name))
            if name in page_names or name == 'SETTINGS PANEL':
                want = words(name, table)
                if label != want:
                    problems.append('%s: the label %r, not %r' % (name, label, want))
                continue
            if name not in panel:
                problems.append('%s: not a row of the settings panel' % name)
                continue
            panel_label, names = panel[name]
            want = words(panel_label, table)
            if label != want:
                problems.append('%s: the label %r, not the panel\'s %r' % (name, label, want))
            # (a choice's name the panel's - but the languages', filled in from the files)
            if names and name != 'HALO_LANGUAGE' and value not in [words(each, table) for each in names]:
                problems.append('%s: the value %r is none of the panel\'s %s' % (name, value, names))
            checked += 1
            continue
        match = re.search(r'menus: ce options: set (\S+?)=(\S*) \(.*\); now (.*)$', line)
        if match and match.group(2) != match.group(3):
            problems.append('%s set to %r, the environment %r' % (match.group(1), match.group(2), match.group(3)))
        match = re.search(r'menus: ce options: set (\S+) \(typed\); now (.*)$', line)
        if match and match.group(2) in ('(unset)', 'not the password typed'):
            problems.append('%s typed, the environment %r' % (match.group(1), match.group(2)))
    expected = [row for row in rows if '--all' in args or row[0] in wanted_pages]
    missing = [row for row in expected if row not in seen]
    for page, name in missing:
        problems.append('%s > %s: never shown' % (page, name))
    for problem in problems:
        print('  ' + problem)
    print('classic options: %d rows shown, %d checked against the panel (%s), %d of %d expected rows shown' %
          (len(seen), checked, lang, len(expected) - len(missing), len(expected)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
