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
#include <QSet>
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

typedef QTextStream QTS;
typedef QXmlStreamWriter QXml;

QTextStream err(stderr);
QQueue<QString> queue;
QStack<QString> stack;
QStack<QPoint> topLeft;
QMap<QString, int> objectNameCounter;
QSet<QString> objectNames;

class Stacker {
    Q_DISABLE_COPY(Stacker)
public:
    Stacker(const QString & item) { stack.push(item); }
    ~Stacker() { stack.pop(); }
};

class TopLeft {
    Q_DISABLE_COPY(TopLeft)
public:
    TopLeft(const QPoint & point) { topLeft.push(point); }
    ~TopLeft() { topLeft.pop(); }
};

/// Find a unique name for an object of given class
QString objectName(QString const & class_, QString name = QString::Null())
{
    QString stem = name;
    if (stem.isEmpty()) {
        stem = class_.startsWith('Q') ? class_.mid(1) : class_;
        stem[0] = stem[0].toLower();
        name = stem;
    }
    while (objectNames.contains(name)) {
        if (! objectNameCounter.contains(stem)) {
            objectNameCounter[stem] = 2;
        }
        name = QString("%1_%2").arg(stem).arg(objectNameCounter[stem]++);
    }
    objectNames.insert(name);
    return name;
}

QString elide(const QString & str, int len = 30)
{
    return (str.length() <= len) ? str : str.left(len) + "...";
}

QString elide(const QVariant & var, int len = 30)
{
    return elide(var.toString(), len);
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

void writeAttrProperty(QXml & ui, const QString & name, const QString & elem, const QVariantMap & attrs, const QString & attrName_ = QString::Null())
{
    QString const attrName = attrName_.isEmpty() ? name : attrName_;
    if (!attrs.contains(attrName)) return;
    writeProperty(ui, name, elem, attrs[attrName].toString());
}

void writeOrientation(QXml & ui, Qt::Orientation ori)
{
    writeProperty(ui, "orientation", "enum", ori == Qt::Vertical ? "Qt::Vertical" : "Qt::Horizontal");
}

void writeStartWidget(QXml & ui, const QString & class_, const QVariantMap & attrs)
{
    ui.writeStartElement("widget");
    ui.writeAttribute("class", class_);
    QString name;
    if (attrs.contains("q_name")) name = attrs["q_name"].toString();
    name = objectName(class_, name);
    ui.writeAttribute("name", name);
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
    ui.writeEndElement();
}

/// Generate a label for an item that could have an optional label
void genLabel(QXml & ui, QVariantMap & attrs)
{
    enum {
        Center = 0,
        Top = 1,
        Bottom = 2,
        Left = 4,
        Right = 8,
        Inside = 16,
        KnownMask = 0x1F,
        LeftTop = 7,
        RightTop = 0xB,
        LeftBottom = 0xD,
        RightBottom = 0xE
    };
    /* Outside alignments are as follows:
     * Top Left,Right: Top|Left...
     * Left Top,Bottom: LeftTop, LeftBottom
     * Right Top,Bottom: RightTop, RightBottom
     * Bottom Left,Right: Bottom|Left...
    */
    if (!attrs.contains("label") || !attrs.contains("xywh")) return;
    bool hasAlign = attrs.contains("align");
    int align = hasAlign ? attrs["align"].toInt() : Center|Inside;
    if (align & ~KnownMask) {
        err << "Warning: Ignoring an unimplemented label alignment " << elide(attrs["align"])
            << " in label for element " << elide(attrs["q_name"]) << endl;
    }
    align &= KnownMask;
    bool outside = ! (align & Inside);
    QRect r = attrs["xywh"].toRect();
    QStringList alignList;
    if (align == LeftTop) {
        alignList << "Qt::AlignRight" << "Qt::AlignTop";
        r.moveTopRight(r.topLeft());
    }
    else if (align == RightTop) {
        alignList << "Qt::AlignLeft" << "Qt::AlignTop";
        r.moveTopLeft(r.topRight());
    }
    else if (align == LeftBottom) {
        alignList << "Qt::AlignRight" << "Qt::AlignBottom";
        r.moveTopRight(r.topLeft());
    }
    else if (align == RightBottom) {
        alignList << "Qt::AlignLeft" << "Qt::AlignBottom";
        r.moveTopLeft(r.topRight());
    }
    else {
        if (align & Top || (align & Bottom && align & Inside))
            alignList << "Qt::AlignBottom";
        else if (align & Bottom || (align & Top && align & Inside))
            alignList << "Qt::AlignTop";
        else
            alignList << "Qt::AlignVCenter";
        bool tb = align & (Top|Bottom);
        if ((align & Left && !tb) || (align & Right && (align & Inside || tb)))
            alignList << "Qt::AlignRight";
        else if ((align & Right && !tb) || (align & Left && (align & Inside || tb)))
            alignList << "Qt::AlignLeft";
        else
            alignList << "Qt::AlignHCenter";

        if (outside) {
            if (align & Top)
                r.moveBottomLeft(r.topLeft());
            else if (align & Bottom)
                r.moveTopLeft(r.bottomLeft());
            else if (align & Left)
                r.moveTopRight(r.topLeft());
            else if (align & Right)
                r.moveTopLeft(r.topRight());
        }
    }
    auto lblAttrs = attrs;
    lblAttrs["xywh"] = r;
    lblAttrs.remove("q_name");
    writeStartWidget(ui, "QLabel", lblAttrs);
    writeProperty(ui, "alignment", "set", alignList.join('|'));
    ui.writeEndElement();
    attrs.remove("label");
}

QString stackTopFl()
{
    if (!stack.isEmpty())
        for (auto it = stack.end()-1; ; --it) {
            if (it->startsWith("Fl_")) return *it;
            if (it == stack.begin()) break;
        }
    return QString::Null();
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
        else if (attr == "xywh") {
            QRect r = pXYWH(in);
            attrs.insert(attr, r.translated(-topLeft.top()));
            attrs.insert("q_xywh", r);
        }
        else {
            auto val = word(in);
            if (val == "}") {
                err << "Warning: attribute " << elide(attr) << " ended early." << endl;
                attrs.insert(attr, val);
                break;
            }
            attrs.insert(attr, val);
        }
    }
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
    genLabel(ui, attrs);
}

void pFlGroup(QTS & in, QXml & ui)
{
    bool tabGroup = stackTopFl() == "Fl_Tabs";
    auto attrs = pItem(in);
    TopLeft tl(attrs["q_xywh"].toRect().topLeft());
    if (true || tabGroup) {
        attrs["q_title"] = attrs["label"];
        attrs.remove("label");
        writeStartWidget(ui, "QWidget", attrs);
        brace(in, '{');
        pVisuals(in, ui);
        ui.writeEndElement();
    }
    else {
        err << "Warning: the non-tab group " << elide(attrs["q_name"]);
        if (!attrs["label"].toString().isEmpty())
            err << " labeled " << elide(attrs["label"]);
        err << " under " << stackTopFl() << " is a no-op." << endl;
        brace(in, '{');
        pVisuals(in, ui);
    }
}

void pFlTextDisplay(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    genLabel(ui, attrs);
    writeStartWidget(ui, "QTextBrowser", attrs);
    ui.writeEndElement();
}

void pFlButton(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    writeStartWidget(ui, "QPushButton", attrs);
    if (stack.top() == "Fl_Repeat_Button")
        writeProperty(ui, "autoRepeat", "bool", "true");
    ui.writeEndElement();
}

void pFlTabs(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    genLabel(ui, attrs);
    brace(in, '{');
    writeStartWidget(ui, "QTabWidget", attrs);
    pVisuals(in, ui);
    ui.writeEndElement();
}

void pFlSlider(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    auto type = attrs["type"].toString();
    genLabel(ui, attrs);
    writeStartWidget(ui, "DoubleSlider", attrs);
    if (type.isEmpty() || type == "Vert Knob") {
        /* default orientation */
    }
    else if (type == "Horz Knob") {
        writeOrientation(ui, Qt::Horizontal);
    }
    else {
        err << "Warning: unknown " << stack.top() << " type " << elide(attrs["type"]) << endl;
    }
    ui.writeEndElement();
}

void pFlInput(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    auto type = attrs["type"].toString();
    genLabel(ui, attrs);
    if (type.isEmpty()) {
        writeStartWidget(ui, "QLineEdit", attrs);
        ui.writeEndElement();
    }
    else if (type == "Float") {
        writeStartWidget(ui, "QDoubleSpinBox", attrs);
        writeProperty(ui, "buttonSymbols", "enum", "QAbstractSpinBox::NoButtons");
        writeAttrProperty(ui, "value", "double", attrs);
        writeAttrProperty(ui, "minimum", "double", attrs);
        writeAttrProperty(ui, "maximum", "double", attrs);
        writeAttrProperty(ui, "singleStep", "double", attrs, "step");
        ui.writeEndElement();
    }
    else if (type == "Int") {
        writeStartWidget(ui, "QSpinBox", attrs);
        writeProperty(ui, "buttonSymbols", "enum", "QAbstractSpinBox::NoButtons");
        writeAttrProperty(ui, "value", "int", attrs);
        writeAttrProperty(ui, "minimum", "int", attrs);
        writeAttrProperty(ui, "maximum", "int", attrs);
        writeAttrProperty(ui, "singleStep", "int", attrs, "step");
        ui.writeEndElement();
    }
    else {
        err << "Warning: unknown " << stack.top() << " type " << elide(type) << endl;
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
    genLabel(ui, attrs);
    writeStartWidget(ui, "QComboBox", attrs);
    brace(in, '{');
    pVisuals(in, ui);
    ui.writeEndElement();
}

void pmenuitem(QTS & in, QXml & ui)
{
    bool choice = stackTopFl() == "Fl_Choice";
    auto attrs = pItem(in);
    if (choice) {
        ui.writeStartElement("item");
        writeAttrProperty(ui, "text", "string", attrs, "label");
        ui.writeEndElement();
    }
    else {
        err << "Warning: ignoring the menu item " << elide(attrs["q_name"]);
        if (!attrs["label"].toString().isEmpty())
            err << " labeled " << elide(attrs["label"]);
        err << " under " << stackTopFl() << "." << endl;
    }
}

void pFlOutput(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    genLabel(ui, attrs);
    writeStartWidget(ui, "QLineEdit", attrs);
    writeProperty(ui, "readOnly", "bool", "true");
    ui.writeEndElement();
}

void pFlRoundButton(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    auto hasType = attrs.contains("type");
    auto type = attrs["type"].toString();
    if (type == "Radio") {
        writeStartWidget(ui, "QRadioButton", attrs);
        ui.writeEndElement();
    }
    else if (! hasType) {
        // Toggle Button
        writeStartWidget(ui, "QRadioButton", attrs);
        writeProperty(ui, "checkable", "bool", "true");
        writeProperty(ui, "autoExclusive", "bool", "false");
        ui.writeEndElement();
    }
    else {
        err << "Warning: unknown " << stack.top() << " type " << elide(type) << endl;
    }
}

void pFlBrowser(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    genLabel(ui, attrs);
    auto type = attrs["type"].toString();
    if (type == "Hold" || type == "Multi") {
        writeStartWidget(ui, "QListWidget", attrs);
        if (type == "Multi") {
            writeProperty(ui, "selectionMode", "enum",
                          "QAbstractItemView::MultiSelection");
        }
        ui.writeEndElement();
    }
    else {
        err << "Warning: unknown " << stack.top() << " type " << elide(type) << endl;
    }
}

void pFlTextEditor(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    genLabel(ui, attrs);
    writeStartWidget(ui, "QTextEdit", attrs);
    ui.writeEndElement();
}

void pFlCheckButton(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    writeStartWidget(ui, "QCheckBox", attrs);
    if (attrs["value"].toInt()) {
        writeProperty(ui, "checked", "bool", "true");
    }
    ui.writeEndElement();
}

void pFlValueSlider(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    genLabel(ui, attrs);
    writeStartWidget(ui, "ValueSlider", attrs);
    writeAttrProperty(ui, "value", "double", attrs);
    writeAttrProperty(ui, "minimum", "double", attrs);
    writeAttrProperty(ui, "maximum", "double", attrs);
    writeAttrProperty(ui, "singleStep", "double", attrs, "step");
    if (attrs["type"].toString() == "Horz Knob") {
        writeOrientation(ui, Qt::Horizontal);
    }
    else {
        err << "Warning: unknown " << stack.top() << " type " << elide(attrs["type"]) << endl;
    }
    ui.writeEndElement();
}

void pFlCounter(QTS & in, QXml & ui)
{
    auto attrs = pItem(in);
    genLabel(ui, attrs);
    writeStartWidget(ui, "QSpinBox", attrs);
    writeAttrProperty(ui, "value", "double", attrs);
    writeAttrProperty(ui, "minimum", "double", attrs);
    writeAttrProperty(ui, "maximum", "double", attrs);
    writeAttrProperty(ui, "singleStep", "double", attrs, "step");
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
        else if (vis == "Fl_Button" || vis == "Fl_Repeat_Button") pFlButton(in, ui);
        else if (vis == "Fl_Tabs") pFlTabs(in, ui);
        else if (vis == "Fl_Slider") pFlSlider(in, ui);
        else if (vis == "Fl_Input") pFlInput(in, ui);
        else if (vis == "Fl_Light_Button") pFlLightButton(in, ui);
        else if (vis == "Fl_Choice") pFlChoice(in, ui);
        else if (vis == "Fl_Output") pFlOutput(in, ui);
        else if (vis == "Fl_Round_Button") pFlRoundButton(in, ui);
        else if (vis == "Fl_Browser") pFlBrowser(in, ui);
        else if (vis == "Fl_Text_Editor") pFlTextEditor(in, ui);
        else if (vis == "Fl_Check_Button") pFlCheckButton(in, ui);
        else if (vis == "Fl_Value_Slider") pFlValueSlider(in, ui);
        else if (vis == "Fl_Counter") pFlCounter(in, ui);
        else if (vis == "menuitem" || vis == "MenuItem") pmenuitem(in, ui);
        else {
            auto name = word(in);
            auto contents = word(in);
            err << "Warning: unknown visual element " << elide(vis) << " named "  << elide(name) << endl;
        }
    }
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
    topLeft << QPoint(0,0);
    QString w;
    while (!(w = readWordDiag(in)).isNull()) {
        if (w == "class") {
            auto name = word(in);
            brace(in, '{');
            pAttributes(in);
            ui.writeTextElement("class", name);
            ui.writeStartElement("widget");
            ui.writeAttribute("class", "QDialog");
            ui.writeAttribute("name", objectName("QDialog", name));
            brace(in, '{');
            pFunction(in, ui);
            ui.writeEndElement();
        }
        else
            word(in);
    }
    ui.writeStartElement("customwidgets");
    writeCustomWidget(ui, "DoubleSlider", "QSlider", "DoubleSlider.h");
    writeCustomWidget(ui, "ValueSlider", "QSlider", "ValueSlider.h");
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
    out.flush();
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
    err << "Processing " << fInPath << endl;
    QTextStream in(&fIn);
    QTextStream out(&fOut);
    int rc = convert(in, out);
    if (!fOut.commit()) {
        err << "Cannot finish output file" << fOutPath << endl;
        return 3;
    }
    return rc;
}
