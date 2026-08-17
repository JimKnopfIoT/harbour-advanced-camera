#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <QQuickView>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QSortFilterProxyModel>

#include <sailfishapp.h>
#include "deviceinfo.h"
#include "effectsmodel.h"
#include "exposuremodel.h"
#include "isomodel.h"
#include "resolutionmodel.h"
#include "wbmodel.h"
#include "focusmodel.h"
#include "flashmodel.h"
#include "fsoperations.h"
#include "resourcehandler.h"
#include "storagemodel.h"
#include "exifmodel.h"
#include "metadatamodel.h"
#include "micgain.h"
#include "histogramitem.h"
#include "videojoiner.h"

int main(int argc, char *argv[])
{
    // SailfishApp::main() will display "qml/harbour-advanced-camera-ext.qml", if you need more
    // control over initialization, you can use:
    //
    //   - SailfishApp::application(int, char *[]) to get the QGuiApplication *
    //   - SailfishApp::createView() to get a new QQuickView * instance
    //   - SailfishApp::pathTo(QString) to get a QUrl to a resource file
    //   - SailfishApp::pathToMainQml() to get a QUrl to the main QML file
    //
    // To display the view, call "show()" (will show fullscreen on device).

    QGuiApplication *app = SailfishApp::application(argc, argv);

    // harbour-advanced-camera-ext --join OUT.mp4 SEG1.mp4 SEG2.mp4 ...
    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--join")) {
        VideoJoiner joiner;
        QStringList segs;
        for (int i = 3; i < argc; ++i)
            segs << QString::fromLocal8Bit(argv[i]);
        int rc = 1;
        QObject::connect(&joiner, &VideoJoiner::finished, app,
                         [&](const QString &out, bool ok, const QString &err) {
                             if (ok)
                                 qInfo() << "joined into" << out;
                             else
                                 qWarning() << "join failed:" << err;
                             rc = ok ? 0 : 1;
                             app->quit();
                         });
        joiner.join(segs, QString::fromLocal8Bit(argv[2]));
        app->exec();
        return rc;
    }

    app->setOrganizationDomain("piggz.co.uk");
    app->setOrganizationName("uk.co.piggz"); // needed for Sailjail
    app->setApplicationName("AdvancedCameraExt");

    qmlRegisterType<EffectsModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "EffectsModel");
    qmlRegisterType<ExposureModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "ExposureModel");
    qmlRegisterType<IsoModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "IsoModel");
    qmlRegisterType<ResolutionModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "ResolutionModel");
    qmlRegisterType<WbModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "WhiteBalanceModel");
    qmlRegisterType<FocusModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "FocusModel");
    qmlRegisterType<FlashModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "FlashModel");
    qmlRegisterType<ExifModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "ExifModel");
    qmlRegisterType<MetadataModel>("uk.co.piggz.harbour_advanced_camera", 1, 0, "MetadataModel");
    qmlRegisterType<HistogramItem>("uk.co.piggz.harbour_advanced_camera", 1, 0, "HistogramItem");

    ResolutionModel resolutionModel;
    QSortFilterProxyModel sortedResolutionModel;
    sortedResolutionModel.setSourceModel(&resolutionModel);
    sortedResolutionModel.setSortRole(ResolutionModel::ResolutionMpx);
    sortedResolutionModel.sort(0, Qt::DescendingOrder);

    QQuickView *view = SailfishApp::createView();

    ResourceHandler handler;
    handler.acquire();

    view->rootContext()->setContextProperty("modelResolution", &resolutionModel);
    view->rootContext()->setContextProperty("sortedModelResolution", &sortedResolutionModel);
    StorageModel storageModel;
    view->rootContext()->setContextProperty("modelStorage", &storageModel);
    FSOperations fsOperations;
    view->rootContext()->setContextProperty("fsOperations", &fsOperations);
    MicGain micGain;
    view->rootContext()->setContextProperty("micGain", &micGain);
    VideoJoiner videoJoiner;
    view->rootContext()->setContextProperty("videoJoiner", &videoJoiner);

    view->setSource(SailfishApp::pathTo("qml/harbour-advanced-camera-ext.qml"));

    DeviceInfo deviceInfo;
    view->rootContext()->setContextProperty("CameraManufacturer", deviceInfo.manufacturer());
    view->rootContext()->setContextProperty("CameraPrettyModelName", deviceInfo.prettyModelName());

    QObject::connect(view, &QQuickView::focusObjectChanged, &handler,
                     &ResourceHandler::handleFocusChange);
    QObject::connect(&fsOperations, &FSOperations::rescan, &storageModel,
                     &StorageModel::scan);

    view->show();

    return app->exec();
}
