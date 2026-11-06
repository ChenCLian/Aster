// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_HELP_DIALOG_H
#define ASTER_HELP_DIALOG_H
#include <QDialog>
#include <QUrl>
#include <QVector>
class QTextBrowser;
class QPushButton;
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
QUrl onlineHelpUrl(const HelpTopic &topic);
bool localHelpAvailable(const HelpTopic &topic);
class HelpDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit HelpDialog(QWidget *parent = nullptr);
    bool navigate(const HelpTopic &topic);
    HelpTopic currentTopic() const { return m_current; }
  private:
    bool display(const HelpTopic &topic);
    void followLink(const QUrl &url);
    void updateHistoryButtons();
    void refreshLinkColors();
    QTextBrowser *m_browser;
    QPushButton *m_back;
    QPushButton *m_forward;
    QVector<HelpTopic> m_history;
    int m_position = -1;
    HelpTopic m_current;
};
void openHelp(QWidget *parent, const HelpTopic &topic);
} // namespace aster::ui
#endif
