#ifndef FVUPDATER_H
#define FVUPDATER_H

#include <QObject>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QXmlStreamReader>
class FvUpdateWindow;
class FvUpdateConfirmDialog;
class FvAvailableUpdate;


typedef int (*check_before_update_callback)(void *, void *);

enum FERVOR_MODE {
    QUIET,
    NORMAL
};

class FvUpdater : public QObject
{
    Q_OBJECT

public:

    // Singleton
    static FvUpdater* sharedUpdater();
    static void drop();
    // Set / get feed URL
    void SetFeedURL(QUrl feedURL);
    void SetFeedURL(QString feedURL);
    QString GetFeedURL();

    // Set /get Dynamic download URL content
    void SetDynamicUrlContent(QString dynamicUrl);
    QString GetDynamicUrlContent();

    void SetCheckBeforeUpdate(check_before_update_callback callback, void* context);
    void SetFervorMode(FERVOR_MODE mode);

public slots:

    // Check for updates
    bool CheckForUpdates(bool silentAsMuchAsItCouldGet = true);

    // Aliases
    bool CheckForUpdatesSilent();
    bool CheckForUpdatesNotSilent();

    // Update window button slots
    void InstallUpdate();
    void SkipUpdate();
    void RemindMeLater();

signals:
    // This signal will inform, whether network is accessible or not
    void updatesDownloaded(bool success);
    void proposedVersionChanged(QString version);
    void proposedReleaseNotesChanged(QString content);
    void proposedReleaseNotesLinkChanged(QUrl link);
    void updateDownloadLinkReady(QString link);
    void updateAvailable();
    void errorMessage(QString message);
    void informationMessage(QString message);

    //
    // ---------------------------------------------------
    // ---------------------------------------------------
    // ---------------------------------------------------
    // ---------------------------------------------------
    //

protected:

    enum msgType {
        INFO_MESSAGE,         // shown always for users
        NO_UPDATE_MESSAGE,    // shown message only in not-silent mode, but modified
        CRITICAL_MESSAGE      // shown always
    };

    friend class FvUpdateWindow;        // Uses GetProposedUpdate() and others
    friend class FvUpdateConfirmDialog; // Uses GetProposedUpdate() and others
    FvAvailableUpdate* GetProposedUpdate();


protected slots:

    // Update confirmation dialog button slots
    void UpdateInstallationConfirmed();
    void UpdateInstallationNotConfirmed();

private:

    //
    // Singleton business
    //
    // (we leave just the declarations, so the compiler will warn us if we try
    //  to use those two functions by accident)
    FvUpdater();                            // Hide main constructor
    ~FvUpdater();                           // Hide main destructor
    FvUpdater(const FvUpdater&);            // Hide copy constructor
    FvUpdater& operator=(const FvUpdater&); // Hide assign op

    static FvUpdater* m_Instance;           // Singleton instance

    //Mode of Fervor operation
    //QUIET - use signals to communicate with the application
    //NORMAL - use default QWidget windows
    FERVOR_MODE m_mode;

    //
    // Windows / dialogs
    //
    FvUpdateWindow* m_updaterWindow;                                // Updater window (NULL if not shown)
    void showUpdaterWindowUpdatedWithCurrentUpdateProposal();       // Show updater window
    void hideUpdaterWindow();                                       // Hide + destroy m_updaterWindow
    void updaterWindowWasClosed();                                  // Sent by the updater window when it gets closed

    FvUpdateConfirmDialog* m_updateConfirmationDialog;                      // Update confirmation dialog (NULL if not shown)
    void showUpdateConfirmationDialogUpdatedWithCurrentUpdateProposal();    // Show update confirmation dialog
    void hideUpdateConfirmationDialog();                                    // Hide + destroy m_updateConfirmationDialog
    void updateConfirmationDialogWasClosed();                               // Sent by the update confirmation dialog when it gets closed

    // Available update (NULL if not fetched)
    FvAvailableUpdate* m_proposedUpdate;

    // If true, don't show the error dialogs and the "no updates." dialog
    // (silentAsMuchAsItCouldGet from CheckForUpdates() goes here)
    // Useful for automatic update checking upon application startup.
    bool m_silentAsMuchAsItCouldGet;

    // Dialogs (notifications)
    void showErrorDialog(QString message, msgType type = NO_UPDATE_MESSAGE);            // Show an error message
    void showInformationDialog(QString message, bool showEvenInSilentMode = false);     // Show an informational message


    //
    // HTTP feed fetcher infrastructure
    //
    QUrl m_feedURL;                 // Feed URL that will be fetched
    QString m_dynamicUrl;      // Dynamic download URL content
    QNetworkAccessManager m_qnam;
    QNetworkReply* m_reply;
    int m_httpGetId;
    bool m_httpRequestAborted;

    check_before_update_callback check_callback;
    void* check_context;

    void startDownloadFeed(QUrl url);   // Start downloading feed
    void cancelDownloadFeed();          // Stop downloading the current feed

private slots:

    void httpFeedReadyRead();
    void httpFeedUpdateDataReadProgress(qint64 bytesRead,
                                        qint64 totalBytes);
    void httpFeedDownloadFinished();


private:

    //
    // XML parser
    //
    QXmlStreamReader m_xml;             // XML data collector and parser
    bool xmlParseFeed();                // Parse feed in m_xml
    bool searchDownloadedFeedForUpdates(QString xmlTitle,
                                        QString xmlLink,
                                        QString xmlReleaseNotesLink,
                                        QString xmlReleaseNotesHtml,
                                        QString xmlPubDate,
                                        QString xmlEnclosureUrl,
                                        QString xmlEnclosureVersion,
                                        QString xmlEnclosurePlatform,
                                        unsigned long xmlEnclosureLength,
                                        QString xmlEnclosureType);


    //
    // Helpers
    //
    void installTranslator();           // Initialize translation mechanism

};

#endif // FVUPDATER_H
