#!/usr/bin/env python3
"""
pascal_strings.py - turn "\\p..." Pascal string literals into plain C ones.

The Mac OS 9 build has Retro68's gcc, which understands -fpascal-strings.
The Mac OS X cross compilers do not (gcc 14 for powerpc-apple-darwin8, and
clang targeting i386-apple-darwin8, both without Apple's extension), so
"\\pFile" would compile to the five characters "pFile". Rather than touch the
sources, the osx-legacy build runs each file that contains one through this
script and compiles the copy:

    "\\pFile"        ->   "\\004File"
    "\\p"            ->   "\\000"
    "\\p\\024"       ->   "\\001\\024"
    "\\pA" "BC"      ->   "\\003A" "BC"       (adjacent literals concatenate)

The length is counted after C escape decoding, so "\\311" (one MacRoman byte)
counts as one. It is written as a three-digit octal escape, which an
octal escape can never run on from: the next character is never swallowed.
Line numbers are preserved, so compiler messages still point into the
original file.

The file is read and written as Latin-1, byte for byte; a MacRoman or UTF-8
byte in a literal counts as one character, as the compiler counts it.

    pascal_strings.py [--cast] IN OUT   convert IN into OUT
    pascal_strings.py --check IN  exit 1 if IN contains a \\p literal
    pascal_strings.py --self-test
"""

import sys

SIMPLE_ESCAPES = set("abfnrtv\\'\"?")
OCTAL = set("01234567")
HEX = set("0123456789abcdefABCDEF")


class PascalError(Exception):
    pass


def decoded_length(body, where):
    """Characters in a string literal's body after escape decoding."""
    n = 0
    i = 0
    while i < len(body):
        c = body[i]
        if c != "\\":
            n += 1
            i += 1
            continue
        i += 1
        if i >= len(body):
            raise PascalError("%s: dangling backslash" % where)
        c = body[i]
        if c in OCTAL:
            j = i
            while j < len(body) and j < i + 3 and body[j] in OCTAL:
                j += 1
            i = j
            n += 1
        elif c == "x":
            j = i + 1
            while j < len(body) and body[j] in HEX:
                j += 1
            if j == i + 1:
                raise PascalError("%s: \\x with no digits" % where)
            i = j            # \x is greedy: it eats every hex digit after it
            n += 1
        elif c in SIMPLE_ESCAPES:
            i += 1
            n += 1
        elif c == "\n":
            i += 1           # line continuation inside a literal: no character
        elif c == "p":
            raise PascalError("%s: \\p is only meaningful at the start of a literal" % where)
        else:
            raise PascalError("%s: unknown escape \\%s" % (where, c))
    return n


def scan(text):
    """Yield ('lit', start, end) for each string literal; end is past the
    closing quote. Comments, character constants and everything else are
    stepped over, so a quote inside them is not mistaken for a literal."""
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//":
            while i < n and text[i] != "\n":
                # a backslash-newline continues a // comment
                if text[i] == "\\" and i + 1 < n and text[i + 1] == "\n":
                    i += 1
                i += 1
        elif two == "/*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == "'":
            i += 1
            while i < n and text[i] != "'":
                i += 2 if text[i] == "\\" else 1
            i += 1
        elif c == '"':
            start = i
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\n":
                    raise PascalError("unterminated string literal at offset %d" % start)
                i += 2 if text[i] == "\\" else 1
            i += 1
            yield (start, i)
        else:
            i += 1


def skip_gap(text, i):
    """Whitespace and comments between two literals that concatenate."""
    n = len(text)
    while i < n:
        if text[i].isspace():
            i += 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                return n
            i = j + 2
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        else:
            break
    return i


def convert(text, name="<input>", cast=False):
    """Return (converted text, number of literals converted).

    With cast, each converted literal (or run of them) is wrapped in
    ((const unsigned char *)...). C++ will not pass a char array where a
    ConstStr255Param is wanted, as C will, with a warning. Not for an array
    initialiser, where a cast is not allowed; nothing in a .cpp is one."""
    lits = list(scan(text))
    out = []
    last = 0
    count = 0
    k = 0
    while k < len(lits):
        start, end = lits[k]
        line = text.count("\n", 0, start) + 1
        where = "%s:%d" % (name, line)
        # The run of adjacent literals starting here: the compiler joins
        # them, so the Pascal length covers all of them.
        run = [(start, end)]
        while k + 1 < len(lits) and skip_gap(text, run[-1][1]) == lits[k + 1][0]:
            k += 1
            run.append(lits[k])
        k += 1

        bodies = [text[s + 1:e - 1] for (s, e) in run]
        if not bodies[0].startswith("\\p"):
            for b in bodies[1:]:
                if b.startswith("\\p"):
                    raise PascalError("%s: \\p begins a later literal of a run" % where)
            continue
        first = bodies[0][2:]
        total = decoded_length(first, where)
        for b in bodies[1:]:
            total += decoded_length(b, where)
        if total > 255:
            raise PascalError("%s: Pascal string of %d characters" % (where, total))
        s0 = run[0][0]
        if cast:
            out.append(text[last:s0])
            out.append("((const unsigned char *)")
            out.append(text[s0:s0 + 1])
        else:
            out.append(text[last:s0 + 1])
        out.append("\\%03o" % total)
        last = s0 + 3          # past the quote and the two characters of \p
        if cast:
            # the literal run ends where its last member does
            out.append(text[last:run[-1][1]])
            out.append(")")
            last = run[-1][1]
        count += 1
    out.append(text[last:])
    return "".join(out), count


def self_test():
    cases = [
        ('"\\pFile"', '"\\004File"'),
        ('"\\p"', '"\\000"'),
        ('"\\p\\024"', '"\\001\\024"'),
        ('"\\pAbout Gazette\\311"', '"\\016About Gazette\\311"'),
        ('"\\pA" "BC"', '"\\003A" "BC"'),
        ('"\\pA"\n  /* x */ "BC"', '"\\003A"\n  /* x */ "BC"'),
        ('"\\p\\x41\\x42z"', '"\\003\\x41\\x42z"'),       # \x41 and \x42 are two; z is one
        ('"\\p\\0123"', '"\\002\\0123"'),                  # \012 then '3'
        ('"\\p\\\\n\\n"', '"\\003\\\\n\\n"'),              # backslash, 'n', newline
        ('"\\p\\""', '"\\001\\""'),
        ('// "\\pnot"\nchar *s = "x";', '// "\\pnot"\nchar *s = "x";'),
        ("c = '\"'; \"\\pq\"", "c = '\"'; \"\\001q\""),
        ('"plain" "\\q"'.replace("\\q", "z"), '"plain" "z"'),
        ('"\\p" "\\x41"', '"\\001" "\\x41"'),
        ('x ? "\\pOn" : "\\pOff"', 'x ? "\\002On" : "\\003Off"'),
    ]
    for src, want in cases:
        got, _ = convert(src)
        if got != want:
            print("FAIL: %r -> %r, wanted %r" % (src, got, want))
            return 1
    got, _ = convert('f("\\pA" "BC", x ? "\\pZ" : "\\p");', cast=True)
    want = 'f(((const unsigned char *)"\\003A" "BC"), x ? ((const unsigned char *)"\\001Z") : ((const unsigned char *)"\\000"));'
    if got != want:
        print("FAIL cast: %r" % got)
        return 1
    try:
        convert('"\\p%s"' % ("a" * 256))
        print("FAIL: 256 characters accepted")
        return 1
    except PascalError:
        pass
    _, n = convert('"\\p%s"' % ("a" * 255))
    assert n == 1
    print("pascal_strings.py: self-test ok")
    return 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) == 3 and argv[1] == "--check":
        with open(argv[2], "rb") as f:
            text = f.read().decode("latin-1")
        _, n = convert(text, argv[2])
        return 1 if n else 0
    cast = False
    if len(argv) == 4 and argv[1] == "--cast":
        cast = True
        argv = [argv[0]] + argv[2:]
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[1], "rb") as f:
        text = f.read().decode("latin-1")
    try:
        new, n = convert(text, argv[1], cast)
    except PascalError as e:
        sys.stderr.write("pascal_strings.py: %s\n" % e)
        return 1
    with open(argv[2], "wb") as f:
        f.write(new.encode("latin-1"))
    sys.stderr.write("pascal_strings.py: %s: %d literal(s)\n" % (argv[1], n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
