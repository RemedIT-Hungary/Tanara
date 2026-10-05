#include <QtGlobal>
#if defined(Q_OS_WIN)

#include "tanara/detect/WindowsCaptureDetector.h"
#include "tanara/detect/detail/WinCaptureRules.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <QHash>
#include <QVector>

#include <iterator>
#include <string>

namespace tanara {

namespace {

namespace win = detail::win;

// A GUID-okat magunk definiáljuk (nem a uuid-libre / __uuidof-ra bízzuk) — így MinGW és MSVC
// alatt is ugyanúgy fordul, ütközés nélkül.
const CLSID kClsidMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const IID   kIidIMMDeviceEnumerator  = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const IID   kIidIAudioSessionManager2 = {0x77AA99A0, 0x1BD6, 0x484F, {0x8B, 0xC7, 0x2C, 0x65, 0x4C, 0x9A, 0x9B, 0x6F}};
const IID   kIidIAudioSessionControl2 = {0xBFB7FF88, 0x7239, 0x4FC9, {0x8F, 0xA2, 0x07, 0xC9, 0x50, 0xBE, 0x9C, 0x6D}};

const wchar_t kConsentMicKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\microphone";

// Minimális COM-okos mutató (a WRL MinGW-n nem mindig elérhető).
template <typename T>
class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    T* get() const { return p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
    T** out() { reset(); return &p; }
    void** outVoid() { return reinterpret_cast<void**>(out()); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
private:
    T* p = nullptr;
};

// Processz-pillanatkép (PID → szülő PID + képnév) — csak ha kell (WebView2-szülő, NonPackaged
// futás-ellenőrzés, ill. ha az OpenProcess nem sikerül). Pollonként legfeljebb egyszer készül.
struct ProcessTable {
    bool built = false;
    QHash<DWORD, QPair<DWORD, QString>> byPid;   // pid → (ppid, kisbetűs alapnév)

    void build()
    {
        if (built) return;
        built = true;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
            byPid.insert(pe.th32ProcessID,
                         {pe.th32ParentProcessID, win::imageBaseName(QString::fromWCharArray(pe.szExeFile))});
        CloseHandle(snap);
    }
    bool running(const QString& base)
    {
        build();
        for (auto it = byPid.cbegin(); it != byPid.cend(); ++it)
            if (it.value().second == base) return true;
        return false;
    }
};

QString imageOf(DWORD pid, ProcessTable& procs)
{
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH * 2];
        DWORD len = DWORD(std::size(buf));
        const BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &len);
        CloseHandle(h);
        if (ok)
            return win::imageBaseName(QString::fromWCharArray(buf, int(len)));
    }
    procs.build();   // pl. emelt jogú processz → a pillanatkép neve
    return procs.byPid.value(pid).second;
}

// A PID látható, legfelső szintű ablakai közül a leghosszabb című (best-effort).
struct TitleSearch { DWORD pid; QString best; };

BOOL CALLBACK collectTitle(HWND hwnd, LPARAM lp)
{
    auto* s = reinterpret_cast<TitleSearch*>(lp);
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner != s->pid || !IsWindowVisible(hwnd))
        return TRUE;
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= s->best.size())
        return TRUE;
    std::wstring buf(size_t(len) + 1, L'\0');
    const int got = GetWindowTextW(hwnd, buf.data(), len + 1);
    if (got > s->best.size())
        s->best = QString::fromWCharArray(buf.data(), got);
    return TRUE;
}

QString windowTitleOf(DWORD pid)
{
    TitleSearch s{pid, {}};
    EnumWindows(collectTitle, reinterpret_cast<LPARAM>(&s));
    return s.best;
}

bool readQword(HKEY key, const wchar_t* name, quint64& out)
{
    DWORD type = 0;
    DWORD size = sizeof(out);
    out = 0;
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&out), &size) == ERROR_SUCCESS
           && type == REG_QWORD;
}

QStringList subkeys(HKEY key)
{
    QStringList names;
    wchar_t name[512];
    for (DWORD i = 0;; ++i) {
        DWORD len = DWORD(std::size(name));
        const LONG rc = RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc == ERROR_SUCCESS) names << QString::fromWCharArray(name, int(len));
    }
    return names;
}

bool keyInUse(HKEY parent, const QString& sub)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(parent, reinterpret_cast<const wchar_t*>(sub.utf16()), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
        return false;
    quint64 start = 0, stop = 0;
    const bool hasStart = readQword(k, L"LastUsedTimeStart", start);
    readQword(k, L"LastUsedTimeStop", stop);
    RegCloseKey(k);
    return hasStart && win::consentInUse(start, stop);
}

} // namespace

struct WindowsCaptureDetector::Impl {
    bool comOwned = false;                      // mi inicializáltuk a COM-ot ezen a szálon → mi is zárjuk
    mutable Com<IMMDeviceEnumerator> enumerator;  // egyszer létrehozva, hibánál újra

    Impl()
    {
        // MTA-t kérünk; ha a szál már STA (pl. a GUI fő szála: a Qt OleInitialize-t hív), az
        // RPC_E_CHANGED_MODE nem hiba — a meglévő apartmentben dolgozunk, és nem zárjuk le.
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comOwned = SUCCEEDED(hr);   // S_OK és S_FALSE is egy CoUninitialize-t kíván
    }
    ~Impl()
    {
        enumerator.reset();
        if (comOwned) CoUninitialize();
    }

    bool ensureEnumerator() const
    {
        if (enumerator) return true;
        return SUCCEEDED(CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL,
                                          kIidIMMDeviceEnumerator, enumerator.outVoid()));
    }
};

WindowsCaptureDetector::WindowsCaptureDetector()
    : d(std::make_unique<Impl>())
{
    // Beépített default (a settings knownCallApps defaultjával egyező). A figyelő felülírja.
    m_knownApps = { QStringLiteral("zoom"), QStringLiteral("teams"), QStringLiteral("webex"),
                    QStringLiteral("slack"), QStringLiteral("discord"), QStringLiteral("meet"),
                    QStringLiteral("skype"), QStringLiteral("chromium"), QStringLiteral("firefox") };
}

WindowsCaptureDetector::~WindowsCaptureDetector() = default;

QString WindowsCaptureDetector::id() const
{
    return QStringLiteral("windows-wasapi");
}

bool WindowsCaptureDetector::isAvailable() const
{
    return d->ensureEnumerator();
}

void WindowsCaptureDetector::configure(const QStringList& knownCallApps, const QString& selfBinary)
{
    if (!knownCallApps.isEmpty())
        m_knownApps = knownCallApps;
    if (!selfBinary.trimmed().isEmpty())
        m_selfBinary = selfBinary.trimmed();
}

MeetingSignal WindowsCaptureDetector::poll()
{
    MeetingSignal sig;
    ProcessTable procs;
    const DWORD selfPid = GetCurrentProcessId();

    // ---- 1) WASAPI capture-session-ök -----------------------------------------------------
    if (d->ensureEnumerator()) {
        Com<IMMDeviceCollection> devices;
        HRESULT hr = d->enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, devices.out());
        if (FAILED(hr)) {
            // Eszköz-/szolgáltatás-változás után a gyorsítótárazott enumerátor elavulhat →
            // eldobjuk, és egyszer újrapróbáljuk.
            d->enumerator.reset();
            if (d->ensureEnumerator())
                hr = d->enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, devices.out());
        }
        UINT devCount = 0;
        if (SUCCEEDED(hr) && devices)
            devices->GetCount(&devCount);

        for (UINT di = 0; di < devCount; ++di) {
            Com<IMMDevice> dev;
            if (FAILED(devices->Item(di, dev.out())))
                continue;
            // A session-managert pollonként frissen kérjük: a gyorsítótárazott példány
            // enumerátora a később létrejött session-öket nem mindig látja.
            Com<IAudioSessionManager2> mgr;
            if (FAILED(dev->Activate(kIidIAudioSessionManager2, CLSCTX_ALL, nullptr, mgr.outVoid())))
                continue;
            Com<IAudioSessionEnumerator> sessions;
            if (FAILED(mgr->GetSessionEnumerator(sessions.out())))
                continue;
            int n = 0;
            sessions->GetCount(&n);
            for (int si = 0; si < n; ++si) {
                Com<IAudioSessionControl> ctl;
                if (FAILED(sessions->GetSession(si, ctl.out())))
                    continue;
                AudioSessionState state = AudioSessionStateInactive;
                if (FAILED(ctl->GetState(&state)) || state != AudioSessionStateActive)
                    continue;
                Com<IAudioSessionControl2> ctl2;
                if (FAILED(ctl->QueryInterface(kIidIAudioSessionControl2, ctl2.outVoid())))
                    continue;
                if (ctl2->IsSystemSoundsSession() == S_OK)
                    continue;
                DWORD pid = 0;
                if (FAILED(ctl2->GetProcessId(&pid)) || pid == 0 || pid == selfPid)
                    continue;

                QString base = imageOf(pid, procs);
                // WebView2-ben futó app (pl. az új Teams): a mikrofont a msedgewebview2
                // gyerekprocessz fogja → a szülő-láncon az első nem-WebView2 processz számít.
                if (base == QLatin1String("msedgewebview2")) {
                    procs.build();
                    DWORD cur = pid;
                    for (int depth = 0; depth < 6; ++depth) {
                        const DWORD parent = procs.byPid.value(cur).first;
                        if (parent == 0 || !procs.byPid.contains(parent)) break;
                        cur = parent;
                        const QString pb = procs.byPid.value(cur).second;
                        if (pb != QLatin1String("msedgewebview2")) { pid = cur; base = pb; break; }
                    }
                }
                if (base.isEmpty() || pid == selfPid || win::isSelfImage(base, m_selfBinary))
                    continue;   // ön-kizárás: a saját felvevőnk sosem meeting
                const win::CallAppMatch m = win::matchCallApp(base, m_knownApps);
                if (!m.matched)
                    continue;

                sig.active      = true;
                sig.appId       = m.appId;
                sig.appName     = m.appName;
                sig.windowTitle = windowTitleOf(pid);
                sig.sourceRef   = QStringLiteral("wasapi-session:%1:%2").arg(pid).arg(base);
                return sig;   // az első aktív találat elég
            }
        }
    }

    // ---- 2) Tartalék: mikrofon-hozzájárulás (ConsentStore) -------------------------------
    HKEY mic = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kConsentMicKey, 0, KEY_READ, &mic) != ERROR_SUCCESS)
        return sig;
    struct Cand { QString key; QString name; bool nonPackaged; };
    QVector<Cand> cands;
    for (const QString& sub : subkeys(mic)) {
        if (sub.compare(QLatin1String("NonPackaged"), Qt::CaseInsensitive) == 0) {
            HKEY np = nullptr;
            if (RegOpenKeyExW(mic, L"NonPackaged", 0, KEY_READ, &np) == ERROR_SUCCESS) {
                for (const QString& p : subkeys(np))
                    if (keyInUse(np, p))
                        cands.push_back({p, win::consentKeyToName(p, true), true});
                RegCloseKey(np);
            }
        } else if (keyInUse(mic, sub)) {
            cands.push_back({sub, win::consentKeyToName(sub, false), false});
        }
    }
    RegCloseKey(mic);

    for (const Cand& c : std::as_const(cands)) {
        if (c.name.isEmpty() || win::isSelfImage(c.name, m_selfBinary))
            continue;
        const win::CallAppMatch m = win::matchCallApp(c.name, m_knownApps);
        if (!m.matched)
            continue;
        // NonPackaged: csak ha a processz tényleg fut (egy összeomlott app Stop-időbélyeg
        // nélkül maradhat → különben örökre „hívásban" lenne).
        if (c.nonPackaged && !procs.running(c.name))
            continue;
        sig.active    = true;
        sig.appId     = m.appId;
        sig.appName   = m.appName;
        sig.sourceRef = QStringLiteral("consent:%1").arg(c.key);
        if (c.nonPackaged) {
            for (auto it = procs.byPid.cbegin(); it != procs.byPid.cend(); ++it)
                if (it.value().second == c.name) {
                    sig.windowTitle = windowTitleOf(it.key());
                    if (!sig.windowTitle.isEmpty()) break;
                }
        }
        return sig;
    }
    return sig;
}

} // namespace tanara

#endif // Q_OS_WIN
