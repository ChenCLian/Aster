#ifndef ASTER_UPDATE_CHECKER_H
#define ASTER_UPDATE_CHECKER_H
#include "update/release_feed.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
class QNetworkReply;
namespace aster::updates {
struct UpdateResult {
    enum class State { Available, Current, NoReleases, Failed, Cancelled };
    State state=State::Failed;
    QString messageKey;
    std::optional<Release> release;
};
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    // The injected manager is a test seam. Production always uses HTTPS GitHub.
    explicit UpdateChecker(UpdateConfig config,QObject *parent=nullptr,QNetworkAccessManager *manager=nullptr);
    ~UpdateChecker() override;
    bool busy() const {return m_busy;}
    void check(const QString &current,bool includePrereleases=false);
    void cancel();
signals:
    void finished(aster::updates::UpdateResult result);
private:
    void requestPage();
    void consume();
    void pageFinished();
    void finish(UpdateResult result);
    UpdateConfig m_config;
    QNetworkAccessManager *m_manager;
    QPointer<QNetworkReply> m_reply;
    QTimer m_deadline;
    QByteArray m_body;
    QVector<Release> m_releases;
    std::optional<ReleaseVersion> m_current;
    QString m_transportFailure;
    int m_page=0;
    bool m_busy=false,m_includePrereleases=false;
};
}
Q_DECLARE_METATYPE(aster::updates::UpdateResult)
#endif
