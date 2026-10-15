#include "tanara/foundation/net/NetworkErrorText.h"

#include <QCoreApplication>
#include <QNetworkReply>

namespace tanara::net {

QString networkErrorText(int code, const QString& rawQtText)
{
    switch (static_cast<QNetworkReply::NetworkError>(code)) {
    case QNetworkReply::ConnectionRefusedError:        return QCoreApplication::translate("NetworkError", "a kapcsolatot visszautasította");
    case QNetworkReply::RemoteHostClosedError:         return QCoreApplication::translate("NetworkError", "a távoli gép lezárta a kapcsolatot");
    case QNetworkReply::HostNotFoundError:             return QCoreApplication::translate("NetworkError", "a gép nem található");
    case QNetworkReply::TimeoutError:                  return QCoreApplication::translate("NetworkError", "időtúllépés");
    case QNetworkReply::OperationCanceledError:        return QCoreApplication::translate("NetworkError", "a művelet megszakadt");
    case QNetworkReply::SslHandshakeFailedError:       return QCoreApplication::translate("NetworkError", "a biztonságos kapcsolat nem jött létre");
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:     return QCoreApplication::translate("NetworkError", "átmeneti hálózati hiba");
    case QNetworkReply::ProxyConnectionRefusedError:   return QCoreApplication::translate("NetworkError", "a proxy visszautasította a kapcsolatot");
    case QNetworkReply::ProxyNotFoundError:            return QCoreApplication::translate("NetworkError", "a proxy nem található");
    case QNetworkReply::ProxyTimeoutError:             return QCoreApplication::translate("NetworkError", "a proxy nem válaszolt időben");
    case QNetworkReply::ProxyAuthenticationRequiredError: return QCoreApplication::translate("NetworkError", "a proxy hitelesítést kér");
    case QNetworkReply::AuthenticationRequiredError:
    case QNetworkReply::ContentAccessDenied:           return QCoreApplication::translate("NetworkError", "hitelesítés szükséges");
    case QNetworkReply::ContentNotFoundError:          return QCoreApplication::translate("NetworkError", "a tartalom nem található");
    case QNetworkReply::ProtocolUnknownError:
    case QNetworkReply::ProtocolInvalidOperationError: return QCoreApplication::translate("NetworkError", "ismeretlen vagy érvénytelen protokoll");
    case QNetworkReply::UnknownNetworkError:           return QCoreApplication::translate("NetworkError", "ismeretlen hálózati hiba");
    default:                                           return rawQtText;
    }
}

} // namespace tanara::net
