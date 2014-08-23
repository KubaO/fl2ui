#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QXmlStreamWriter>
#include <QRect>
#include <QQueue>
#include <QStack>
#include <QDebug>
#include <algorithm>
#include <cstdio>
#include "read.h"

#ifdef Q_OS_MAC
// Apple LLVM Workaround
namespace std {
template<class _InputIterator, class _OutputIterator, class _Predicate>
_OutputIterator
copy_if(_InputIterator __first, _InputIterator __last,
        _OutputIterator __result, _Predicate __pred)
{
    for (; __first != __last; ++__first) {
        if (__pred(*__first)) {
            *__result = *__first;
            ++__result;
        }
    }
    return __result;
} }
#endif

QTextStream err(stderr);
QQueue<QString> queue;
QStack<QString> stack;

typedef QTextStream QTS;
typedef QXmlStreamWriter QXml;

class Stacker {
    Q_DISABLE_COPY(Stacker)
public:
    Stacker(const QString & item) { stack.push(item); }
    ~Stacker() { stack.pop(); }
};


QString elide(const QString & str, int len = 12)
{
    return (str.length() <= len) ? str : str.left(len) + "...";
}

bool isOk(QTextStream & in)
{
    return in.status() == QTextStream::Ok;
}

void perr(const QString & msg, int rc = 10)
{
    err << msg << "\nLast words read:\n";
    while (!queue.isEmpty()) {
        auto w = queue.dequeue();
        if (!w.isEmpty())
            err << elide(w) << " ";
        else
            err << "\"\" ";
    }
    err << "\nStack:\n";
    while (!stack.isEmpty()) err << stack.pop() << "\n";
    err.flush();
    exit(rc);
}

QString readWordDiag(QTS & in, bool readBrace = false)
{
    auto rv = readWord(in, readBrace);
    queue.enqueue(rv);
    if (queue.size() > 10) queue.dequeue();
    return rv;
}

QString word(QTS & in, const QString & expect = QString::Null())
{
    QString rv = readWordDiag(in);
    if (rv.isNull())
        perr("premature end of input");
    if (! expect.isNull() && rv != expect)
        perr(QString("expected \"%1\", got \"%2\"").arg(expect).arg(elide(rv)));
    return rv;
}

void brace(QTS & in, QChar c)
{
    QString rv = readWordDiag(in, true);
    if (rv != c) perr(QString("expected \"%1\", got \"%2\"").arg(c).arg(elide(rv)));
}

void upto(QTS & in, QChar c)
{
    while (word(in) != c);
}

void writeGeometry(QXml & ui, const QRect & r)
{
    if (r.isNull()) return;
    ui.writeStartElement("property");
    ui.writeAttribute("name", "geometry");
    ui.writeStartElement("rect");
    ui.setAutoFormatting(false);
    ui.writeTextElement("x", QString::number(r.x()));
    ui.writeTextElement("y", QString::number(r.y()));
    ui.writeTextElement("width", QString::number(r.width()));
    ui.writeTextElement("height", QString::number(r.height()));
    ui.writeEndElement();
    ui.setAutoFormatting(true);
    ui.writeEndElement();
}

void writeText(QXml & ui, const QString & text)
{
    if (text.isEmpty()) return;
    ui.writeStartElement("property");
    ui.writeAttribute("name", "text");
    ui.writeTextElement("string", text);
    ui.writeEndElement();
}

void writeAttribute(QXml & ui, const QString & name, const QString & string)
{
    if (string.isEmpty()) return;
    ui.writeStartElement("attribute");
    ui.writeAttribute("name", name);
    ui.writeTextElement("string", string);
    ui.writeEndElement();
}

void writeProperty(QXml & ui, const QString & name, const QString & elem, const QString & value)
{
    ui.writeStartElement("property");
    ui.writeAttribute("name", name);
    ui.writeTextElement(elem, value);
    ui.writeEndElement();
}

void writeOrientation(QXml & ui, Qt::Orientation ori)
{
    writeProperty(ui, "orientation", "enum", ori == Qt::Vertical ? "Qt::Vertical" : "Qt::Horizontal");
}

void writeStartWidget(QXml & ui, const QString & cl, const QVariantMap & attrs)
{
    ui.writeStartElement("widget");
    ui.writeAttribute("class", cl);
    if (attrs.contains("q_name")) {
        auto name = attrs["q_name"].toString();
        if (! name.isEmpty()) ui.writeAttribute("name", name);
    }
    if (attrs.contains("xywh"))
        writeGeometry(ui, attrs["xywh"].toRect());
    if (attrs.contains("label"))
        writeText(ui, attrs["label"].toString());
    if (attrs.contains("q_title"))
        writeAttribute(ui, "title", attrs["q_title"].toString());
}

void writeCustomWidget(QXml & ui, const QString & cl, const QString & baseClass, const QString & headerFile)
{
    ui.writeStartElement("customwidget");
    ui.writeTextElement("class", cl);
    ui.writeTextElement("extends", baseClass);
    ui.writeTextElement("header", headerFile);
    ui.writeTextElement("container", 0);
    ui.writeEndElement();
}

QRect pXYWH(QTS & in)
{
    Stacker s("pXYWH");
    brace(in, '{');
    QRect r(word(in).toInt(), word(in).toInt(), word(in).toInt(), word(in).toInt());
    brace(in, '}');
    return r;
}

QVariantMap pAttributes(QTS & in) {
    Stacker s("pAttributes");
    QVariantMap attrs;
    forever {
        auto const attr = word(in);
        if (attr == "}") break;
        else if (attr == "open" || attr == "hide"
                 || attr == "resizable" || attr == "visible"
                 || attr == "selected") attrs.insert(attr, true);
        else if (attr == "xywh") attrs.insert(attr, pXYWH(in));
        else {
            auto val = word(in);
            if (val == "}") {
                err << "Warning: attribute " << attr << " ended early." << endl;
                attrs.insert(attr, val);
                break;
            }
            attrs.insert(attr, val);
        }
    }
    queue.enqueue("*endAttr");
    return attrs;
}

QVariantMap pItem(QTS & in)
{
    auto name = word(in);
    brace(in, '{');
    QVariantMap attrs = pAttributes(in);
    attrs["q_name"] = name;
    return attrs;
}

void pVisuals(QTS & in, QXml & ui);

void pFlBox(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    if (attrs.contains("label")) {
        writeStartWidget(ui, "QLabel", attrs);
        ui.writeEndElement();
    }
    queue.enqueue("*endBox");
}

void pFlGroup(QTS & in, QXml & ui)
{
    QStack<QString> stackCopy;
    std::copy_if(stack.begin(), stack.end(), std::back_inserter(stackCopy),
                 [](const QString & entry){ return entry.startsWith("Fl_"); });
    stackCopy.pop();
    bool tabGroup = stackCopy.top() == "Fl_Tabs";
    auto attrs = pItem(in);
    if (tabGroup) {
        attrs["q_title"] = attrs["label"];
        attrs.remove("label");
        writeStartWidget(ui, "QWidget", attrs);
        brace(in, '{');
        pVisuals(in, ui);
        ui.writeEndElement();
    }
    else {
        err << "Warning: the non-tab group " << elide(attrs["q_name"].toString(), 20);
        if (!attrs["label"].toString().isEmpty())
            err << " labeled " << elide(attrs["label"].toString());
        err << " under " << stackCopy.top() << " is a no-op." << endl;
        brace(in, '{');
        pVisuals(in, ui);
    }
}

void pFlTextDisplay(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    writeStartWidget(ui, "QTextBrowser", attrs);
    ui.writeEndElement();
}

void pFlButton(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    writeStartWidget(ui, "QPushButton", attrs);
    ui.writeEndElement();
}

void pFlTabs(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    brace(in, '{');
    writeStartWidget(ui, "QTabWidget", attrs);
    pVisuals(in, ui);
    ui.writeEndElement();
}

void pFlSlider(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    writeStartWidget(ui, "DoubleSlider", attrs);
    if (attrs["type"].toString() == "Horz Knob")
        writeOrientation(ui, Qt::Horizontal);
    ui.writeEndElement();
}

void pFlInput(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    auto type = attrs["type"].toString();
    if (type.isEmpty()) {
        writeStartWidget(ui, "QLineEdit", attrs);
        ui.writeEndElement();
    }
    else if (type == "Float") {
        writeStartWidget(ui, "QDoubleSpinBox", attrs);
        writeProperty(ui, "buttonSymbols", "enum", "QAbstractSpinBox::NoButtons");
        ui.writeEndElement();
    }
    else {
        err << "Warning: unknown " << stack.top() << " type " << type << endl;
    }
}

void pFlLightButton(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    writeStartWidget(ui, "QCheckBox", attrs);
    ui.writeEndElement();
}

void pFlChoice(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    word(in); // choice contents
    writeStartWidget(ui, "QComboBox", attrs);
    ui.writeEndElement();
}

void pFlOutput(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    writeStartWidget(ui, "QLineEdit", attrs);
    writeProperty(ui, "readOnly", "bool", "true");
    ui.writeEndElement();
}

void pFlRoundButton(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    auto type = attrs["type"].toString();
    if (type == "Radio") {
        writeStartWidget(ui, "QRadioButton", attrs);
        ui.writeEndElement();
    }
    else {
        err << "Warning: unknown " << stack.top() << " type " << type << endl;
    }
}

void pFlBrowser(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    auto type = attrs["type"].toString();
    if (type == "Hold") {
        writeStartWidget(ui, "QListWidget", attrs);
        ui.writeEndElement();
    }
    else {
        err << "Warning: unknown " << stack.top() << " type " << type << endl;
    }
}

void pFlTextEditor(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    writeStartWidget(ui, "QTextEdit", attrs);
    ui.writeEndElement();
}

void pVisuals(QTS & in, QXml & ui)
{
    Stacker s("pVisuals");
    forever {
        auto vis = word(in);
        if (vis.startsWith('{')) {
            err << "warning: unexpected group" << endl;
            vis = word(in);
        }
        if (vis == "}") break;
        Stacker s(vis);
        if (vis == "Fl_Box") pFlBox(in, ui);
        else if (vis == "Fl_Group") pFlGroup(in, ui);
        else if (vis == "Fl_Text_Display") pFlTextDisplay(in, ui);
        else if (vis == "Fl_Button") pFlButton(in, ui);
        else if (vis == "Fl_Tabs") pFlTabs(in, ui);
        else if (vis == "Fl_Slider") pFlSlider(in, ui);
        else if (vis == "Fl_Input") pFlInput(in, ui);
        else if (vis == "Fl_Light_Button") pFlLightButton(in, ui);
        else if (vis == "Fl_Choice") pFlChoice(in, ui);
        else if (vis == "Fl_Output") pFlOutput(in, ui);
        else if (vis == "Fl_Round_Button") pFlRoundButton(in, ui);
        else if (vis == "Fl_Browser") pFlBrowser(in, ui);
        else if (vis == "Fl_Text_Editor") pFlTextEditor(in, ui);
        else {
            auto name = word(in);
            auto contents = word(in);
            err << "Warning: unknown visual element " << elide(vis) << " named "  << elide(name) << endl;
        }
    }
    queue.enqueue("*endpVis");
}

void pWindow(QTS & in, QXml & ui)
{
    Stacker s("Fl_Window");
    word(in, "Fl_Window");
    auto attrs = pItem(in);
    if (attrs.contains("label")) {
        ui.writeStartElement("property");
        ui.writeAttribute("name", "windowTitle");
        ui.writeTextElement("string", attrs["label"].toString());
        ui.writeEndElement();
    }
    if (attrs.contains("xywh")) {
        writeGeometry(ui, attrs["xywh"].toRect());
    }
    brace(in, '{');
    pVisuals(in, ui);
}

void pFunction(QTS & in, QXml & ui)
{
    Stacker s("Function");
    word(in, "Function");
    auto name = word(in);
    brace(in, '{');
    pAttributes(in);
    brace(in, '{');
    pWindow(in, ui);
}

void pTop(QTS & in, QXml & ui)
{
    Stacker s("pTop");
    QString w;
    while (!(w = readWordDiag(in)).isNull()) {
        if (w == "class") {
            auto name = word(in);
            brace(in, '{');
            pAttributes(in);
            ui.writeTextElement("class", name);
            ui.writeStartElement("widget");
            ui.writeAttribute("class", "QDialog");
            ui.writeAttribute("name", name);
            brace(in, '{');
            pFunction(in, ui);
            ui.writeEndElement();
        }
        else
            word(in);
    }
    ui.writeStartElement("customwidgets");
    writeCustomWidget(ui, "DoubleSlider", "QWidget", "DoubleSlider.h");
    ui.writeEndElement();
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

    writer.setCodec("UTF-8");
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(1);
    writer.writeStartDocument();
    writer.writeStartElement("ui");
    writer.writeAttribute("version", "4.0");

    pTop(in, writer);

    writer.writeEndDocument();

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
    int rc = convert(in, out);
    out.flush();
    if (!fOut.commit()) {
        err << "Cannot finish output file" << fOutPath << endl;
        return 3;
    }
    return rc;
}
