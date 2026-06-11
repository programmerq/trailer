#include "UxRecorder.h"

#include "TrailerVersion.h"
#include "settings/AppPaths.h"
#include "uxrecord/UxPlatformCapture.h"
#include "uxrecord/UxQtEventCapture.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPixmap>
#include <QSaveFile>
#include <QScreen>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QWidget>

#include <atomic>

#if !defined(Q_OS_WIN)
#include <csignal>
#include <cerrno>
#endif

namespace trailer {

namespace {

// Drain interval for the event buffer (and the Qt-side mouse/wheel
// coalescers). 2 s bounds crash data-loss to a couple of seconds of
// events while keeping disk writes far apart enough not to matter.
constexpr int kFlushIntervalMs = 2000;

// Whether printable key text is written into key events. This is a
// developer-only recorder for the developer's own sessions, so the
// MVP records real keystrokes; flip to false to keep only key
// identity/modifiers (both Qt-side and platform-side honour it).
constexpr bool kCaptureKeyText = true;

// Bundle id of the sanctioned external fallback app whose foreground
// time stays in the recording.
const QLatin1String kPreviewBundleId("com.apple.Preview");

// The active recorder behind the uxrecord:: facade and the installed
// message handler. Set after start() succeeds, cleared on stop().
std::atomic<UxRecorder *> g_activeRecorder{nullptr};
QtMessageHandler g_previousMessageHandler = nullptr;

// Reentrancy guard: if logging from inside the handler ever warns,
// don't loop back into ourselves.
thread_local bool t_inMessageHandler = false;

void uxMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (!t_inMessageHandler) {
        t_inMessageHandler = true;
        if (UxRecorder *recorder = g_activeRecorder.load()) {
            recorder->recordEvent(
                QStringLiteral("log"), QStringLiteral("log_message"),
                QJsonObject{{QStringLiteral("level"), static_cast<int>(type)},
                            {QStringLiteral("category"),
                             context.category ? QString::fromLatin1(context.category) : QString()},
                            {QStringLiteral("message"), message}});
        }
        t_inMessageHandler = false;
    }
    if (g_previousMessageHandler) {
        g_previousMessageHandler(type, context, message);
    }
}

bool processAlive(qint64 pid) {
#if defined(Q_OS_WIN)
    // No cheap liveness probe wired up on Windows; report "alive" so
    // the sweep never mislabels a session from a concurrent instance.
    Q_UNUSED(pid);
    return true;
#else
    if (pid <= 0) {
        return false;
    }
    // Signal 0 probes existence without delivering anything. EPERM
    // still means "exists".
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

QString utcNowIso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool writeJsonFile(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

} // namespace

namespace uxrecord {

// Facade backing store (declared in UxRecord.h, only in recorder
// builds). Lives here so the recorder flips it exactly around the
// started/stopped window.
UxRecorder *recorder() {
    return g_activeRecorder.load();
}

} // namespace uxrecord

UxRecorder::UxRecorder(QString baseDirOverride, bool withPlatformCapture, QObject *parent)
    : QObject(parent), m_baseDirOverride(std::move(baseDirOverride)),
      m_withPlatformCapture(withPlatformCapture) {}

UxRecorder::~UxRecorder() {
    stop();
}

bool UxRecorder::start() {
    if (m_recording) {
        return true;
    }

    const QString base =
        m_baseDirOverride.isEmpty() ? AppPaths::uxSessionsDir() : m_baseDirOverride;
    AppPaths::ensureDirExists(base);
    markStaleSessions();

    // Directory name sorts chronologically and stays Finder-safe
    // (no colons): 2026-06-09T21-14-33Z-1a2b3c4d.
    m_sessionId = QUuid::createUuid().toString(QUuid::Id128).left(8).toLower();
    const QString stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH-mm-ss'Z'"));
    m_sessionDir = QDir(base).filePath(stamp + QLatin1Char('-') + m_sessionId);

    QDir dir(m_sessionDir);
    if (!dir.mkpath(QStringLiteral("screen")) || !dir.mkpath(QStringLiteral("camera")) ||
        !dir.mkpath(QStringLiteral("screenshots"))) {
        qWarning() << "UxRecorder: could not create session directory" << m_sessionDir;
        return false;
    }

    if (!m_stream.open(dir.filePath(QStringLiteral("events.jsonl")), m_sessionId)) {
        qWarning() << "UxRecorder: could not open events.jsonl in" << m_sessionDir;
        return false;
    }

    m_startedUtc = utcNowIso();
    m_recording = true;

    writeMetadata();
    // Status "recording" until a clean stop; a session that still says
    // so with a dead pid is marked "crashed" by the next start().
    writeManifest(QStringLiteral("recording"));

    // trailer.log tee. Install once per session; the previous handler
    // (Qt's default stderr writer) stays chained so console output is
    // unchanged.
    m_logFile.setFileName(dir.filePath(QStringLiteral("trailer.log")));
    m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    g_activeRecorder.store(this);
    g_previousMessageHandler = qInstallMessageHandler(uxMessageHandler);

    recordEvent(QStringLiteral("session"), QStringLiteral("session_started"),
                QJsonObject{{QStringLiteral("started_utc"), m_startedUtc},
                            {QStringLiteral("session_dir"), m_sessionDir}});

    // Application-wide Qt observer (mouse/key/focus/dialog/menu…).
    m_qtCapture = new UxQtEventCapture(
        [this](const QString &type, const QJsonObject &data) {
            recordEvent(QStringLiteral("qt"), type, data);
        },
        this);
    m_qtCapture->setCaptureKeyText(kCaptureKeyText);
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->installEventFilter(m_qtCapture);
    }

    // Cross-platform application-active signal; complements the
    // macOS-side frontmost tracking with Trailer's own view.
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(gui, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    recordEvent(
                        QStringLiteral("qt"), QStringLiteral("application_state"),
                        QJsonObject{{QStringLiteral("state"), state == Qt::ApplicationActive
                                                                  ? QStringLiteral("active")
                                                                  : QStringLiteral("inactive")}});
                });
    }

    if (m_withPlatformCapture) {
        UxCaptureContext context;
        context.sessionDir = m_sessionDir;
        context.previewBundleId = kPreviewBundleId;
        context.captureKeyText = kCaptureKeyText;
        context.elapsedMs = [this]() { return m_stream.elapsedMs(); };
        context.emitEvent = [this](const QString &type, const QJsonObject &data) {
            recordEvent(QStringLiteral("macos"), type, data);
            // Surface capture problems in the UI without the platform
            // layer knowing anything about widgets. Two routes:
            //   - a "user_message" field lets the backend supply the
            //     exact wording for important cases (e.g. the
            //     "approve, then relaunch" Screen Recording guidance);
            //   - otherwise any denied / unavailable / failed event
            //     gets a generic one-liner.
            // Queued because the backend may call from its own threads.
            QString message;
            if (data.contains(QLatin1String("user_message"))) {
                message = data.value(QLatin1String("user_message")).toString();
            } else if (type.contains(QLatin1String("denied")) ||
                       type.contains(QLatin1String("unavailable")) ||
                       type.contains(QLatin1String("failed"))) {
                message =
                    QStringLiteral("UX recorder: %1 — session continues without it.").arg(type);
            }
            if (!message.isEmpty()) {
                QMetaObject::invokeMethod(
                    this, [this, message]() { emit captureIssue(message); },
                    Qt::QueuedConnection);
            }
        };
        context.frustrationHotkey = [this]() {
            insertMarker(QStringLiteral("frustration"), QStringLiteral("global hotkey"));
        };
        m_platformCapture = createUxPlatformCapture(std::move(context));
        m_platformCapture->start();
    }

    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(kFlushIntervalMs);
    connect(m_flushTimer, &QTimer::timeout, this, &UxRecorder::flushNow);
    m_flushTimer->start();

    // Stop cleanly at shutdown even if the owner forgets.
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                &UxRecorder::stop, Qt::UniqueConnection);
    }
    return true;
}

void UxRecorder::stop() {
    if (!m_recording) {
        return;
    }

    recordEvent(QStringLiteral("session"), QStringLiteral("session_stopped"),
                QJsonObject{{QStringLiteral("stopped_utc"), utcNowIso()},
                            {QStringLiteral("duration_ms"), m_stream.elapsedMs()}});

    if (m_flushTimer) {
        m_flushTimer->stop();
    }

    // Platform capture first: closing the camera file emits its final
    // camera_stopped event, which still lands in the stream below.
    if (m_platformCapture) {
        m_platformCapture->stop();
        m_platformCapture.reset();
    }

    if (m_qtCapture) {
        if (QCoreApplication::instance()) {
            QCoreApplication::instance()->removeEventFilter(m_qtCapture);
        }
        m_qtCapture->flushPending();
        m_qtCapture->deleteLater();
        m_qtCapture = nullptr;
    }

    // Detach the log tee before tearing the stream down.
    if (g_activeRecorder.load() == this) {
        g_activeRecorder.store(nullptr);
        qInstallMessageHandler(g_previousMessageHandler);
        g_previousMessageHandler = nullptr;
    }
    {
        QMutexLocker locker(&m_logMutex);
        if (m_logFile.isOpen()) {
            m_logFile.close();
        }
    }

    m_recording = false;
    m_stream.close();
    writeManifest(QStringLiteral("complete"));
}

void UxRecorder::recordEvent(const QString &source, const QString &type, const QJsonObject &data) {
    if (!m_recording) {
        return;
    }
    m_stream.append(source, type, data);
    if (source == QLatin1String("log")) {
        QMutexLocker locker(&m_logMutex);
        if (m_logFile.isOpen()) {
            const QString line = QStringLiteral("[%1ms] %2\n")
                                     .arg(m_stream.elapsedMs())
                                     .arg(data.value(QStringLiteral("message")).toString());
            m_logFile.write(line.toUtf8());
        }
    }
}

void UxRecorder::recordTrailerEvent(const QString &type, const QJsonObject &data) {
    recordEvent(QStringLiteral("trailer"), type, data);
}

void UxRecorder::insertMarker(const QString &kind, const QString &note) {
    if (!m_recording) {
        return;
    }
    if (QThread::currentThread() != thread()) {
        // Global-hotkey path arrives from the platform input thread;
        // the screenshot grab below needs the GUI thread.
        QMetaObject::invokeMethod(
            this, [this, kind, note]() { insertMarker(kind, note); }, Qt::QueuedConnection);
        return;
    }
    QJsonObject data{{QStringLiteral("kind"), kind}};
    if (!note.isEmpty()) {
        data.insert(QStringLiteral("note"), note);
    }
    const quint64 sequence =
        m_stream.append(QStringLiteral("trailer"), QStringLiteral("manual_marker"), data);
    ++m_markerCount;
    saveMarkerScreenshot(sequence, kind);
    // Markers are the artefacts the user explicitly asked to keep —
    // make sure they survive an immediate crash.
    flushNow();
    emit markerInserted(kind);
}

void UxRecorder::saveMarkerScreenshot(quint64 sequence, const QString &kind) {
    QWidget *window = QApplication::activeWindow();
    if (!window) {
        const auto windows = QApplication::topLevelWidgets();
        for (QWidget *w : windows) {
            if (w->isVisible()) {
                window = w;
                break;
            }
        }
    }
    if (!window) {
        return;
    }
    const QString file = QStringLiteral("screenshots/marker-%1-%2.png")
                             .arg(sequence, 6, 10, QLatin1Char('0'))
                             .arg(kind);
    const QPixmap grab = window->grab();
    if (!grab.isNull() && grab.save(QDir(m_sessionDir).filePath(file))) {
        recordTrailerEvent(
            QStringLiteral("marker_screenshot"),
            QJsonObject{{QStringLiteral("file"), file},
                        {QStringLiteral("marker_sequence"), static_cast<qint64>(sequence)}});
    }
}

bool UxRecorder::platformCaptureSupported() const {
    return m_platformCapture && m_platformCapture->isSupported();
}

void UxRecorder::setVisualCapturePaused(bool paused) {
    if (!m_platformCapture) {
        return;
    }
    m_platformCapture->setPaused(paused);
    recordTrailerEvent(paused ? QStringLiteral("visual_capture_paused")
                              : QStringLiteral("visual_capture_resumed"));
}

bool UxRecorder::visualCapturePaused() const {
    return m_platformCapture && m_platformCapture->isPaused();
}

void UxRecorder::flushNow() {
    if (m_qtCapture) {
        m_qtCapture->flushPending();
    }
    m_stream.flush();
    QMutexLocker locker(&m_logMutex);
    if (m_logFile.isOpen()) {
        m_logFile.flush();
    }
}

QJsonObject UxRecorder::captureConfigJson() const {
    return QJsonObject{
        {QStringLiteral("capture_key_text"), kCaptureKeyText},
        {QStringLiteral("flush_interval_ms"), kFlushIntervalMs},
        {QStringLiteral("preview_bundle_id"), QString(kPreviewBundleId)},
        {QStringLiteral("platform_capture"), m_withPlatformCapture},
    };
}

void UxRecorder::writeMetadata() {
    QJsonArray screens;
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        const auto screenList = QGuiApplication::screens();
        for (const QScreen *screen : screenList) {
            const QRect geo = screen->geometry();
            screens.append(QJsonObject{
                {QStringLiteral("name"), screen->name()},
                {QStringLiteral("geometry"),
                 QJsonArray{geo.x(), geo.y(), geo.width(), geo.height()}},
                {QStringLiteral("device_pixel_ratio"), screen->devicePixelRatio()},
            });
        }
    }
    const QJsonObject metadata{
        {QStringLiteral("schema_version"), kUxSchemaVersion},
        {QStringLiteral("session_id"), m_sessionId},
        {QStringLiteral("app"), QStringLiteral("Trailer")},
        {QStringLiteral("app_version"), QStringLiteral(TRAILER_VERSION_STRING)},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("os"), QSysInfo::prettyProductName()},
        {QStringLiteral("kernel"),
         QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion()},
        {QStringLiteral("cpu_arch"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("started_utc"), m_startedUtc},
        {QStringLiteral("command_line"), QJsonArray::fromStringList(QCoreApplication::arguments())},
        {QStringLiteral("screens"), screens},
        {QStringLiteral("config"), captureConfigJson()},
    };
    writeJsonFile(QDir(m_sessionDir).filePath(QStringLiteral("metadata.json")), metadata);
}

void UxRecorder::writeManifest(const QString &status) {
    QJsonObject manifest{
        {QStringLiteral("schema_version"), kUxSchemaVersion},
        {QStringLiteral("session_id"), m_sessionId},
        {QStringLiteral("status"), status},
        {QStringLiteral("started_utc"), m_startedUtc},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("files"),
         QJsonObject{{QStringLiteral("events"), QStringLiteral("events.jsonl")},
                     {QStringLiteral("metadata"), QStringLiteral("metadata.json")},
                     {QStringLiteral("log"), QStringLiteral("trailer.log")}}},
        {QStringLiteral("dirs"), QJsonArray{QStringLiteral("screen"), QStringLiteral("camera"),
                                            QStringLiteral("screenshots")}},
    };
    if (status == QLatin1String("complete")) {
        manifest.insert(QStringLiteral("stopped_utc"), utcNowIso());
        manifest.insert(QStringLiteral("event_count"), static_cast<qint64>(m_stream.eventCount()));
        manifest.insert(QStringLiteral("marker_count"), static_cast<qint64>(m_markerCount));
    }
    writeJsonFile(QDir(m_sessionDir).filePath(QStringLiteral("manifest.json")), manifest);
}

void UxRecorder::markStaleSessions() {
    const QString base =
        m_baseDirOverride.isEmpty() ? AppPaths::uxSessionsDir() : m_baseDirOverride;
    const QDir baseDir(base);
    const QStringList entries = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &entry : entries) {
        const QString manifestPath = baseDir.filePath(entry + QStringLiteral("/manifest.json"));
        QFile file(manifestPath);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        if (manifest.value(QStringLiteral("status")).toString() != QLatin1String("recording")) {
            continue;
        }
        const qint64 pid = manifest.value(QStringLiteral("pid")).toVariant().toLongLong();
        if (processAlive(pid)) {
            continue;
        }
        manifest.insert(QStringLiteral("status"), QStringLiteral("crashed"));
        manifest.insert(QStringLiteral("crash_detected_utc"), utcNowIso());
        writeJsonFile(manifestPath, manifest);
    }
}

} // namespace trailer
