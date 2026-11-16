// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/settings/update_dialog.h"
#include "ui/common/presentation.h"
#include <QCoreApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QSettings>
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>
#include <QTabBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

namespace aster::ui {
namespace {
updates::UpdateConfig bundledConfig() {
    QFile file(":/updates/github.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return updates::UpdateConfig::fromJson(file.readAll());
}
// Release bodies are untrusted text. No HTML resources or automatic URL opens.
// Only explicit HTTPS links can leave the application; local images stay blocked.
class ReleaseDocument : public QTextDocument {
public:
    explicit ReleaseDocument(QObject *parent) : QTextDocument(parent) {}
protected:
    // Block at the document layer too: QTextBrowser's empty result alone lets
    // the default QTextDocument loader fall back to reading local files.
    QVariant loadResource(int, const QUrl &) override { return {}; }
};
class ReleaseNotes : public QTextBrowser {
public:
    explicit ReleaseNotes(QWidget *parent=nullptr) : QTextBrowser(parent) {
        setDocument(new ReleaseDocument(this));
        setOpenLinks(false); setOpenExternalLinks(false);
        setFrameShape(QFrame::NoFrame);
        setLineWrapMode(QTextEdit::WidgetWidth);
        connect(this,&QTextBrowser::anchorClicked,this,[this](const QUrl &url) {
            if (url.scheme()=="aster-note") {
                bool ok=false;const auto index=url.path().toInt(&ok);
                if (ok && index>=0 && index<m_parts.size() && m_parts[index].fold) {
                    m_parts[index].open=!m_parts[index].open;
                    const auto position=verticalScrollBar()->value();render();
                    verticalScrollBar()->setValue(position);
                }
            } else if (url.scheme()=="https" && !url.host().isEmpty() && url.userInfo().isEmpty()) {
                QDesktopServices::openUrl(url);
            }
        });
    }
    void setNotes(const QString &source) {
        m_parts.clear();
        const QRegularExpression details("<details\\b[^>]*>(.*?)</details\\s*>",QRegularExpression::CaseInsensitiveOption|QRegularExpression::DotMatchesEverythingOption);
        const QRegularExpression summary("<summary\\b[^>]*>(.*?)</summary\\s*>",QRegularExpression::CaseInsensitiveOption|QRegularExpression::DotMatchesEverythingOption);
        auto matches=details.globalMatch(source);qsizetype last=0;
        while(matches.hasNext()) {
            const auto match=matches.next();
            if(match.capturedStart()>last)m_parts.append({{},source.mid(last,match.capturedStart()-last),false,true});
            auto body=match.captured(1);const auto title=summary.match(body);
            if(title.hasMatch()) {
                auto heading=title.captured(1);heading.remove(QRegularExpression("<[^>]*>"));
                body.remove(title.capturedStart(),title.capturedLength());
                // Nested folds are flattened; their text is never silently lost.
                body.remove(QRegularExpression("</?(?:details|summary)\\b[^>]*>",QRegularExpression::CaseInsensitiveOption));
                m_parts.append({heading.trimmed(),body,true,false});
            } else m_parts.append({{},body,false,true});
            last=match.capturedEnd();
        }
        if(last<source.size())m_parts.append({{},source.mid(last),false,true});
        render();verticalScrollBar()->setValue(0);
    }
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
private:
    struct Part {QString title,body;bool fold,open;};
    QVector<Part> m_parts;
    QString markdown(const QString &body) {
        ReleaseDocument document(nullptr);document.setDefaultFont(font());
        document.setMarkdown(body,QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)|QTextDocument::MarkdownNoHTML);
        auto html=document.toHtml();
        const auto start=html.indexOf(QRegularExpression("<body[^>]*>"));
        if(start>=0){const auto content=html.indexOf('>',start)+1;html=html.mid(content,html.lastIndexOf("</body>")-content);}
        return html;
    }
    void render() {
        QString html;
        for(qsizetype i=0;i<m_parts.size();++i) {
            const auto &part=m_parts[i];
            if(part.fold)html+=QString("<p><a href=\"aster-note:%1\">%2 <b>%3</b></a></p>").arg(i).arg(part.open?QStringLiteral("▾"):QStringLiteral("▸"),part.title.toHtmlEscaped());
            if(!part.fold || part.open)html+=markdown(part.body);
        }
        setHtml(html);
    }
};
QLabel *wrappedLabel(const QString &name) {
    auto *label=new QLabel;label->setObjectName(name);label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    return label;
}
}
UpdateDialog::UpdateDialog(QWidget *parent) : UpdateDialog(bundledConfig(),parent) {}
UpdateDialog::UpdateDialog(updates::UpdateConfig config,QWidget *parent,QNetworkAccessManager *manager)
    : QDialog(parent),m_config(std::move(config)),m_checker(new updates::UpdateChecker(m_config,this,manager)),
      m_download(new updates::PackageDownload(m_config,this,manager)),
      m_onlineAllowed(!updates::isAlphaVersion(QCoreApplication::applicationVersion())) {
    setObjectName("updateDialog");setWindowTitle(text("update.window_title"));
    resize(660,580);setMinimumSize(520,480);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(24,22,24,20);layout->setSpacing(14);
    auto *header=new QHBoxLayout;header->setSpacing(12);
    auto *icon=new QLabel;icon->setPixmap(QIcon(":/app/aster.png").pixmap(36,36));header->addWidget(icon);
    auto *identity=new QVBoxLayout;identity->setSpacing(2);
    auto *title=new QLabel(text("update.window_title"));title->setObjectName("PageTitle");identity->addWidget(title);
    auto *installed=wrappedLabel("updateInstalled");installed->setText(text("update.installed")+"  "+QCoreApplication::applicationVersion());identity->addWidget(installed);
    header->addLayout(identity,1);
    m_check=new QPushButton(text("update.refresh"));m_check->setObjectName("updateCheck");m_check->setAutoDefault(false);header->addWidget(m_check);
    layout->addLayout(header);

    m_channels=new QTabBar;m_channels->setObjectName("updateChannel");m_channels->setExpanding(false);m_channels->setDrawBase(false);
    m_channels->addTab(text("update.stable"));m_channels->addTab(text("update.beta"));
    const auto current=updates::ReleaseVersion::parse(QCoreApplication::applicationVersion());
    if(current && !current->prerelease.isEmpty() && current->prerelease.first().compare("beta",Qt::CaseInsensitive)==0)m_channels->setCurrentIndex(1);
    m_channels->setAccessibleName(text("update.channel"));layout->addWidget(m_channels);

    m_summary=new QFrame;m_summary->setObjectName("updateSummary");auto *summary=new QVBoxLayout(m_summary);summary->setContentsMargins(16,14,16,14);summary->setSpacing(6);
    m_heading=wrappedLabel("updateHeading");summary->addWidget(m_heading);
    m_status=wrappedLabel("updateStatus");m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);summary->addWidget(m_status);
    m_releaseMeta=wrappedLabel("updateReleaseMeta");summary->addWidget(m_releaseMeta);
    m_progress=new QProgressBar;m_progress->setObjectName("updateProgress");m_progress->setRange(0,0);m_progress->setTextVisible(false);m_progress->setMaximumHeight(4);summary->addWidget(m_progress);
    layout->addWidget(m_summary);

    auto *notesHeader=new QHBoxLayout;auto *notesTitle=new QLabel(text("update.notes"));notesTitle->setObjectName("SectionTitle");notesHeader->addWidget(notesTitle);notesHeader->addStretch();
    auto *source=new QLabel(text("update.source_github"));source->setObjectName("updateSourceLabel");notesHeader->addWidget(source);layout->addLayout(notesHeader);
    m_notes=new ReleaseNotes(this);m_notes->setObjectName("updateNotes");m_notes->setAccessibleName(text("update.notes"));m_notes->setMinimumHeight(90);layout->addWidget(m_notes,1);
    m_details=new QToolButton;m_details->setObjectName("updateDetails");m_details->setText(text("update.details"));m_details->setCheckable(true);m_details->setArrowType(Qt::RightArrow);m_details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    layout->addWidget(m_details,0,Qt::AlignLeft);
    m_diagnostics=wrappedLabel("updateDiagnostics");m_diagnostics->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_diagnostics);
    connect(m_details,&QToolButton::toggled,this,[this](bool open){m_diagnostics->setVisible(open);m_details->setArrowType(open?Qt::DownArrow:Qt::RightArrow);});
    m_downloadOptions=new QWidget;auto *options=new QVBoxLayout(m_downloadOptions);options->setContentsMargins(0,0,0,0);options->setSpacing(6);
    auto *sourceRow=new QHBoxLayout;sourceRow->addWidget(new QLabel(text("update.download_source")));
    m_source=new QComboBox;m_source->setObjectName("updateDownloadSource");sourceRow->addWidget(m_source,1);options->addLayout(sourceRow);
    auto *directoryRow=new QHBoxLayout;m_destination=wrappedLabel("updateDestination");directoryRow->addWidget(m_destination,1);
    auto *choose=new QPushButton(text("update.choose_directory"));choose->setAutoDefault(false);choose->setObjectName("updateChooseDirectory");directoryRow->addWidget(choose);options->addLayout(directoryRow);
    m_directory=QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);if(!QDir(m_directory).exists())m_directory=QDir::homePath();
    m_destination->setText(text("update.destination").arg(QDir::toNativeSeparators(m_directory)));
    connect(choose,&QPushButton::clicked,this,[this]{const auto folder=QFileDialog::getExistingDirectory(this,text("update.choose_directory"),m_directory);if(!folder.isEmpty()){m_directory=folder;m_destination->setText(text("update.destination").arg(QDir::toNativeSeparators(folder)));}});
    layout->addWidget(m_downloadOptions);m_downloadOptions->hide();
    m_hint=wrappedLabel("updateManualHint");m_hint->setText(text("update.manual_hint"));layout->addWidget(m_hint);
    auto *footer=new QHBoxLayout;footer->setSpacing(10);
    auto *releases=new QPushButton(text("update.releases"));releases->setObjectName("updateReleases");footer->addWidget(releases);footer->addStretch();
    m_cancel=new QPushButton(text("update.cancel"));m_cancel->setObjectName("updateCancel");footer->addWidget(m_cancel);
    auto *close=new QPushButton(text("update.close"));close->setObjectName("updateClose");footer->addWidget(close);
    m_open=new QPushButton(text("update.open_release"));m_open->setObjectName("updateOpen");m_open->setProperty("updateRole","primary");footer->addWidget(m_open);
    for(auto *button:{releases,m_cancel,close,m_open})button->setAutoDefault(false);
    layout->addLayout(footer);
    connect(close,&QPushButton::clicked,this,&QDialog::reject);
    connect(this,&QDialog::finished,this,[this]{m_closed=true;m_checker->cancel();m_download->cancel();});
    connect(m_cancel,&QPushButton::clicked,this,[this]{m_checker->cancel();m_download->cancel();});
    connect(m_check,&QPushButton::clicked,this,[this]{check();});
    connect(m_channels,&QTabBar::currentChanged,this,[this]{check();});
    connect(m_checker,&updates::UpdateChecker::finished,this,[this](const updates::UpdateResult &result){showResult(result);});
    connect(m_open,&QPushButton::clicked,this,[this]{if(m_downloadDone)openDirectory();else if(m_packageReady)download();else if(m_retry)check();else browse(m_releaseUrl);});
    connect(releases,&QPushButton::clicked,this,[this]{browse(m_config.releasesUrl());});
    connect(m_download,&updates::PackageDownload::prepared,this,&UpdateDialog::packagePrepared);
    connect(m_download,&updates::PackageDownload::sourceChanged,this,[this](const QString &source){m_usedSource=source;if(auto *label=findChild<QLabel*>("updateSourceLabel"))label->setText(text("update.source_actual").arg(source));});
    connect(m_download,&updates::PackageDownload::phaseChanged,this,[this](const QString &key){
        if(m_closed)return;
        setState("checking",text(qPrintable(key)),text("update.download_hint"));m_progress->setRange(0,0);m_downloadOptions->setEnabled(false);
    });
    connect(m_download,&updates::PackageDownload::progress,this,[this](qint64 done,qint64 total){
        if(m_closed)return;
        m_progress->setRange(0,1000);m_progress->setValue(total?int(done*1000/total):0);
        m_status->setText(text("update.download_progress").arg(double(done)/1048576,0,'f',1).arg(double(total)/1048576,0,'f',1));
    });
    connect(m_download,&updates::PackageDownload::finished,this,[this](bool success,const QString &error){
        if(m_closed)return;
        m_downloadOptions->setEnabled(true);m_downloadDone=success;
        setState(success?"available":"error",text(success?"update.download_complete":"update.download_failed"),text(success?"update.open_directory_hint":qPrintable(error)));
        m_open->setEnabled(true);m_open->setText(text(success?"update.open_directory":"update.retry_download"));
        if(success){m_downloadOptions->hide();m_hint->setText(text("update.download_finished_hint"));QSettings().setValue("updates/lastSource",m_usedSource);if(!error.isEmpty()){m_details->show();m_diagnostics->setText(text(qPrintable(error)));}}
        else {m_details->show();m_diagnostics->setText(text("update.diagnostic_text").arg(m_config.releasesUrl().toString(),error));}
    });
    setState("idle",text("update.ready_heading"),text("update.ready_hint"));
    static_cast<ReleaseNotes*>(m_notes)->setNotes(text("update.notes_waiting"));
    if(!m_onlineAllowed){setState("development",text("update.development_heading"),text("update.alpha_development_only"));m_check->setEnabled(false);m_channels->setEnabled(false);}
    setTabOrder(m_channels,m_check);setTabOrder(m_check,m_notes);setTabOrder(m_notes,m_details);setTabOrder(m_details,releases);setTabOrder(releases,close);setTabOrder(close,m_open);
}
void UpdateDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if(m_autoCheckQueued || !m_onlineAllowed)return;
    m_autoCheckQueued=true;
    QTimer::singleShot(0,this,[this]{if(isVisible()&&!m_closed)check();});
}
void UpdateDialog::reject(){m_closed=true;m_checker->cancel();m_download->cancel();QDialog::reject();}
void UpdateDialog::setState(const char *state,const QString &heading,const QString &detail){
    const bool busy=qstrcmp(state,"checking")==0;
    m_summary->setProperty("state",QString::fromLatin1(state));m_summary->style()->unpolish(m_summary);m_summary->style()->polish(m_summary);
    m_heading->setText(heading);m_status->setText(detail);m_status->setVisible(!detail.isEmpty());
    m_releaseMeta->clear();m_releaseMeta->hide();m_progress->setVisible(busy);
    m_check->setEnabled(m_onlineAllowed&&!busy);m_channels->setEnabled(m_onlineAllowed&&!busy);
    m_cancel->setEnabled(busy);m_cancel->setVisible(busy);
    m_open->setEnabled(false);m_open->setVisible(!busy);m_open->setText(text("update.open_release"));
    m_details->setChecked(false);m_details->hide();m_diagnostics->hide();m_diagnostics->clear();
    m_retry=false;m_releaseUrl=QUrl{};
}
void UpdateDialog::check(){
    if(m_closed || !m_onlineAllowed || m_checker->busy() || m_download->busy())return;
    m_packageReady=false;m_downloadDone=false;m_selectedRelease.reset();m_downloadOptions->hide();m_hint->setText(text("update.manual_hint"));
    if(auto *source=findChild<QLabel*>("updateSourceLabel"))source->setText(text("update.source_github"));
    setState("checking",text("update.checking_heading"),text("update.checking_hint"));
    static_cast<ReleaseNotes*>(m_notes)->setNotes(text("update.notes_waiting"));
    m_checker->checkChannel(QCoreApplication::applicationVersion(),m_channels->currentIndex()==1);
}
void UpdateDialog::showResult(const updates::UpdateResult &result){
    using State=updates::UpdateResult::State;
    if(m_closed)return;
    QString heading,detail=text(qPrintable(result.messageKey));const char *state="idle";
    switch(result.state){
    case State::Available:state="available";heading=text("update.available_heading");if(result.release)detail=text("update.version_line").arg(result.release->tag);break;
    case State::Current:state="current";heading=text("update.current_heading");detail=text("update.current_channel_hint");break;
    case State::NoReleases:state="empty";heading=text("update.empty_heading");detail=text("update.empty_hint");break;
    case State::Failed:state="error";heading=text("update.failed_heading");break;
    case State::Cancelled:state="cancelled";heading=text("update.cancelled_heading");detail=text("update.cancelled_hint");break;
    }
    if(result.partial && (result.state==State::Current || result.state==State::NoReleases)){
        heading=text("update.mirror_no_new");detail=text("update.source_partial_hint");
    }
    setState(state,heading,detail);
    if(auto *source=findChild<QLabel*>("updateSourceLabel")){
        const auto name=result.release?result.release->source:result.partial?QString("Gitee"):QString("GitHub");
        source->setText(text("update.source_actual").arg(name)+(result.partial?text("update.source_partial"):QString{}));
        source->setToolTip(result.partial?text("update.source_partial_hint"):QString{});
    }
    if(result.release){
        const auto &release=*result.release;QStringList meta;
        meta<<text(m_channels->currentIndex()==1?"update.beta":"update.stable");
        if(result.state!=State::Available)meta<<text("update.channel_version").arg(release.tag);
        if(release.publishedAt.isValid())meta<<release.publishedAt.date().toString(Qt::ISODate);
        m_releaseMeta->setText(meta.join("  ·  "));m_releaseMeta->show();
        static_cast<ReleaseNotes*>(m_notes)->setNotes(release.notes.trimmed().isEmpty()?text("update.notes_empty"):release.notes);
        if(result.state==State::Available){m_releaseUrl=release.pageUrl;m_open->setEnabled(true);}
    }else static_cast<ReleaseNotes*>(m_notes)->setNotes(text(result.state==State::Failed?"update.notes_failed":"update.notes_empty"));
    if(result.state==State::Failed){
        m_details->show();m_diagnostics->setText(text("update.diagnostic_text").arg(m_config.releasesUrl().toString(),result.messageKey));
    }
    if(result.state==State::Failed || result.state==State::Cancelled){m_retry=true;m_open->setText(text("update.retry"));m_open->setEnabled(m_onlineAllowed);}
    if(result.state==State::Current || result.state==State::NoReleases)m_open->setText(text("update.no_new_release"));
    if(result.state==State::Available && result.release && !m_config.packageMetadata.isEmpty()) {
        m_selectedRelease=result.release;m_download->prepare(result.release->tag,result.release->source);
    }
}
void UpdateDialog::packagePrepared(bool available,const QString &error){
    if(m_closed||!m_selectedRelease)return;
    m_packageReady=available;m_downloadDone=false;
    if(error=="update.cancelled"){m_selectedRelease.reset();showResult({updates::UpdateResult::State::Cancelled,error,{}});return;}
    setState("available",text("update.available_heading"),text("update.version_line").arg(m_selectedRelease->tag));
    m_releaseUrl=m_selectedRelease->pageUrl;m_open->setEnabled(true);
    if(available){
        const auto p=*m_download->package();m_source->clear();m_source->addItem(text("update.source_auto"),"auto");
        for(const auto &source:p.sources)m_source->addItem(source.name,source.name);
        m_releaseMeta->setText(text("update.package_size_label").arg(double(p.bytes)/1048576,0,'f',1));m_releaseMeta->show();
        m_open->setText(text("update.download_extract"));m_downloadOptions->setEnabled(true);m_downloadOptions->show();m_hint->setText(text("update.download_hint"));
    }else {
        m_hint->setText(text("update.manual_fallback"));
        if(error!="update.package_missing"){m_details->show();m_diagnostics->setText(text(qPrintable(error)));}
    }
}
void UpdateDialog::download(){
    m_download->start(m_directory,m_source->currentData().toString(),QSettings().value("updates/lastSource").toString());
}
void UpdateDialog::openDirectory(){
    if(!QDesktopServices::openUrl(QUrl::fromLocalFile(m_download->completedDirectory()))){m_status->setText(text("update.directory_open_failed").arg(QDir::toNativeSeparators(m_download->completedDirectory())));m_status->show();}
}
void UpdateDialog::browse(const QUrl &url){
    if(url.scheme()!="https" || (url.host()!="github.com"&&url.host()!="gitee.com") || !url.userInfo().isEmpty() || !QDesktopServices::openUrl(url)){
        m_status->setText(text("update.error_browser"));m_status->show();m_details->show();m_diagnostics->setText(m_config.releasesUrl().toString());
    }
}
} // namespace aster::ui
