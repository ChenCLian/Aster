// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/help/help_dialog.h"
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
        document.setDefaultFont(QApplication::font());
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
    QStringList pending;
    QHash<QString, int> occurrences;
    const QRegularExpression explicitId("<a\\s+(?:id|name)=\"([^\"]+)\"\\s*></a>");
    const QRegularExpression heading("^ {0,3}#{1,6}\\s+(.+?)\\s*#*\\s*$");
    const QRegularExpression punctuation("[^\\p{L}\\p{N}_\\-\\s]");
    QString fence;
    for (const auto &line : markdown.split('\n'))
    {
        const auto trimmed = line.trimmed();
        if (trimmed.startsWith("```") || trimmed.startsWith("~~~"))
        {
            const QString marker = trimmed.left(3);
            if (fence.isEmpty()) fence = marker;
            else if (fence == marker) fence.clear();
            continue;
        }
        if (!fence.isEmpty()) continue;
        const auto id = explicitId.match(line);
        if (id.hasMatch()) pending << id.captured(1);
        const auto match = heading.match(line);
        if (!match.hasMatch()) continue;
        QString slug = match.captured(1).toLower();
        slug.remove(punctuation);
        slug.replace(QRegularExpression("\\s"), "-");
        const int occurrence = occurrences[slug]++;
        if (occurrence) slug += '-' + QString::number(occurrence);
        pending << slug;
        pending.removeDuplicates();
        headings << pending;
        page.anchors << pending;
        pending.clear();
    }
    QTextDocument document;
    document.setDefaultFont(QApplication::font());
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
QUrl onlineHelpUrl(const HelpTopic &topic)
{
    const QString path = safePath(topic.path);
    if (path.isEmpty()) return {};
    QUrl url("https://github.com");
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
    resize(900, 680);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    auto *toolbar = new QHBoxLayout;
    m_back = new QPushButton(text("help.back"));
    m_back->setObjectName("helpBack");
    m_forward = new QPushButton(text("help.forward"));
    m_forward->setObjectName("helpForward");
    auto *home = new QPushButton(text("help.home"));
    toolbar->addWidget(m_back);
    toolbar->addWidget(m_forward);
    toolbar->addWidget(home);
    auto *search = new QLineEdit;
    search->setObjectName("helpSearch");
    search->setPlaceholderText(text("help.search"));
    toolbar->addWidget(search, 1);
    layout->addLayout(toolbar);
    m_browser = new QTextBrowser;
    m_browser->setObjectName("helpBrowser");
    m_browser->setOpenLinks(false);
    m_browser->setOpenExternalLinks(false);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &HelpDialog::refreshLinkColors);
    layout->addWidget(m_browser, 1);
    auto *footer = new QHBoxLayout;
    auto *online = new QPushButton(text("help.online"));
    auto *close = new QPushButton(text("help.close"));
    footer->addWidget(online);
    footer->addStretch();
    footer->addWidget(close);
    layout->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(home, &QPushButton::clicked, this, [this] { navigate(userGuideTopic()); });
    connect(online, &QPushButton::clicked, this, [this] { openOnline(this, m_current); });
    connect(m_back, &QPushButton::clicked, this, [this] {
        if (m_position > 0) { display(m_history[--m_position]); updateHistoryButtons(); }
    });
    connect(m_forward, &QPushButton::clicked, this, [this] {
        if (m_position + 1 < m_history.size()) { display(m_history[++m_position]); updateHistoryButtons(); }
    });
    connect(m_browser, &QTextBrowser::anchorClicked, this, &HelpDialog::followLink);
    auto *find = new QShortcut(QKeySequence::Find, this);
    connect(find, &QShortcut::activated, search, [search] { search->setFocus(); search->selectAll(); });
    connect(search, &QLineEdit::returnPressed, this, [this, search] {
        if (search->text().isEmpty()) return;
        if (!m_browser->find(search->text()))
        {
            m_browser->moveCursor(QTextCursor::Start);
            m_browser->find(search->text());
        }
    });
    // Enter in the search field must not activate a navigation or Close button.
    for (auto *button : findChildren<QPushButton *>()) button->setAutoDefault(false);
    updateHistoryButtons();
}
bool HelpDialog::display(const HelpTopic &topic)
{
    const Page page = readPage(topic);
    if (!page.found) return false;
    const bool legal = topic.path == "LICENSE" || topic.path == "COPYRIGHT.md" ||
                       topic.path == "SOURCE_CODE.md" || topic.path == "docs/LICENSE_GUIDE_ZH.md" ||
                       topic.path.startsWith("licenses/");
    setWindowTitle(text(legal ? "ui.license" : "help.title"));
    // Chapter changes within one reference should scroll, not parse and lay out
    // the entire document again. Language changes have different document paths.
    if (m_current.path != topic.path)
    {
        QUrl base;
        base.setScheme("qrc");
        base.setPath("/help/" + safePath(topic.path));
        m_browser->document()->setBaseUrl(base);
        m_browser->setHtml(page.html);
        refreshLinkColors();
    }
    m_current = topic;
    m_browser->verticalScrollBar()->setValue(0);
    const QString anchor = topic.anchor;
    if (!anchor.isEmpty())
        QTimer::singleShot(0, this, [this, topic, anchor] {
            if (m_current.path == topic.path && m_current.anchor == anchor)
                m_browser->scrollToAnchor(anchor);
        });
    return true;
}
bool HelpDialog::navigate(const HelpTopic &topic)
{
    const auto resolved = localTopic(topic);
    if (!display(resolved)) { openOnline(this, topic); return false; }
    if (m_position >= 0 && m_history[m_position].path == resolved.path &&
        m_history[m_position].anchor == resolved.anchor) return true;
    m_history.resize(m_position + 1);
    m_history.append(resolved);
    m_position = m_history.size() - 1;
    updateHistoryButtons();
    return true;
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
        const QString prefix = "/ChenCLian/Aster/blob/main/";
        const QString directoryPrefix = "/ChenCLian/Aster/tree/main/";
        if (resolved.host() == "github.com" && resolved.path().startsWith(prefix))
            navigate({resolved.path().mid(prefix.size()), resolved.fragment()});
        else if (resolved.host() == "github.com" && resolved.path().startsWith(directoryPrefix))
            navigate({resolved.path().mid(directoryPrefix.size()) + '/', resolved.fragment()});
        else
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
