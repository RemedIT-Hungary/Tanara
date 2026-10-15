// The transport-error text: a QNetworkReply::NetworkError code renders through tr() in Hungarian (source) and
// English (the shipped i18n/tanara_en.ts); an unmapped code falls back to the raw Qt text; the job-failure detail
// line uses it (and keeps the raw text out of the UI when a code is known).
#include <QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QNetworkReply>
#include <QTranslator>
#include <QXmlStreamReader>

#include "tanara/foundation/error/Error.h"
#include "tanara/foundation/net/HttpClient.h"
#include "tanara/foundation/net/NetworkErrorText.h"

using namespace tanara;

namespace {

class TsTranslator : public QTranslator {
public:
    bool loadTs(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QXmlStreamReader x(&f);
        QString ctx, src;
        while (!x.atEnd()) {
            x.readNext();
            if (!x.isStartElement()) continue;
            if (x.name() == u"name") ctx = x.readElementText(QXmlStreamReader::IncludeChildElements);
            else if (x.name() == u"source") src = x.readElementText(QXmlStreamReader::IncludeChildElements);
            else if (x.name() == u"translation") {
                const bool unfinished = x.attributes().value(u"type") == u"unfinished";
                const QString tr = x.readElementText(QXmlStreamReader::IncludeChildElements);
                if (!unfinished && !tr.isEmpty()) m_map.insert(ctx + QLatin1Char('\x1f') + src, tr);
            }
        }
        return true;
    }
    QString translate(const char* context, const char* source, const char*, int) const override
    {
        return m_map.value(QString::fromUtf8(context) + QLatin1Char('\x1f') + QString::fromUtf8(source));
    }
    bool isEmpty() const override { return false; }
private:
    QHash<QString, QString> m_map;
};

} // namespace

class NetworkErrorTextTest : public QObject {
    Q_OBJECT
private slots:
    void hungarianSource()
    {
        QCOMPARE(net::networkErrorText(QNetworkReply::ConnectionRefusedError, QStringLiteral("raw")),
                 QStringLiteral("a kapcsolatot visszautasította"));
        QCOMPARE(net::networkErrorText(QNetworkReply::TimeoutError), QStringLiteral("időtúllépés"));
    }
    void unmappedFallsBackToRawText()
    {
        QCOMPARE(net::networkErrorText(int(QNetworkReply::InternalServerError), QStringLiteral("raw text")),
                 QStringLiteral("raw text"));
        QCOMPARE(net::networkErrorText(0, QStringLiteral("raw text")), QStringLiteral("raw text"));
        QVERIFY(net::networkErrorText(0).isEmpty());
    }
    void englishFromShippedTs()
    {
        TsTranslator tr;
        QVERIFY(tr.loadTs(QStringLiteral(TANARA_EN_TS)));
        QCoreApplication::installTranslator(&tr);
        using E = QNetworkReply;
        const QList<QPair<int, QString>> table = {
            {E::ConnectionRefusedError, "connection refused"},
            {E::RemoteHostClosedError, "the remote host closed the connection"},
            {E::HostNotFoundError, "host not found"},
            {E::TimeoutError, "timed out"},
            {E::OperationCanceledError, "the operation was cancelled"},
            {E::SslHandshakeFailedError, "the secure connection could not be established"},
            {E::TemporaryNetworkFailureError, "temporary network failure"},
            {E::NetworkSessionFailedError, "temporary network failure"},
            {E::ProxyConnectionRefusedError, "the proxy refused the connection"},
            {E::ProxyNotFoundError, "the proxy was not found"},
            {E::ProxyTimeoutError, "the proxy did not answer in time"},
            {E::ProxyAuthenticationRequiredError, "the proxy requires authentication"},
            {E::AuthenticationRequiredError, "authentication required"},
            {E::ContentAccessDenied, "authentication required"},
            {E::ContentNotFoundError, "content not found"},
            {E::ProtocolUnknownError, "unknown or invalid protocol"},
            {E::ProtocolInvalidOperationError, "unknown or invalid protocol"},
            {E::UnknownNetworkError, "unknown network error"},
        };
        for (const auto& row : table)
            QCOMPARE(net::networkErrorText(row.first, QStringLiteral("raw")), row.second);

        // The step's technical line: no raw (system-locale) text when the code is known.
        net::HttpExchange ex;
        ex.status = 0;
        ex.networkError = QStringLiteral("Kapcsolat visszautasítva");
        ex.networkErrorCode = E::ConnectionRefusedError;
        QCOMPARE(net::failureDetail(ex), QStringLiteral("network · connection refused"));
        QCoreApplication::removeTranslator(&tr);
        QCOMPARE(net::failureDetail(ex), QStringLiteral("hálózat · a kapcsolatot visszautasította"));
        // A code-less exchange keeps the raw text.
        ex.networkErrorCode = 0;
        QCOMPARE(net::failureDetail(ex), QStringLiteral("hálózat · Kapcsolat visszautasítva"));
    }
};

QTEST_GUILESS_MAIN(NetworkErrorTextTest)
#include "test_network_error_text.moc"
