// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/help/help_dialog.h"
#include "ui/help/help_catalog.h"
#include <QElapsedTimer>
#include <QTreeWidget>
#include <QListWidget>
#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <algorithm>
#include "ui/common/presentation.h"
#include "ui/settings/language_catalog.h"
#include "ui/settings/theme_manager.h"
#include <QTextBlock>
#include <QTextFragment>
#include <QDesktopServices>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace aster::ui
{
namespace
{
QString safePath(const QString &path)
{
    // Help is read only from the compiled manual, never from arbitrary local files.
    if (path.isEmpty() || path.startsWith('/') || path.contains('\\') || path.contains(':'))
        return {};
    const QString clean = QDir::cleanPath(path);
    if (clean == ".." || clean.startsWith("../"))
        return {};
    return clean;
}
HelpTopic localTopic(const HelpTopic &topic)
{
    const QString path = safePath(topic.path);
    if (!path.isEmpty() && topic.path.endsWith('/'))
    {
        const QString index = path + "/README.md";
        if (QFile::exists(":/help/" + index)) return {index, topic.anchor};
    }
    return topic;
}
struct Page
{
    QString html;
    QStringList anchors;
    bool found = false;
};
Page readPage(const HelpTopic &topic)
{
    Page page;
    const QString path = safePath(topic.path);
    if (path.isEmpty())
        return page;
    static QHash<QString, Page> pages;
    if (pages.contains(path))
    {
        Page cached = pages.value(path);
        if (!topic.anchor.isEmpty() && !cached.anchors.contains(topic.anchor)) cached.found = false;
        return cached;
    }
    const QString resourcePath = ":/help/" + path;
    if (!QFileInfo(resourcePath).isFile()) return page;
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return page;
    const QString markdown = QString::fromUtf8(file.readAll());
    // License texts are immutable plain text; Markdown would reinterpret their
    // indentation, numbered clauses and line breaks.
    if (!path.endsWith(".md", Qt::CaseInsensitive))
    {
        QTextDocument document;
        auto font = QApplication::font(); font.setPointSizeF(std::max(11., font.pointSizeF()));
        document.setDefaultFont(font);
        document.setPlainText(markdown);
        page.html = document.toHtml();
        page.found = true;
        pages.insert(path, page);
        if (!topic.anchor.isEmpty()) page.found = false;
        return page;
    }
    // QTextDocument does not generate heading anchors. Retain explicit stable IDs
    // and GitHub-style heading fragments in the rendered local document.
    QVector<QStringList> headings;
    for (const auto &section : helpSections(path)) {
        headings << section.anchors;
        page.anchors << section.anchors;
    }
    QTextDocument document;
    auto font = QApplication::font(); font.setPointSizeF(std::max(11., font.pointSizeF()));
        document.setDefaultFont(font);
    document.setMarkdown(markdown);
    const QString rendered = document.toHtml();
    QRegularExpression htmlHeading("<h[1-6]\\b");
    auto matches = htmlHeading.globalMatch(rendered);
    int from = 0, index = 0;
    while (matches.hasNext())
    {
        const auto match = matches.next();
        page.html += rendered.mid(from, match.capturedStart() - from);
        if (index < headings.size())
            for (const auto &anchor : headings[index])
                page.html += "<a name=\"" + anchor.toHtmlEscaped() + "\"></a>";
        from = match.capturedStart();
        ++index;
    }
    page.html += rendered.mid(from);
    page.found = true;
    pages.insert(path, page);
    if (!topic.anchor.isEmpty() && !page.anchors.contains(topic.anchor)) page.found = false;
    return page;
}
void openOnline(QWidget *parent, const HelpTopic &topic)
{
    const QUrl url = onlineHelpUrl(topic);
    if (!url.isEmpty() && !QDesktopServices::openUrl(url))
        QMessageBox::warning(parent, text("help.title"), text("help.open_failed"));
}
}
HelpTopic userGuideTopic(const QString &anchor)
{
    return {LanguageCatalog::instance().language().startsWith("zh")
                ? "docs/USER_GUIDE.md" : "docs/USER_GUIDE_EN.md", anchor};
}
HelpTopic licenseTopic()
{
    return {LanguageCatalog::instance().language().startsWith("zh")
                ? "docs/LICENSE_GUIDE_ZH.md" : "COPYRIGHT.md", {}};
}
HelpTopic methodHelpTopic(const QString &method)
{
    return {LanguageCatalog::instance().language().startsWith("zh")
                ? "docs/reference/task_parameters.md" : "docs/reference/task_parameters_en.md",
            "m-" + method};
}
HelpTopic parameterHelpTopic(const QString &method, const QString &parameter)
{
    auto topic = methodHelpTopic(method);
    topic.anchor += "-p-" + parameter;
    return topic;
}
QUrl onlineHelpUrl(const HelpTopic &topic, const QString &host)
{
    const QString path = safePath(topic.path);
    if (path.isEmpty() || (host != "github.com" && host != "gitee.com")) return {};
    QUrl url("https://" + host);
    url.setPath((topic.path.endsWith('/') ? "/ChenCLian/Aster/tree/main/"
                                         : "/ChenCLian/Aster/blob/main/") + path);
    url.setFragment(topic.anchor);
    return url;
}
bool localHelpAvailable(const HelpTopic &topic) { return readPage(localTopic(topic)).found; }

HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("helpDialog");
    setWindowTitle(text("help.title"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1080, 760); setMinimumSize(680, 420);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14); layout->setSpacing(10);
    auto *toolbar = new QHBoxLayout;
    m_back = new QPushButton; m_back->setObjectName("helpBack");
    m_forward = new QPushButton; m_forward->setObjectName("helpForward");
    m_home = new QPushButton; m_home->setObjectName("helpHome");
    m_findButton = new QPushButton; m_findButton->setObjectName("helpFindToggle");
    toolbar->addWidget(m_back); toolbar->addWidget(m_forward); toolbar->addWidget(m_home);
    toolbar->addStretch(); toolbar->addWidget(m_findButton); layout->addLayout(toolbar);
    auto *split = new QSplitter; split->setObjectName("helpSplitter");
    split->setChildrenCollapsible(false);
    auto *sidebar = new QWidget; sidebar->setMinimumWidth(190);
    auto *side = new QVBoxLayout(sidebar); side->setContentsMargins(0,0,0,0); side->setSpacing(8);
    m_globalSearch = new QLineEdit; m_globalSearch->setObjectName("helpGlobalSearch");
    m_globalSearch->setClearButtonEnabled(true); side->addWidget(m_globalSearch);
    m_searchStatus = new QLabel; m_searchStatus->setObjectName("helpSearchStatus");
    m_searchStatus->setWordWrap(true); m_searchStatus->setTextFormat(Qt::PlainText);
    side->addWidget(m_searchStatus);
    m_navigationStack = new QStackedWidget; side->addWidget(m_navigationStack,1);
    m_contents = new QTreeWidget; m_contents->setObjectName("helpContents");
    m_contents->setHeaderHidden(true); m_contents->setIndentation(14);
    m_contents->setUniformRowHeights(true); m_contents->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_navigationStack->addWidget(m_contents);
    m_results = new QListWidget; m_results->setObjectName("helpSearchResults");
    m_results->setWordWrap(true); m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_navigationStack->addWidget(m_results);
    auto *content = new QWidget; content->setMinimumWidth(280);
    auto *body = new QVBoxLayout(content); body->setContentsMargins(0,0,0,0); body->setSpacing(8);
    m_breadcrumb = new QLabel; m_breadcrumb->setObjectName("helpBreadcrumb");
    m_breadcrumb->setTextFormat(Qt::PlainText); m_breadcrumb->setWordWrap(true);
    m_breadcrumb->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred); body->addWidget(m_breadcrumb);
    m_findBar = new QWidget; m_findBar->setObjectName("helpFindBar");
    auto *findLayout = new QHBoxLayout(m_findBar); findLayout->setContentsMargins(0,0,0,0);
    m_pageSearch = new QLineEdit; m_pageSearch->setObjectName("helpSearch"); m_pageSearch->setClearButtonEnabled(true);
    m_findStatus = new QLabel; m_findStatus->setObjectName("helpFindStatus");
    auto *previous = new QPushButton; previous->setObjectName("helpFindPrevious");
    auto *next = new QPushButton; next->setObjectName("helpFindNext");
    auto *dismiss = new QPushButton(QString::fromUtf8("×")); dismiss->setObjectName("helpFindClose"); dismiss->setFixedWidth(28);
    findLayout->addWidget(m_pageSearch,1); findLayout->addWidget(m_findStatus);
    findLayout->addWidget(previous); findLayout->addWidget(next); findLayout->addWidget(dismiss);
    body->addWidget(m_findBar); m_findBar->hide();
    m_browser = new QTextBrowser; m_browser->setObjectName("helpBrowser");
    m_browser->setOpenLinks(false); m_browser->setOpenExternalLinks(false);
    m_browser->setMinimumWidth(0); m_browser->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Expanding);
    body->addWidget(m_browser,1);
    split->addWidget(sidebar); split->addWidget(content); split->setStretchFactor(0,0); split->setStretchFactor(1,1);
    split->setSizes({270,780}); layout->addWidget(split,1);
    auto *footer = new QHBoxLayout;
    m_online = new QPushButton; m_online->setObjectName("helpOnline");
    m_close = new QPushButton; m_close->setObjectName("helpClose");
    footer->addWidget(m_online); footer->addStretch(); footer->addWidget(m_close); layout->addLayout(footer);
    connect(m_close,&QPushButton::clicked,this,&QDialog::close);
    connect(m_home,&QPushButton::clicked,this,[this]{navigate(userGuideTopic());});
    connect(m_online,&QPushButton::clicked,this,[this]{openOnline(this,m_current);});
    connect(m_back,&QPushButton::clicked,this,[this]{visitHistory(m_position-1);});
    connect(m_forward,&QPushButton::clicked,this,[this]{visitHistory(m_position+1);});
    connect(m_browser,&QTextBrowser::anchorClicked,this,&HelpDialog::followLink);
    connect(&ThemeManager::instance(),&ThemeManager::changed,this,&HelpDialog::refreshLinkColors);
    auto showFind=[this]{m_findBar->show();m_pageSearch->setFocus();m_pageSearch->selectAll();};
    connect(m_findButton,&QPushButton::clicked,this,showFind);
    connect(new QShortcut(QKeySequence::Find,this),&QShortcut::activated,this,showFind);
    connect(new QShortcut(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_F),this),&QShortcut::activated,this,[this]{m_globalSearch->setFocus();m_globalSearch->selectAll();});
    connect(dismiss,&QPushButton::clicked,this,[this]{m_pageSearch->clear();m_findBar->hide();m_browser->setFocus();});
    connect(m_pageSearch,&QLineEdit::textChanged,this,[this]{
        m_lastFind.clear();m_findMatches.clear();m_findPosition=-1;m_findStatus->clear();
        if(!m_pageSearch->text().isEmpty())m_findBar->show();
        auto cursor=m_browser->textCursor();cursor.clearSelection();m_browser->setTextCursor(cursor);
    });
    connect(m_pageSearch,&QLineEdit::returnPressed,this,[this]{findInPage();});
    connect(next,&QPushButton::clicked,this,[this]{findInPage();});
    connect(previous,&QPushButton::clicked,this,[this]{findInPage(true);});
    connect(new QShortcut(QKeySequence::FindNext,this),&QShortcut::activated,this,[this]{findInPage();});
    connect(new QShortcut(QKeySequence::FindPrevious,this),&QShortcut::activated,this,[this]{findInPage(true);});
    auto *reverse = new QShortcut(QKeySequence(Qt::SHIFT|Qt::Key_Return),m_pageSearch);
    reverse->setContext(Qt::WidgetShortcut); connect(reverse,&QShortcut::activated,this,[this]{findInPage(true);});
    m_searchTimer.setSingleShot(true);m_searchTimer.setInterval(120);
    m_indexTimer.setSingleShot(true);m_indexTimer.setInterval(0);
    connect(&m_searchTimer,&QTimer::timeout,this,&HelpDialog::updateSearch);
    connect(&m_indexTimer,&QTimer::timeout,this,&HelpDialog::buildSearchChunk);
    connect(m_globalSearch,&QLineEdit::textChanged,this,[this]{
        m_openSearchResult=false;
        // Old results must not remain clickable under a newly entered query.
        m_results->clear();m_searchStatus->clear();
        if(m_globalSearch->text().trimmed().isEmpty()){m_searchTimer.stop();updateSearch();}
        else { m_navigationStack->setCurrentWidget(m_results);m_searchTimer.start(); }
    });
    connect(m_contents,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *item){
        if(item && !item->data(0,Qt::UserRole).toString().isEmpty())
            navigate({item->data(0,Qt::UserRole).toString(),item->data(0,Qt::UserRole+1).toString()});
    });
    auto activate=[this](QListWidgetItem *item){
        if(!item)return;
        const int i=item->data(Qt::UserRole).toInt();
        if(i>=0 && i<m_searchEntries.size())navigate(m_searchEntries[i].topic);
    };
    connect(m_results,&QListWidget::itemClicked,this,activate);
    connect(m_results,&QListWidget::itemActivated,this,activate);
    connect(m_globalSearch,&QLineEdit::returnPressed,this,[this]{
        m_openSearchResult=!m_globalSearch->text().trimmed().isEmpty();
        m_searchActivationSerial=m_navigationSerial;
        m_searchTimer.stop();updateSearch();
    });
    for(auto *button:findChildren<QPushButton*>())button->setAutoDefault(false);
    rebuildNavigation();updateHistoryButtons();
}
void HelpDialog::rebuildNavigation()
{
    m_navigationLanguage=LanguageCatalog::instance().language();
    m_searchTimer.stop();m_indexTimer.stop();m_indexReady=false;m_indexPosition=0;m_openSearchResult=false;
    m_searchEntries.clear();m_topicItems.clear();m_results->clear();
    const QSignalBlocker treeBlock(m_contents), searchBlock(m_globalSearch);
    m_contents->clear();m_globalSearch->clear();m_navigationStack->setCurrentWidget(m_contents);
    m_searchStatus->hide();
    m_back->setText(text("help.back"));m_forward->setText(text("help.forward"));m_home->setText(text("help.home"));
    m_findButton->setText(text("help.find_page"));m_online->setText(text("help.online"));m_close->setText(text("help.close"));
    m_globalSearch->setPlaceholderText(text("help.search_all"));m_globalSearch->setAccessibleName(text("help.search_all"));
    m_pageSearch->setPlaceholderText(text("help.search"));m_pageSearch->setAccessibleName(text("help.search"));
    m_contents->setAccessibleName(text("help.contents"));m_results->setAccessibleName(text("help.results"));
    findChild<QPushButton*>("helpFindPrevious")->setText(text("help.previous_match"));
    findChild<QPushButton*>("helpFindNext")->setText(text("help.next_match"));
    findChild<QPushButton*>("helpFindClose")->setAccessibleName(text("help.close_find"));
    QHash<QString,QTreeWidgetItem*> groups;
    for(const auto &page:helpNavigation(m_navigationLanguage)) {
        QTreeWidgetItem *parent=nullptr;QString groupKey;
        for(const auto &name:page.group){
            groupKey+='/'+name;
            auto *group=groups.value(groupKey);
            if(!group){group=parent?new QTreeWidgetItem(parent):new QTreeWidgetItem(m_contents);group->setText(0,name);group->setToolTip(0,name);groups.insert(groupKey,group);}
            parent=group;
        }
        auto *pageItem=parent?new QTreeWidgetItem(parent):new QTreeWidgetItem(m_contents);
        pageItem->setText(0,page.title);pageItem->setToolTip(0,page.title);
        pageItem->setData(0,Qt::UserRole,page.path);m_topicItems.insert(page.path+'#',pageItem);
        const auto sections=helpSections(page.path);
        QVector<QPair<int,QTreeWidgetItem*>> ancestors;
        for(int i=0;i<sections.size();++i){
            const auto &section=sections[i];QTreeWidgetItem *item=pageItem;
            if(i>0 || section.level>1){
                while(!ancestors.isEmpty() && ancestors.last().first>=section.level)ancestors.removeLast();
                item=new QTreeWidgetItem(ancestors.isEmpty()?pageItem:ancestors.last().second);
                item->setText(0,section.title);item->setToolTip(0,section.title);
                item->setData(0,Qt::UserRole,page.path);item->setData(0,Qt::UserRole+1,section.anchors.value(0));
                ancestors.append({section.level,item});
            }
            for(const auto &anchor:section.anchors)m_topicItems.insert(page.path+'#'+anchor,item);
            const auto trail=(page.group+QStringList{page.title}).join(QString::fromUtf8(" › "));
            m_searchEntries.append({{page.path,section.anchors.value(0)},section.title,trail,section.markdown,{}});
        }
    }
    for(int i=0;i<m_contents->topLevelItemCount();++i)m_contents->topLevelItem(i)->setExpanded(true);
}
void HelpDialog::syncNavigation()
{
    const QSignalBlocker block(m_contents);
    auto *item=m_topicItems.value(m_current.path+'#'+m_current.anchor);
    if(!item)item=m_topicItems.value(m_current.path+'#');
    m_contents->setCurrentItem(item);
    QStringList trail;
    for(auto *p=item;p;p=p->parent()){trail.prepend(p->text(0));if(p!=item)p->setExpanded(true);}
    if(item)m_contents->scrollToItem(item);
    if(trail.isEmpty()){
        const auto sections=helpSections(m_current.path);
        if(!sections.isEmpty())trail<<sections.first().title;
    }
    m_breadcrumb->setText(trail.join(QString::fromUtf8(" › ")));
    m_breadcrumb->setToolTip(m_breadcrumb->text());
}
void HelpDialog::buildSearchChunk()
{
    if(m_globalSearch->text().trimmed().isEmpty())return;
    QElapsedTimer elapsed;elapsed.start();
    do {
        if(m_indexPosition>=m_searchEntries.size())break;
        auto &entry=m_searchEntries[m_indexPosition++];
        QTextDocument document;
        if(entry.topic.path.endsWith(".md",Qt::CaseInsensitive))document.setMarkdown(entry.markdown);
        else document.setPlainText(entry.markdown);
        entry.body=document.toPlainText().simplified();
    }while(elapsed.elapsed()<10);
    m_indexReady=m_indexPosition>=m_searchEntries.size();
    m_searchStatus->setText(text("help.indexing").arg(m_indexPosition).arg(m_searchEntries.size()));
    if(m_indexReady)updateSearch();else m_indexTimer.start();
}
void HelpDialog::updateSearch()
{
    const auto query=m_globalSearch->text().simplified();
    if(query.isEmpty()){
        m_indexTimer.stop();m_results->clear();m_searchStatus->hide();
        m_navigationStack->setCurrentWidget(m_contents);syncNavigation();return;
    }
    m_navigationStack->setCurrentWidget(m_results);m_searchStatus->show();
    if(!m_indexReady){m_searchStatus->setText(text("help.indexing").arg(m_indexPosition).arg(m_searchEntries.size()));m_indexTimer.start();return;}
    const auto terms=query.split(' ',Qt::SkipEmptyParts);
    QVector<QPair<int,int>> matches;
    for(int i=0;i<m_searchEntries.size();++i){
        const auto &entry=m_searchEntries[i];
        const auto haystack=entry.title+' '+entry.body+' '+entry.topic.anchor;
        bool matchesAll=true;int score=entry.title.compare(query,Qt::CaseInsensitive)==0?300:0;
        for(const auto &term:terms){
            if(!haystack.contains(term,Qt::CaseInsensitive)){matchesAll=false;break;}
            if(entry.title.contains(term,Qt::CaseInsensitive))score+=100;
            if(entry.topic.anchor.contains(term,Qt::CaseInsensitive))score+=30;
        }
        if(matchesAll)matches.append({score,i});
    }
    std::stable_sort(matches.begin(),matches.end(),[](const auto &a,const auto &b){return a.first>b.first;});
    m_results->clear();
    for(const auto &match:matches){
        const auto &entry=m_searchEntries[match.second];
        int pos=entry.body.indexOf(terms.front(),0,Qt::CaseInsensitive);
        const int start=std::max(0,pos-25);
        auto snippet=entry.body.mid(start,110);
        if(start>0)snippet.prepend(QString::fromUtf8("…"));
        if(start+110<entry.body.size())snippet+=QString::fromUtf8("…");
        auto *item=new QListWidgetItem(entry.title+'\n'+snippet,m_results);
        item->setData(Qt::UserRole,match.second);item->setToolTip(entry.trail+'\n'+entry.title+'\n'+snippet);
    }
    m_searchStatus->setText(matches.isEmpty()?text("help.no_results"):text("help.result_count").arg(matches.size()));
    if(m_results->count())m_results->setCurrentRow(0);
    const bool openFirst=m_openSearchResult && m_searchActivationSerial==m_navigationSerial;
    m_openSearchResult=false;
    if(openFirst && !matches.isEmpty())navigate(m_searchEntries[matches.front().second].topic);
}
void HelpDialog::findInPage(bool backwards)
{
    m_findBar->show();
    const auto query=m_pageSearch->text();if(query.isEmpty()){m_pageSearch->setFocus();return;}
    if(m_lastFind!=query){
        m_lastFind=query;m_findMatches.clear();m_findPosition=-1;
        QTextCursor cursor(m_browser->document());
        while(!(cursor=m_browser->document()->find(query,cursor)).isNull())m_findMatches.append({cursor.selectionStart(),cursor.selectionEnd()});
    }
    if(m_findMatches.isEmpty()){m_findStatus->setText(text("help.no_match"));return;}
    if(m_findPosition<0)m_findPosition=backwards?m_findMatches.size()-1:0;
    else m_findPosition=(m_findPosition+(backwards?-1:1)+m_findMatches.size())%m_findMatches.size();
    QTextCursor cursor(m_browser->document());cursor.setPosition(m_findMatches[m_findPosition].first);
    cursor.setPosition(m_findMatches[m_findPosition].second,QTextCursor::KeepAnchor);
    m_browser->setTextCursor(cursor);m_browser->ensureCursorVisible();
    m_findStatus->setText(QString("%1 / %2").arg(m_findPosition+1).arg(m_findMatches.size()));
}
void HelpDialog::savePosition()
{
    if(m_position>=0 && !m_scrollPending)m_history[m_position].scroll=m_browser->verticalScrollBar()->value();
}
void HelpDialog::visitHistory(int position)
{
    if(position<0 || position>=m_history.size())return;
    savePosition();m_position=position;const auto entry=m_history[position];
    display(entry.topic,entry.scroll);updateHistoryButtons();
}
bool HelpDialog::display(const HelpTopic &topic, int scroll)
{
    const Page page=readPage(topic);if(!page.found)return false;
    if(m_navigationLanguage!=LanguageCatalog::instance().language())rebuildNavigation();
    const bool legal=topic.path=="LICENSE" || topic.path=="COPYRIGHT.md" || topic.path=="SOURCE_CODE.md" ||
        topic.path=="docs/LICENSE_GUIDE_ZH.md" || topic.path.startsWith("licenses/");
    setWindowTitle(text(legal?"ui.license":"help.title"));
    if(m_current.path!=topic.path){
        QUrl base;base.setScheme("qrc");base.setPath("/help/"+safePath(topic.path));
        m_browser->document()->setBaseUrl(base);m_browser->setHtml(page.html);refreshLinkColors();
    }
    m_lastFind.clear();m_findMatches.clear();m_findPosition=-1;m_findStatus->clear();
    m_current=topic;syncNavigation();
    m_scrollPending=true;const auto serial=++m_navigationSerial;
    m_browser->verticalScrollBar()->setValue(0);
    QTimer::singleShot(0,this,[this,topic,scroll,serial]{
        if(serial!=m_navigationSerial)return;
        if(scroll>=0)m_browser->verticalScrollBar()->setValue(scroll);
        else if(!topic.anchor.isEmpty())m_browser->scrollToAnchor(topic.anchor);
        m_scrollPending=false;
    });
    return true;
}
bool HelpDialog::navigate(const HelpTopic &topic)
{
    const auto resolved=localTopic(topic);
    savePosition();
    if(!display(resolved)){openOnline(this,topic);return false;}
    if(m_position>=0 && m_history[m_position].topic.path==resolved.path && m_history[m_position].topic.anchor==resolved.anchor)return true;
    m_history.resize(m_position+1);m_history.append({resolved,-1});m_position=m_history.size()-1;
    updateHistoryButtons();return true;
}
void HelpDialog::refreshLinkColors()
{
    // Markdown serializes link colors into HTML. Reapply the current theme to
    // actual links; named section anchors must not change paragraph styling.
    auto *document = m_browser->document();
    QVector<QPair<int, int>> links;
    for (auto block = document->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
        {
            const auto fragment = it.fragment();
            if (fragment.isValid() && !fragment.charFormat().anchorHref().isEmpty())
                links.append({fragment.position(), fragment.length()});
        }
    QTextCharFormat format;
    format.setForeground(themeColor("accent"));
    format.setFontUnderline(true);
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (const auto &link : links)
    {
        cursor.setPosition(link.first);
        cursor.setPosition(link.first + link.second, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(format);
    }
    cursor.endEditBlock();
    // Reflow the complete page after batched format changes. Otherwise Qt's
    // incremental layout can retain zero-height blocks between distant links.
    document->markContentsDirty(0, document->characterCount());
}
void HelpDialog::updateHistoryButtons()
{
    m_back->setEnabled(m_position > 0);
    m_forward->setEnabled(m_position + 1 < m_history.size());
}
void HelpDialog::followLink(const QUrl &link)
{
    const QUrl resolved = m_browser->document()->baseUrl().resolved(link);
    if (resolved.scheme() == "https" || resolved.scheme() == "http")
    {
        // Map current-repository document links back into the embedded manual.
        // Historical versions and other repositories keep their original URL.
        const QRegularExpression own("^/ChenCLian/Aster/(blob|tree)/(main|master|release-stable|release-beta)/(.+)$");
        const auto match=own.match(resolved.path());
        if ((resolved.host()=="github.com" || resolved.host()=="gitee.com") &&
            resolved.userInfo().isEmpty() && !resolved.hasQuery() && match.hasMatch())
            navigate({match.captured(3)+(match.captured(1)=="tree"?"/":""),resolved.fragment()});
        else if(resolved.userInfo().isEmpty())
            QDesktopServices::openUrl(resolved);
        return;
    }
    if (resolved.scheme() != "qrc" || !resolved.path().startsWith("/help/")) return;
    navigate({resolved.path().mid(6), resolved.fragment()});
}
void openHelp(QWidget *parent, const HelpTopic &topic)
{
    if (!localHelpAvailable(topic)) { openOnline(parent, topic); return; }
    QWidget *owner = parent ? parent->window() : nullptr;
    auto *dialog = owner ? owner->findChild<HelpDialog *>("helpDialog", Qt::FindDirectChildrenOnly) : nullptr;
    if (!dialog) dialog = new HelpDialog(owner);
    dialog->navigate(topic);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
} // namespace aster::ui
