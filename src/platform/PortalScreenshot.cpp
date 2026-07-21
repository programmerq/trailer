#include "PortalScreenshot.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <atomic>

// Real (Linux/BSD) XDG screenshot-portal backend. Local D-Bus IPC only — see
// the header for why this stays inside Trailer's no-network constraint.

namespace trailer {

namespace {

constexpr char kPortalService[] = "org.freedesktop.portal.Desktop";
constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
constexpr char kScreenshotIface[] = "org.freedesktop.portal.Screenshot";
constexpr char kRequestIface[] = "org.freedesktop.portal.Request";

// How long to wait for the portal's async Request.Response before giving up.
// Interactive backends can sit on a user prompt; a whole-screen non-interactive
// grab returns in well under a second. 30 s is generous headroom for a slow
// compositor without hanging the UI thread indefinitely on a wedged portal.
// NOTE: this bounds EACH of the two blocking phases separately — the initial
// bus.call() ack (QDBus::Block, kResponseTimeoutMs) and then the guarded
// loop.exec() waiting for the async Response — so the worst-case GUI-thread
// block for a maximally-slow-but-not-dead portal is ~2× this constant, not a
// single 30 s. Tried: a single shared deadline across both phases (more code
// for a corner that only matters when a portal half-answers then wedges);
// 30 s per phase is the simpler guard. Symptom to lower it: users report the
// UI hanging up to a minute when a backend is wedged rather than absent.
constexpr int kResponseTimeoutMs = 30000;

// Receives the one org.freedesktop.portal.Request.Response we care about and
// unblocks the nested event loop. response==0 is success; results carries the
// captured image "uri".
class ResponseWaiter : public QObject {
    Q_OBJECT
  public:
    bool responded = false; // set true only when a real Response signal arrives
    uint response = 0;
    QVariantMap results;
    QEventLoop loop;

  public slots:
    void onResponse(uint code, const QVariantMap &r) {
        responded = true;
        response = code;
        results = r;
        loop.quit();
    }
};

} // namespace

bool isWaylandSession() {
    // "wayland" and any "wayland-egl"/variant all start with "wayland".
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"),
                                                      Qt::CaseInsensitive);
}

bool portalScreenshotAvailable() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    // Require the portal frontend to be ALREADY running (name registered), so
    // this probe never auto-starts an activatable service merely because a
    // menu opened. A real Wayland session starts the portal at login; if it
    // isn't up, honest-degrade (disable the control) is the correct, safe read.
    //
    // This is intentionally a name-registration check ONLY — no QDBusInterface
    // construction, no introspection round-trip. portalScreenshotAvailable() is
    // called on every screenshot-submenu open (aboutToShow), so it sits on a
    // hot UI path; a synchronous introspect/property blocking IPC per open was
    // adding avoidable latency. isServiceRegistered() is a cheap local query
    // against the bus daemon's known-names table and keeps the non-activating
    // semantics (it never starts the service). If the frontend is up but a
    // backend fails to export Screenshot, the actual capture call surfaces that
    // honestly (Failed with a reason), which is the G3-correct place for it.
    QDBusConnectionInterface *dbus = bus.interface();
    return dbus && dbus->isServiceRegistered(QLatin1String(kPortalService)).value();
}

PortalCaptureResult capturePortalScreenshotToPng(const QString &outPngPath, bool interactive,
                                                 QString *errorOut) {
    auto fail = [&](PortalCaptureResult r, const QString &msg) {
        if (errorOut)
            *errorOut = msg;
        return r;
    };

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return fail(PortalCaptureResult::Unavailable,
                    QStringLiteral("no D-Bus session bus is available"));

    // Compute the Request object path up front and subscribe to its Response
    // BEFORE issuing the call, so a fast portal cannot emit Response between
    // the call returning and us connecting (the race the handle_token in the
    // portal spec exists to close). Path is
    //   /org/freedesktop/portal/desktop/request/<SENDER>/<TOKEN>
    // where SENDER is our unique name (':1.42') with the leading ':' dropped
    // and '.' -> '_'.
    static std::atomic<unsigned> counter{0};
    const QString token = QStringLiteral("trailer_screenshot_%1").arg(counter.fetch_add(1));
    QString sender = bus.baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString requestPath =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    ResponseWaiter waiter;
    const bool connected = bus.connect(
        QLatin1String(kPortalService), requestPath, QLatin1String(kRequestIface),
        QStringLiteral("Response"), &waiter, SLOT(onResponse(uint, QVariantMap)));
    if (!connected)
        return fail(PortalCaptureResult::Failed,
                    QStringLiteral("could not subscribe to the portal Response signal"));

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("interactive"), interactive);

    QDBusMessage call = QDBusMessage::createMethodCall(
        QLatin1String(kPortalService), QLatin1String(kPortalPath),
        QLatin1String(kScreenshotIface), QStringLiteral("Screenshot"));
    call.setArguments({QString(), QVariant::fromValue(options)}); // parent_window="", options

    const QDBusMessage reply = bus.call(call, QDBus::Block, kResponseTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        bus.disconnect(QLatin1String(kPortalService), requestPath, QLatin1String(kRequestIface),
                       QStringLiteral("Response"), &waiter, SLOT(onResponse(uint, QVariantMap)));
        return fail(PortalCaptureResult::Failed,
                    QStringLiteral("portal Screenshot call failed: %1").arg(reply.errorMessage()));
    }

    // Guard the nested loop with a timeout so a portal that never answers can't
    // wedge the GUI thread forever.
    QTimer::singleShot(kResponseTimeoutMs, &waiter.loop, &QEventLoop::quit);
    waiter.loop.exec();

    bus.disconnect(QLatin1String(kPortalService), requestPath, QLatin1String(kRequestIface),
                   QStringLiteral("Response"), &waiter, SLOT(onResponse(uint, QVariantMap)));

    // No Response signal before the timeout: the method call was accepted but
    // the portal never answered (wedged/slow backend). Treat as a failure so
    // the caller surfaces it, NOT as a user cancel (which would be a silent
    // no-op — the exact G3 trap this backend exists to avoid).
    if (!waiter.responded)
        return fail(PortalCaptureResult::Failed,
                    QStringLiteral("portal did not respond within the timeout"));

    // Portal Response codes: 0 = success, 1 = user cancelled, 2 = ended/other.
    if (waiter.response == 1)
        return PortalCaptureResult::Cancelled;
    if (waiter.response != 0)
        return fail(PortalCaptureResult::Failed,
                    QStringLiteral("portal did not return a screenshot (response %1)")
                        .arg(waiter.response));

    const QString uri = waiter.results.value(QStringLiteral("uri")).toString();
    if (uri.isEmpty())
        return fail(PortalCaptureResult::Failed,
                    QStringLiteral("portal succeeded but returned no image URI"));

    const QString localPath = QUrl(uri).toLocalFile();
    if (localPath.isEmpty() || !QFile::exists(localPath))
        return fail(PortalCaptureResult::Failed,
                    QStringLiteral("portal image URI is not a readable local file: %1").arg(uri));

    // Copy the portal's result into the path the caller owns (QFile::copy will
    // not overwrite, so clear any stale transient first).
    QFile::remove(outPngPath);
    if (!QFile::copy(localPath, outPngPath))
        return fail(PortalCaptureResult::Failed,
                    QStringLiteral("could not copy portal screenshot to %1").arg(outPngPath));

    return PortalCaptureResult::Ok;
}

} // namespace trailer

#include "PortalScreenshot.moc"
