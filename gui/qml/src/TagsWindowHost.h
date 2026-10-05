#pragma once
//
// TagsWindowHost — a QML Címkék-ablak (TagsWindow.qml) C++ gazdája (a PeopleWindowHost mintájára).
//
// Egy folyamatban EGY példány elég: a főalkalmazásban a QmlShellBridge hozza létre
// (ShellActions.openTags → open()). Saját QML-motorral dolgozik, a téma az `App` singletonon át
// közös. Az ablak nem modális; a „Megnyitás a könyvtárban szűrőként” és a megbeszélés-linkek a
// főablakhoz mennek (openInLibraryRequested / meetingRequested — a híd köti a héjhoz).
//
#include <QObject>
#include <QPointer>
#include <QString>

class QQmlEngine;
class QQuickWindow;
class QWindow;

namespace tanara {
class AppController;
}

namespace tanara_qml {

class TagsViewModel;

class TagsWindowHost : public QObject {
    Q_OBJECT
public:
    explicit TagsWindowHost(tanara::AppController* controller, QObject* parent = nullptr);
    ~TagsWindowHost() override;

    // Az ablak megnyitása / előtérbe hozása; tagId: ez a címke legyen kijelölve (üres: a korábbi
    // kijelölés marad). false: QML-hiba.
    bool open(const QString& tagId = QString());
    void closeNow();
    bool isVisible() const;
    QQuickWindow* window() const;
    TagsViewModel* viewModel() const;

    // Az ablak ehhez tartozzon az ablakkezelő szerint.
    void setTransientParent(QWindow* parent);

signals:
    void closed();
    void openInLibraryRequested(const QString& tagId);
    void meetingRequested(const QString& meetingId);

private:
    bool ensureWindow();

    QPointer<tanara::AppController> m_controller;
    QQmlEngine* m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<TagsViewModel> m_vm;
    QPointer<QWindow> m_transientParent;
};

} // namespace tanara_qml
