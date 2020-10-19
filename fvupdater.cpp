#include "fvupdater.h"
#include "fvupdatewindow.h"
#include "fvupdatedownloadprogress.h"
#include "fvplatform.h"
#include "fvignoredversions.h"
#include "fvavailableupdate.h"
#include <QApplication>
#include <QtNetwork>
#include <QMessageBox>
#include <QDesktopServices>
#include <QDebug>
#include <QSettings>
#include "quazip.h"
#include "quazipfile.h"

#ifdef Q_WS_MAC
#include "CoreFoundation/CoreFoundation.h"
#endif

#define FV_NEW_VERSION_POLICY_KEY              "FVNewVersionPolicy"

#ifdef FV_DEBUG
    // Unit tests
#    include "fvversioncomparatortest.h"
#endif

extern QSettings* settings;

FvUpdater* FvUpdater::m_Instance = 0;


FvUpdater* FvUpdater::sharedUpdater()
{
    static QMutex mutex;
    if (!m_Instance) {
        mutex.lock();

        if (!m_Instance) {
            m_Instance = new FvUpdater;
        }

        mutex.unlock();
    }

    return m_Instance;
}

void FvUpdater::drop()
{
    static QMutex mutex;
    mutex.lock();
    delete m_Instance;
    m_Instance = 0;
    mutex.unlock();
}

FvUpdater::FvUpdater() : QObject(0)
{
    m_reply = 0;
    m_updaterWindow = 0;
    m_proposedUpdate = 0;
    m_mode = NORMAL;

    m_requiredSslFingerprint = "";
    htAuthUsername = "";
    htAuthPassword = "";
    skipVersionAllowed = true;
    remindLaterAllowed = true;

    connect(&m_qnam, SIGNAL(authenticationRequired(QNetworkReply*, QAuthenticator*)),this, SLOT(authenticationRequired(QNetworkReply*, QAuthenticator*)));

    check_callback = 0;
    check_context = 0;

    // Translation mechanism
    installTranslator();

#ifdef FV_DEBUG
    // Unit tests
    FvVersionComparatorTest* test = new FvVersionComparatorTest();
    test->runAll();
    delete test;
#endif

}

FvUpdater::~FvUpdater()
{
    if (m_proposedUpdate) {
        delete m_proposedUpdate;
        m_proposedUpdate = 0;
    }
    if (m_mode == NORMAL)
        hideUpdaterWindow();
}

void FvUpdater::installTranslator()
{
    QTranslator translator;
    QString locale = QLocale::system().name();
    translator.load(QString("fervor_") + locale);

#if QT_VERSION < 0x050000
    QTextCodec::setCodecForTr(QTextCodec::codecForName("utf8"));
#endif

    qApp->installTranslator(&translator);
}

void FvUpdater::showUpdaterWindowUpdatedWithCurrentUpdateProposal()
{
    qDebug() << "FvUpdater::showUpdaterWindowUpdatedWithCurrentUpdateProposal()";

    if (m_mode == NORMAL) {
        // Destroy window if already exists
        hideUpdaterWindow();

        // Create a new window
        m_updaterWindow = new FvUpdateWindow(NULL, skipVersionAllowed, remindLaterAllowed);
        m_updaterWindow->UpdateWindowWithCurrentProposedUpdate();
        m_updaterWindow->show();
    } else {
        // Get the proposed update and emit the signal
        FvAvailableUpdate* proposedUpdate = FvUpdater::sharedUpdater()->GetProposedUpdate();
        if (proposedUpdate == nullptr) {
            qDebug() << "FvUpdater::showUpdaterWindowUpdatedWithCurrentUpdateProposal(): proposedUpdate is empty!";
            return;
        }

        emit proposedVersionChanged(proposedUpdate->GetEnclosureVersion());
        emit proposedReleaseNotesChanged(proposedUpdate->GetReleaseNotesHtml());
        emit proposedReleaseNotesLinkChanged(proposedUpdate->GetReleaseNotesLink());
        emit updateAvailable();
    }
}

void FvUpdater::hideUpdaterWindow()
{
    if (m_updaterWindow) {
        if (!m_updaterWindow->close()) {
            qWarning() << "Update window didn't close, leaking memory from now on";
        }

        // not deleting because of Qt::WA_DeleteOnClose

        m_updaterWindow = 0;
    }
}

void FvUpdater::updaterWindowWasClosed()
{
    // (Re-)nullify a pointer to a destroyed QWidget or you're going to have a bad time.
    m_updaterWindow = 0;
}


// void FvUpdater::showUpdateConfirmationDialogUpdatedWithCurrentUpdateProposal()
// {
//     if (m_mode == NORMAL) {
//         // Destroy dialog if already exists
//         hideUpdateConfirmationDialog();

//         // Create a new window
//         m_updateConfirmationDialog = new FvUpdateConfirmDialog();
//         m_updateConfirmationDialog->UpdateWindowWithCurrentProposedUpdate();
//         m_updateConfirmationDialog->show();
//     } else {
//         //Get the proposed update and emit the signal
//         FvAvailableUpdate* proposedUpdate = FvUpdater::sharedUpdater()->GetProposedUpdate();
//         if (proposedUpdate == nullptr) {
//             qDebug() << "FvUpdater::showUpdateConfirmationDialogUpdatedWithCurrentUpdateProposal(): proposedUpdate is empty!";
//             return;
//         }

//         emit updateDownloadLinkReady(proposedUpdate->GetEnclosureUrl().toString());
//     }
// }

// void FvUpdater::hideUpdateConfirmationDialog()
// {
//     if (m_updateConfirmationDialog) {
//         if (!m_updateConfirmationDialog->close()) {
//             qWarning() << "Update confirmation dialog didn't close, leaking memory from now on";
//         }

//         // not deleting because of Qt::WA_DeleteOnClose

//         m_updateConfirmationDialog = 0;
//     }
// }

// void FvUpdater::updateConfirmationDialogWasClosed()
// {
//     // (Re-)nullify a pointer to a destroyed QWidget or you're going to have a bad time.
//     m_updateConfirmationDialog = 0;
// }


void FvUpdater::SetFeedURL(QUrl feedURL)
{
    m_feedURL = feedURL;
}

void FvUpdater::SetFeedURL(QString feedURL)
{
    SetFeedURL(QUrl(feedURL));
}

QString FvUpdater::GetFeedURL()
{
    return m_feedURL.toString();
}

// Set /get Dynamic download URL content
void FvUpdater::SetDynamicUrlContent(QString dynamicUrl)
{
    m_dynamicUrl = dynamicUrl;
}

QString FvUpdater::GetDynamicUrlContent()
{
    return m_dynamicUrl;
}

void FvUpdater::SetCheckBeforeUpdate(check_before_update_callback callback, void* context)
{
    check_callback = callback;
    check_context = context;
}

void FvUpdater::SetFervorMode(FERVOR_MODE mode) {
    m_mode = mode;
}

FvAvailableUpdate* FvUpdater::GetProposedUpdate()
{
    return m_proposedUpdate;
}

void FvUpdater::InstallUpdate()
{
    qDebug() << "Install update";

    if (m_proposedUpdate == NULL)
    {
        qWarning() << "Abort Update: No update proposed! This should not happen.";
        return;
    }

    // Check callback function
    if (check_callback) {
        if (!check_callback(check_context, (void*)m_updaterWindow)) {
            if (m_mode == NORMAL) {
                hideUpdaterWindow();
                // hideUpdateConfirmationDialog(); // if any; shouldn't be shown at this point, but who knows
            }
            return;
        }
    }

    // Prepare download
    QUrl url = m_proposedUpdate->GetEnclosureUrl();

    // Check SSL Fingerprint if required
    if(url.scheme()=="https" && !m_requiredSslFingerprint.isEmpty())
        if( !checkSslFingerPrint(url) ) // check failed
        {
            qWarning() << "Update aborted.";
            return;
        }
    // Start Download
    QNetworkReply* reply = m_qnam.get(QNetworkRequest(url));
    connect(reply, SIGNAL(finished()), this, SLOT(httpUpdateDownloadFinished()));

    // Maybe Check request 's return value
    if (reply->error() != QNetworkReply::NoError)
    {
        qDebug()<<"Unable to download the update: "<<reply->errorString();
        return;
    }
    else
        qDebug()<<"OK";

    // Show download Window
    if (m_mode == NORMAL) {
        FvUpdateDownloadProgress* dlwindow = new FvUpdateDownloadProgress(NULL);
        connect(reply, SIGNAL(downloadProgress(qint64, qint64)), dlwindow, SLOT(downloadProgress(qint64, qint64) ));
        connect(&m_qnam, SIGNAL(finished(QNetworkReply*)), dlwindow, SLOT(close()));
        dlwindow->show();
    }

    emit updatedFinishedSuccessfully();

    if (m_mode == NORMAL) {
        hideUpdaterWindow();
    }
    // showUpdateConfirmationDialogUpdatedWithCurrentUpdateProposal();
}

void FvUpdater::httpUpdateDownloadFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if(reply==NULL)
    {
        qWarning()<<"The slot httpUpdateDownloadFinished() should only be invoked by S&S.";
        return;
    }

    if(reply->error() == QNetworkReply::NoError)
    {
        int httpstatuscode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toUInt();

        // no error received?
        if (reply->error() == QNetworkReply::NoError)
        {
            if (reply->isReadable())
            {
#ifdef Q_WS_MAC
                CFURLRef appURLRef = CFBundleCopyBundleURL(CFBundleGetMainBundle());
                char path[PATH_MAX];
                if (!CFURLGetFileSystemRepresentation(appURLRef, TRUE, (UInt8 *)path, PATH_MAX)) {
                    // error!
                }

                CFRelease(appURLRef);
                QString filePath = QString(path);
                QString rootDirectory = filePath.left(filePath.lastIndexOf("/"));
#else
                QString rootDirectory = QCoreApplication::applicationDirPath() + "/";
#endif

                // Write download into File
                QFileInfo fileInfo=reply->url().path();
                QString fileName = rootDirectory + fileInfo.fileName();
                //qDebug()<<"Writing downloaded file into "<<fileName;

                QFile file(fileName);
                file.open(QIODevice::WriteOnly);
                file.write(reply->readAll());
                file.close();

                // Retrieve List of updated files (Placed in an extra scope to avoid QuaZIP handles the archive permanently and thus avoids the deletion.)
                {
                    QuaZip zip(fileName);
                    if (!zip.open(QuaZip::mdUnzip)) {
                        qWarning("testRead(): zip.open(): %d", zip.getZipError());
                        return;
                    }
                    zip.setFileNameCodec("IBM866");
                    QList<QuaZipFileInfo> updateFiles = zip.getFileInfoList();

                    // Rename all current files with available update.
                    for (int i=0;i<updateFiles.size();i++)
                    {
                        QString sourceFilePath = rootDirectory + "\\" + updateFiles[i].name;
                        QDir appDir( QCoreApplication::applicationDirPath() );

                        QFileInfo file( sourceFilePath );
                        if(file.exists())
                        {
                            //qDebug()<<tr("Moving file %1 to %2").arg(sourceFilePath).arg(sourceFilePath+".oldversion");
                            appDir.rename( sourceFilePath, sourceFilePath+".oldversion" );
                        }
                    }
                }

                // Install updated Files
                unzipUpdate(fileName, rootDirectory);

                // Delete update archive
                while(QFile::remove(fileName) )
                {
                };

                // Restart ap to clean up and start usual business
                restartApplication();

            }
            else qDebug()<<"Error: QNetworkReply is not readable!";
        }
        else
        {
            qDebug()<<"Download errors ocurred! HTTP Error Code:"<<httpstatuscode;
        }

        reply->deleteLater();
    }   // If !reply->error END
}   // httpUpdateDownloadFinished END

bool FvUpdater::unzipUpdate(const QString & filePath, const QString & extDirPath, const QString & singleFileName )
{
    QuaZip zip(filePath);

    if (!zip.open(QuaZip::mdUnzip)) {
        qWarning()<<tr("Error: Unable to open zip archive %1 for unzipping: %2").arg(filePath).arg(zip.getZipError());
        return false;
    }

    zip.setFileNameCodec("IBM866");

    //qWarning("Update contains %d files\n", zip.getEntriesCount());

    QuaZipFileInfo info;
    QuaZipFile file(&zip);
    QFile out;
    QString name;
    QDir appDir(extDirPath);
    for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile())
    {
        if (!zip.getCurrentFileInfo(&info)) {
            qWarning()<<tr("Error: Unable to retrieve fileInfo about the file to extract: %2").arg(zip.getZipError());
            return false;
        }

        if (!singleFileName.isEmpty())
            if (!info.name.contains(singleFileName))
                continue;

        if (!file.open(QIODevice::ReadOnly)) {
            qWarning()<<tr("Error: Unable to open file %1 for unzipping: %2").arg(filePath).arg(file.getZipError());
            return false;
        }

        name = QString("%1/%2").arg(extDirPath).arg(file.getActualFileName());

        if (file.getZipError() != UNZ_OK) {
            qWarning()<<tr("Error: Unable to retrieve zipped filename to unzip from %1: %2").arg(filePath).arg(file.getZipError());
            return false;
        }

        QFileInfo fi(name);
        appDir.mkpath(fi.absolutePath() );  // Ensure that subdirectories - if required - exist
        out.setFileName(name);
        out.open(QIODevice::WriteOnly);
        out.write( file.readAll() );
        out.close();

        if (file.getZipError() != UNZ_OK) {
            qWarning()<<tr("Error: Unable to unzip file %1: %2").arg(name).arg(file.getZipError());
            return false;
        }

        if (!file.atEnd()) {
            qWarning()<<tr("Error: Have read all available bytes, but pointer still does not show EOF: %1").arg(file.getZipError());
            return false;
        }

        file.close();

        if (file.getZipError() != UNZ_OK) {
            qWarning()<<tr("Error: Unable to close zipped file %1: %2").arg(name).arg(file.getZipError());
            return false;
        }
    }

    zip.close();

    if (zip.getZipError() != UNZ_OK) {
        qWarning()<<tr("Error: Unable to close zip archive file %1: %2").arg(filePath).arg(file.getZipError());
        return false;
    }

    return true;
}

void FvUpdater::SkipUpdate()
{
    qDebug() << "Skip update";

    FvAvailableUpdate* proposedUpdate = GetProposedUpdate();
    if (proposedUpdate == nullptr) {
        qWarning() << "Proposed update is NULL (shouldn't be at this point)";
        return;
    }

    // Start ignoring this particular version
    FVIgnoredVersions::IgnoreVersion(proposedUpdate->GetEnclosureVersion());
    if (m_mode == NORMAL) {
        hideUpdaterWindow();
        // hideUpdateConfirmationDialog();    // if any; shouldn't be shown at this point, but who knows
    }
}

void FvUpdater::RemindMeLater()
{
    qDebug() << "Remind me later";
    if (m_mode == NORMAL) {
        hideUpdaterWindow();
        // hideUpdateConfirmationDialog();    // if any; shouldn't be shown at this point, but who knows
    }
}

bool FvUpdater::CheckForUpdates(bool silentAsMuchAsItCouldGet)
{
    if (m_feedURL.isEmpty()) {
        qCritical() << "Please set feed URL via setFeedURL() before calling CheckForUpdates().";
        return false;
    }

    m_silentAsMuchAsItCouldGet = silentAsMuchAsItCouldGet;

    // Check if application's organization name and domain are set, fail otherwise
    // (nowhere to store QSettings to)
    if (QApplication::organizationName().isEmpty()) {
        qCritical() << "QApplication::organizationName is not set. Please do that.";
        return false;
    }
    if (QApplication::organizationDomain().isEmpty()) {
        qCritical() << "QApplication::organizationDomain is not set. Please do that.";
        return false;
    }

    // Set application name / version is not set yet
    if (QApplication::applicationName().isEmpty()) {
        qCritical() << "QApplication::applicationName is not set. Please do that.";
        return false;
    }
    if (QApplication::applicationVersion().isEmpty()) {
        qCritical() << "QApplication::applicationVersion is not set. Please do that.";
        return false;
    }

    cancelDownloadFeed();
    m_httpRequestAborted = false;
    startDownloadFeed(m_feedURL);

    return true;
}

bool FvUpdater::CheckForUpdatesSilent()
{
    return CheckForUpdates(true);
}

bool FvUpdater::CheckForUpdatesNotSilent()
{
    return CheckForUpdates(false);
}


void FvUpdater::startDownloadFeed(QUrl url)
{
    m_xml.clear();

    // Check SSL Fingerprint if required
    if(url.scheme()=="https" && !m_requiredSslFingerprint.isEmpty())
        if( !checkSslFingerPrint(url) ) // check failed
        {
            qWarning() << "Update aborted.";
            return;
        }

    QNetworkRequest request;
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
    request.setHeader(QNetworkRequest::UserAgentHeader, QApplication::applicationName());
    request.setUrl(url);

    m_reply = m_qnam.get(request);

    connect(m_reply, SIGNAL(readyRead()), this, SLOT(httpFeedReadyRead()));
    connect(m_reply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(httpFeedUpdateDataReadProgress(qint64, qint64)));
    connect(m_reply, SIGNAL(finished()), this, SLOT(httpFeedDownloadFinished()));
}

void FvUpdater::cancelDownloadFeed()
{
    if (m_reply) {
        m_httpRequestAborted = true;
        m_reply->abort();
    }
}

void FvUpdater::httpFeedReadyRead()
{
    // this slot gets called every time the QNetworkReply has new data.
    // We read all of its new data and write it into the file.
    // That way we use less RAM than when reading it at the finished()
    // signal of the QNetworkReply
    m_xml.addData(m_reply->readAll());
}

void FvUpdater::httpFeedUpdateDataReadProgress(qint64 bytesRead,
                                               qint64 totalBytes)
{
    Q_UNUSED(bytesRead);
    Q_UNUSED(totalBytes);

    if (m_httpRequestAborted) {
        return;
    }
}

void FvUpdater::httpFeedDownloadFinished()
{
    if (m_httpRequestAborted) {
        m_reply->deleteLater();
        return;
    }

    QVariant redirectionTarget = m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (m_reply->error()) {

        qDebug() << " error value " << m_reply->error();
        // Error.
        // showErrorDialog(tr("Feed download failed: %1.").arg(m_reply->errorString()), false);
        showErrorDialog(tr("Updates are unable to detect: %1.").arg(m_reply->errorString()), CRITICAL_MESSAGE);
        emit updatesDownloaded(false);

    } else if (!redirectionTarget.isNull()) {
        QUrl newUrl = m_feedURL.resolved(redirectionTarget.toUrl());

        m_feedURL = newUrl;
        m_reply->deleteLater();

        startDownloadFeed(m_feedURL);
        return;

    } else {
        // Done.
        xmlParseFeed();
        emit updatesDownloaded(true);

    }

    m_reply->deleteLater();
    m_reply = 0;
}

bool FvUpdater::xmlParseFeed()
{
    QString currentTag, currentQualifiedTag;

    QString xmlTitle, xmlLink, xmlReleaseNotesLink, xmlPubDate, xmlEnclosureUrl,
            xmlEnclosureVersion, xmlEnclosurePlatform, xmlEnclosureType,
            xmlReleaseNotesHtml;
    unsigned long xmlEnclosureLength = 0;

    // Parse
    while (!m_xml.atEnd()) {

        m_xml.readNext();

        if (m_xml.isStartElement()) {

            currentTag = m_xml.name().toString();
            currentQualifiedTag = m_xml.qualifiedName().toString();

            if (m_xml.name() == "item") {

                xmlTitle.clear();
                xmlLink.clear();
                xmlReleaseNotesLink.clear();
                xmlReleaseNotesHtml.clear();
                xmlPubDate.clear();
                xmlEnclosureUrl.clear();
                xmlEnclosureVersion.clear();
                xmlEnclosurePlatform.clear();
                xmlEnclosureLength = 0;
                xmlEnclosureType.clear();

            } else if (m_xml.name() == "enclosure") {

                QXmlStreamAttributes attribs = m_xml.attributes();

                if (attribs.hasAttribute("fervor:platform")) {

                    if (FvPlatform::CurrentlyRunningOnPlatform(attribs.value("fervor:platform").toString().trimmed())) {

                        xmlEnclosurePlatform = attribs.value("fervor:platform").toString().trimmed();

                        if (attribs.hasAttribute("url")) {
                            xmlEnclosureUrl = attribs.value("url").toString().trimmed();
                        } else {
                            xmlEnclosureUrl = "";
                        }

                        // First check for Sparkle's version, then overwrite with Fervor's version (if any)
                        if (attribs.hasAttribute("sparkle:version")) {
                            QString candidateVersion = attribs.value("sparkle:version").toString().trimmed();
                            if (!candidateVersion.isEmpty()) {
                                xmlEnclosureVersion = candidateVersion;
                            }
                        }
                        if (attribs.hasAttribute("fervor:version")) {
                            QString candidateVersion = attribs.value("fervor:version").toString().trimmed();
                            if (!candidateVersion.isEmpty()) {
                                xmlEnclosureVersion = candidateVersion;
                            }
                        }

                        if (attribs.hasAttribute("length")) {
                            xmlEnclosureLength = attribs.value("length").toString().toLong();
                        } else {
                            xmlEnclosureLength = 0;
                        }
                        if (attribs.hasAttribute("type")) {
                            xmlEnclosureType = attribs.value("type").toString().trimmed();
                        } else {
                            xmlEnclosureType = "";
                        }

                    }

                }

            }

        } else if (m_xml.isEndElement()) {

            if (m_xml.name() == "item") {

                // That's it - we have analyzed a single <item> and we'll stop
                // here (because the topmost is the most recent one, and thus
                // the newest version.
                return searchDownloadedFeedForUpdates(xmlTitle,
                                                      xmlLink,
                                                      xmlReleaseNotesLink,
                                                      xmlReleaseNotesHtml,
                                                      xmlPubDate,
                                                      xmlEnclosureUrl,
                                                      xmlEnclosureVersion,
                                                      xmlEnclosurePlatform,
                                                      xmlEnclosureLength,
                                                      xmlEnclosureType);
            }

        } else if (m_xml.isCharacters() && !m_xml.isWhitespace()) {

            if (currentTag == "title") {
                xmlTitle += m_xml.text().toString().trimmed();

            } else if (currentTag == "link") {
                xmlLink += m_xml.text().toString().trimmed();

            } else if (currentQualifiedTag == "sparkle:releaseNotesLink") {
                xmlReleaseNotesLink += m_xml.text().toString().trimmed();

            } else if (currentTag == "pubDate") {
                xmlPubDate += m_xml.text().toString().trimmed();

            } else if (currentTag == "description") {
                xmlReleaseNotesHtml += m_xml.text().toString().trimmed();

            }
        }

        if (m_xml.error() && m_xml.error() != QXmlStreamReader::PrematureEndOfDocumentError) {

            showErrorDialog(tr("Feed parsing failed: %1 %2.").arg(QString::number(m_xml.lineNumber()), m_xml.errorString()), NO_UPDATE_MESSAGE);
            return false;

        }
    }

    // No updates were found if we're at this point
    // (not a single <item> element found)
    showInformationDialog(tr("No updates were found."), false);

    return false;
}


bool FvUpdater::searchDownloadedFeedForUpdates(QString xmlTitle,
                                               QString xmlLink,
                                               QString xmlReleaseNotesLink,
                                               QString xmlReleaseNotesHtml,
                                               QString xmlPubDate,
                                               QString xmlEnclosureUrl,
                                               QString xmlEnclosureVersion,
                                               QString xmlEnclosurePlatform,
                                               unsigned long xmlEnclosureLength,
                                               QString xmlEnclosureType)
{
    qDebug() << "Title:" << xmlTitle;
    qDebug() << "Link:" << xmlLink;
    qDebug() << "Release notes link:" << xmlReleaseNotesLink;
    qDebug() << "Pub. date:" << xmlPubDate;
    qDebug() << "Enclosure URL:" << xmlEnclosureUrl;
    qDebug() << "Enclosure version:" << xmlEnclosureVersion;
    qDebug() << "Enclosure platform:" << xmlEnclosurePlatform;
    qDebug() << "Enclosure length:" << xmlEnclosureLength;
    qDebug() << "Enclosure type:" << xmlEnclosureType;

    // Validate Release notes
    if (xmlReleaseNotesHtml.isEmpty()) {
        if (xmlReleaseNotesLink.isEmpty()) {
            if (xmlLink.isEmpty()) {
                showErrorDialog(tr("Feed error: \"release notes\" link is empty"), NO_UPDATE_MESSAGE);
                return false;
            } else {
                xmlReleaseNotesLink = xmlLink;
            }
        } else {
            xmlLink = xmlReleaseNotesLink;
        }
        if (!(xmlLink.startsWith("http://") || xmlLink.startsWith("https://"))) {
            showErrorDialog(tr("Feed error: invalid \"release notes\" link"), NO_UPDATE_MESSAGE);
            return false;
        }
    }
    // Validate
    if (xmlEnclosureUrl.isEmpty() || xmlEnclosureVersion.isEmpty() || xmlEnclosurePlatform.isEmpty()) {
        showErrorDialog(tr("Feed error: invalid \"enclosure\" with the download link"), NO_UPDATE_MESSAGE);
        return false;
    }

    xmlEnclosureUrl = QUrl::fromPercentEncoding(xmlEnclosureUrl.toLatin1());
    qDebug() << " decoded URL:" << xmlEnclosureUrl;
    // Append dynamic url content - if supported in EnclosureUrl
    if (xmlEnclosureUrl.contains('%')) {
        xmlEnclosureUrl = xmlEnclosureUrl.arg(m_dynamicUrl);
    }
    qDebug() << "Download URL:" << xmlEnclosureUrl;

    // Relevant version?
    if (FVIgnoredVersions::VersionIsIgnored(xmlEnclosureVersion)) {
        qDebug() << "Version '" << xmlEnclosureVersion << "' is ignored, too old or something like that.";

        showInformationDialog(tr("No updates were found."), false);

        return true;    // Things have succeeded when you think of it.
    }


    //
    // Success! At this point, we have found an update that can be proposed
    // to the user.
    //
    if (m_proposedUpdate) {
        delete m_proposedUpdate; m_proposedUpdate = 0;
    }
    m_proposedUpdate = new FvAvailableUpdate();
    m_proposedUpdate->SetTitle(xmlTitle);
    m_proposedUpdate->SetReleaseNotesLink(xmlReleaseNotesLink);
    m_proposedUpdate->SetReleaseNotesHtml(xmlReleaseNotesHtml);
    m_proposedUpdate->SetPubDate(xmlPubDate);
    m_proposedUpdate->SetEnclosureUrl(xmlEnclosureUrl);
    m_proposedUpdate->SetEnclosureVersion(xmlEnclosureVersion);
    m_proposedUpdate->SetEnclosurePlatform(xmlEnclosurePlatform);
    m_proposedUpdate->SetEnclosureLength(xmlEnclosureLength);
    m_proposedUpdate->SetEnclosureType(xmlEnclosureType);

    // Show "look, there's an update" window
    showUpdaterWindowUpdatedWithCurrentUpdateProposal();

    return true;
}


void FvUpdater::showErrorDialog(QString message, msgType type)
{
    if (m_silentAsMuchAsItCouldGet) {
        if (type != CRITICAL_MESSAGE) {
            // Don't show errors in the silent mode
            return;
        }
    }
    else {
        if(type == NO_UPDATE_MESSAGE) {
            qDebug() << " Error " << message;
            message = "No updates were found.";
        }
    }
    if (m_mode == NORMAL) {
        QMessageBox dlFailedMsgBox;
        dlFailedMsgBox.setIcon(QMessageBox::Critical);
        dlFailedMsgBox.setWindowTitle(tr("Error"));
        dlFailedMsgBox.setText(message);
        dlFailedMsgBox.exec();
    } else {
        emit errorMessage(message);
    }
}

void FvUpdater::showInformationDialog(QString message, bool showEvenInSilentMode)
{
    if (m_silentAsMuchAsItCouldGet) {
        if (!showEvenInSilentMode) {
            // Don't show information dialogs in the silent mode
            return;
        }
    }
    if (m_mode == NORMAL) {
        QMessageBox dlInformationMsgBox;
        dlInformationMsgBox.setIcon(QMessageBox::Information);
        dlInformationMsgBox.setText(tr("Information"));
        dlInformationMsgBox.setInformativeText(message);
        dlInformationMsgBox.exec();
    } else {
        emit informationMessage(message);
    }
}

void FvUpdater::finishUpdate(QString pathToFinish)
{
    pathToFinish = pathToFinish.isEmpty() ? QCoreApplication::applicationDirPath() : pathToFinish;
    QDir appDir(pathToFinish);
    appDir.setFilter( QDir::Files | QDir::Dirs );

    QFileInfoList dirEntries = appDir.entryInfoList();
    foreach (QFileInfo fi, dirEntries)
    {
        if ( fi.isDir() )
        {
            QString dirname = fi.fileName();
            if ((dirname==".") || (dirname == ".."))
                continue;

            // recursive clean up subdirectory
            finishUpdate(fi.filePath());
        }
        else
        {
            if(fi.suffix()=="oldversion")
                if( !appDir.remove( fi.absoluteFilePath() ) )
                    qDebug()<<"Error: Unable to clean up file: "<<fi.absoluteFilePath();

        }
    }   // For each dir entry END
}

void FvUpdater::restartApplication()
{
    // Spawn a new instance of myApplication:
    QString app = QApplication::applicationFilePath();
    QStringList arguments = QApplication::arguments();
    QString wd = QDir::currentPath();
    qDebug() << app << arguments << wd;
    QProcess::startDetached(app, arguments, wd);
    QApplication::exit();
}

void FvUpdater::setRequiredSslFingerPrint(QString md5)
{
    m_requiredSslFingerprint = md5.remove(":");
}

QString FvUpdater::getRequiredSslFingerPrint()
{
    return m_requiredSslFingerprint;
}

bool FvUpdater::checkSslFingerPrint(QUrl urltoCheck)
{
    if(urltoCheck.scheme()!="https")
    {
        qWarning()<<tr("SSL fingerprint check: The url %1 is not a ssl connection!").arg(urltoCheck.toString());
        return false;
    }

    QSslSocket *socket = new QSslSocket(this);
    socket->connectToHostEncrypted(urltoCheck.host(), 443);
    if( !socket->waitForEncrypted(1000))    // waits until ssl emits encrypted(), max 1000msecs
    {
        qWarning()<<"SSL fingerprint check: Unable to connect SSL server: "<<socket->sslErrors();
        return false;
    }

    QSslCertificate cert = socket->peerCertificate();

    if(cert.isNull())
    {
        qWarning()<<"SSL fingerprint check: Unable to retrieve SSL server certificate.";
        return false;
    }

    // COmpare digests
    if(cert.digest().toHex() != m_requiredSslFingerprint)
    {
        qWarning()<<"SSL fingerprint check: FINGERPRINT MISMATCH! Server digest="<<cert.digest().toHex()<<", requiered ssl digest="<<m_requiredSslFingerprint;
        return false;
    }

    return true;
}

void FvUpdater::authenticationRequired ( QNetworkReply * reply, QAuthenticator * authenticator )
{
    if(reply==NULL || authenticator==NULL)
        return;

    if(!authenticator->user().isEmpty())    // If there is already a login user set but an authentication is still required: credentials must be wrong -> abort
    {
        reply->abort();
        qWarning()<<"Http authentication: Wrong credentials!";
        return;
    }

    authenticator->setUser(htAuthUsername);
    authenticator->setPassword(htAuthPassword);
}

void FvUpdater::setHtAuthCredentials(QString user, QString pass)
{
    htAuthUsername = user;
    htAuthPassword = pass;
}

void FvUpdater::setHtAuthUsername(QString user)
{
    htAuthUsername = user;
}

void FvUpdater::setHtAuthPassword(QString pass)
{
    htAuthPassword = pass;
}

void FvUpdater::setSkipVersionAllowed(bool allowed)
{
    skipVersionAllowed = allowed;
}

void FvUpdater::setRemindLaterAllowed(bool allowed)
{
    remindLaterAllowed = allowed;
}

bool FvUpdater::getSkipVersionAllowed()
{
    return skipVersionAllowed;
}

bool FvUpdater::getRemindLaterAllowed()
{
    return remindLaterAllowed;
}

void FvUpdater::decideWhatToDoWithCurrentUpdateProposal()
{
    QString policy = settings->value(FV_NEW_VERSION_POLICY_KEY).toString();
    if(policy == "install")
        InstallUpdate();
    else if(policy == "skip")
        SkipUpdate();
    else
        RemindMeLater();
}
