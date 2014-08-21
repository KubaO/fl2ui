#ifndef FL2UI_READ_H
#define FL2UI_READ_H

#include <QString>

class QTextStream;

QString readWord(QTextStream & in, bool readBrace = false);

#endif // FL2UI_READ_H
