#pragma once
// A QTranslator straight from the shipped i18n/tanara_en.ts (no lrelease step in the test): only the
// finished translations; plural messages map to their first (singular) and last (plural) form by %n.
#include <QFile>
#include <QHash>
#include <QStringList>
#include <QTranslator>
#include <QXmlStreamReader>

namespace tstest {

class TsTranslator : public QTranslator {
public:
    bool loadTs(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QXmlStreamReader x(&f);
        QString ctx, src;
        QStringList forms;
        QString text;
        bool inTranslation = false, unfinished = false;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement()) {
                if (x.name() == u"name") ctx = x.readElementText(QXmlStreamReader::IncludeChildElements);
                else if (x.name() == u"source") src = x.readElementText(QXmlStreamReader::IncludeChildElements);
                else if (x.name() == u"translation") {
                    unfinished = x.attributes().value(u"type") == u"unfinished";
                    inTranslation = true;
                    forms.clear();
                    text.clear();
                } else if (inTranslation && x.name() == u"numerusform") {
                    forms << x.readElementText(QXmlStreamReader::IncludeChildElements);
                }
            } else if (x.isCharacters() && inTranslation) {
                text += x.text();
            } else if (x.isEndElement() && x.name() == u"translation") {
                inTranslation = false;
                if (forms.isEmpty()) forms << text;
                if (!unfinished && !forms.isEmpty() && !forms.first().isEmpty())
                    m_map.insert(ctx + QLatin1Char('\x1f') + src, forms);
            }
        }
        return !x.hasError();
    }
    QString translate(const char* context, const char* source, const char*, int n) const override
    {
        const QStringList forms = m_map.value(QString::fromUtf8(context) + QLatin1Char('\x1f') + QString::fromUtf8(source));
        if (forms.isEmpty()) return {};
        return (n == 1 || forms.size() == 1) ? forms.first() : forms.last();
    }
    bool isEmpty() const override { return false; }

private:
    QHash<QString, QStringList> m_map;
};

} // namespace tstest
