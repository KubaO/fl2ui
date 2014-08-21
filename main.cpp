#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QXmlStreamWriter>
#include <QDebug>
#include <cstdio>
#include "read.h"

QTextStream err(stderr);

bool isOk(QTextStream & in)
{
    return in.status() == QTextStream::Ok;
}

int convert(QTextStream & inRaw, QTextStream & out)
{
    QString input = inRaw.readAll();
    QString output;
    if (!isOk(inRaw)) {
        err << "Error reading the input" << endl;
        return 3;
    }
    QTextStream in(&input);
    QXmlStreamWriter writer(&output);

    //pTop(in, writer);

    out << output;
    if (!isOk(out)) {
        err << "Error writing the output" << endl;
        return 4;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    if (a.arguments().count() < 2) {
        QTextStream in(stdin);
        QTextStream out(stdout);
        return convert(in, out);
    }
    QString fInPath(a.arguments().at(1));
    QFile fIn(fInPath);
    if (! fIn.open(QIODevice::ReadOnly | QIODevice::Text)) {
        err << "Cannot open input file" << fInPath << endl;
        return 1;
    }
    QString fOutPath;
    if (a.arguments().count() >= 3) {
        fOutPath = a.arguments().at(2);
    } else {
        QFileInfo fi(fInPath);
        fOutPath = fi.path() + "/" + fi.baseName() + ".ui";
    }
    QSaveFile fOut(fOutPath);
    if (! fOut.open(QIODevice::WriteOnly | QIODevice::Text)) {
        err << "Cannot open output file" << fOutPath << endl;
        return 2;
    }
    QTextStream in(&fIn);
    QTextStream out(&fOut);
    return convert(in, out);
}
