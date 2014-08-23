// This file is derived from the following FLTK source file:
// fluid/file.cxx
// It is provided under the terms of the FLTK License, which
// gives you some rights in addition to LGPL v.2

#include <QTextStream>

static bool isBrace(QChar c)
{
    return c == '{' || c == '}';
}

static bool unread(QTextStream & in)
{
    return in.seek(in.pos() - 1);
}

static int hexdigit(QChar c)
{
    if (c.isDigit()) return c.unicode()-'0';
    if (c.isUpper()) return c.unicode()-'A'+10;
    if (c.isLower()) return c.unicode()-'a'+10;
    return 20;
}

static QChar readQuoted(QTextStream & in)
{
    QChar ch;
    in >> ch;
    int c = ch.unicode();
    switch (c) {
    case '\n': return QChar::Null;
    case 'a' : return '\a';
    case 'b' : return '\b';
    case 'f' : return '\f';
    case 'n' : return '\n';
    case 'r' : return '\r';
    case 't' : return '\t';
    case 'v' : return '\v';
    case 'x' : {
        // read hex
        c = 0;
        for (int x = 0; x < 3; x++) {
            if (in.atEnd()) break;
            in >> ch;
            int d = hexdigit(ch);
            if (d > 15) {
                unread(in);
                break;
            }
            c = (c << 4) + d;
        }
        break;
    }
    default:
        // read octal
        if (c<'0' || c>'7') break;
        c -= '0';
        for (int x=0; x<2; x++) {
            if (in.atEnd()) break;
            in >> ch;
            int d = hexdigit(ch);
            if (d > 7) {
                unread(in);
                break;
            }
            c = (c << 3) + d;
        }
        break;
    }
    return QChar(c);
}

QString readWord(QTextStream & in, bool readBrace)
{
    QString result;
    QChar c;

    // Skip the whitespace
    forever {
        if (in.atEnd()) return result;
        in >> c;
        if (c == '#') {
            in.readLine();
        }
        else if (! c.isSpace()) {
            break;
        }
    }
    result.reserve(100);
    if (c == '{' && ! readBrace) {
        // Read between the braces
        int level = 1;
        forever {
            if (in.atEnd()) return result;
            in >> c;
            if (c == '#') {
                in.readLine();
                continue;
            }
            else if (c == '\\') {
                c = readQuoted(in);
                if (c.isNull()) continue;
            }
            else if (c == '{') {
                ++ level;
            }
            else if (c == '}') {
                if (! --level) break;
            }
            result.append(c);
        }
    }
    else if (isBrace(c)) {
        // Read the braces themselves
        result = c;
    }
    else {
        // Read a single word
        forever {
            if (c == '\\') {
                c = readQuoted(in);
            }
            else if (c.isSpace() || isBrace(c) || c == '#') {
                unread(in);
                break;
            }
            if (! c.isNull()) result.append(c);
            if (in.atEnd()) break;
            in >> c;
        }
    }
    return result;
}
