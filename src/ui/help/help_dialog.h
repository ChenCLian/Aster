// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_HELP_DIALOG_H
#define ASTER_HELP_DIALOG_H
#include <QDialog>
#include <QUrl>
#include <QVector>
#include <QHash>
#include <QTimer>
class QTextBrowser;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QLineEdit;
class QLabel;
class QStackedWidget;
namespace aster::ui
{
struct HelpTopic
{
    QString path;
    QString anchor;
};
HelpTopic userGuideTopic(const QString &anchor = {});
HelpTopic licenseTopic();
HelpTopic methodHelpTopic(const QString &method);
HelpTopic parameterHelpTopic(const QString &method, const QString &parameter);
QUrl onlineHelpUrl(const HelpTopic &topic, const QString &host = QStringLiteral("github.com"));
bool localHelpAvailable(const HelpTopic &topic);
class HelpDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit HelpDialog(QWidget *parent = nullptr);
    bool navigate(const HelpTopic &topic);
    HelpTopic currentTopic() const { return m_current; }
  private:
    bool display(const HelpTopic &topic, int scroll = -1);
    void rebuildNavigation();
    void syncNavigation();
    void updateSearch();
    void buildSearchChunk();
    void findInPage(bool backwards = false);
    void savePosition();
    void visitHistory(int position);
    struct HistoryEntry { HelpTopic topic; int scroll = -1; };
    struct SearchEntry { HelpTopic topic; QString title, trail, markdown, body; };
    void followLink(const QUrl &url);
    void updateHistoryButtons();
    void refreshLinkColors();
    QTextBrowser *m_browser;
    QPushButton *m_back;
    QPushButton *m_forward;
    QPushButton *m_home, *m_findButton, *m_online, *m_close;
    QTreeWidget *m_contents;
    QListWidget *m_results;
    QStackedWidget *m_navigationStack;
    QLineEdit *m_globalSearch, *m_pageSearch;
    QLabel *m_searchStatus, *m_breadcrumb, *m_findStatus;
    QWidget *m_findBar;
    QHash<QString,QTreeWidgetItem*> m_topicItems;
    QVector<SearchEntry> m_searchEntries;
    QTimer m_searchTimer, m_indexTimer;
    QString m_navigationLanguage, m_lastFind;
    int m_indexPosition = 0, m_findPosition = -1;
    QVector<QPair<int, int>> m_findMatches;
    bool m_indexReady = false, m_scrollPending = false;
    bool m_openSearchResult = false;
    quint64 m_searchActivationSerial = 0;
    quint64 m_navigationSerial = 0;
    QVector<HistoryEntry> m_history;
    int m_position = -1;
    HelpTopic m_current;
};
void openHelp(QWidget *parent, const HelpTopic &topic);
} // namespace aster::ui
#endif
