#include "ui/panels/file_options_editor.h"
#include "ui/common/presentation.h"
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QThread>
#include <QScrollArea>
#include <QDebug>
#include <QElapsedTimer>
#include <QTableWidget>
#include <QHeaderView>
#include <QLocale>
#include <cmath>
#include "ui/settings/theme_manager.h"
namespace aster::ui
{
FileOptionsEditor::FileOptionsEditor(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *section = new QLabel(text("ui.input_settings"));
    section->setObjectName("SectionTitle");
    layout->addWidget(section);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 5, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_delimiter = new QComboBox;
    m_delimiter->setObjectName("fileDelimiter");
    for (auto key : {"auto", "comma", "tab", "whitespace"})
        m_delimiter->addItem(choiceLabel("delimiter", key), key);
    m_header = new QComboBox;
    m_header->setObjectName("fileHeader");
    m_header->addItem(text("ui.detect_header"), -1);
    m_header->addItem(text("ui.header_present"), 1);
    m_header->addItem(text("ui.no_header"), 0);
    m_x = new QComboBox;
    m_x->setObjectName("fileXColumn");
    m_y = new QComboBox;
    m_y->setObjectName("fileYColumn");
    for (auto *box : {m_delimiter, m_header, m_x, m_y})
    {
        box->setMinimumWidth(0);
        box->setMinimumContentsLength(5);
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    m_xUnit = new QLineEdit;
    m_xUnit->setObjectName("fileXUnit");
    m_xUnit->setPlaceholderText("ka / yr / s");
    m_yUnit = new QLineEdit;
    m_yUnit->setObjectName("fileYUnit");
    m_skip = new QCheckBox(text("ui.skip_invalid_rows"));
    m_skip->setObjectName("fileSkipInvalidRows");
    m_skip->setToolTip(text("parameterhelp.skip_invalid_rows"));
    form->addRow(text("ui.delimiter"), m_delimiter);
    form->addRow(text("ui.header"), m_header);
    form->addRow("X", m_x);
    form->addRow("Y", m_y);
    form->addRow(text("ui.x_unit"), m_xUnit);
    form->addRow(text("ui.y_unit"), m_yUnit);
    contentLayout->addLayout(form);
    contentLayout->addWidget(m_skip);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    m_info->setObjectName("fileProbeInfo");
    m_info->setTextFormat(Qt::PlainText);
    contentLayout->addWidget(m_info);
    auto *previewTitle = new QLabel(text("ui.input_preview"));
    previewTitle->setObjectName("Muted");
    contentLayout->addWidget(previewTitle);
    m_preview = new QTableWidget(0, 3);
    m_preview->setObjectName("fileRowPreview");
    m_preview->setHorizontalHeaderLabels({text("ui.preview_row"), "X", "Y"});
    m_preview->verticalHeader()->hide();
    m_preview->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_preview->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_preview->verticalHeader()->setDefaultSectionSize(26);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_preview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentLayout->addWidget(m_preview);
    m_previewHint = new QLabel;
    m_previewHint->setObjectName("filePreviewHint");
    m_previewHint->setWordWrap(true);
    m_previewHint->setTextFormat(Qt::PlainText);
    contentLayout->addWidget(m_previewHint);
    contentLayout->addStretch();
    m_apply = new QPushButton(text("ui.apply_to_this_file"));
    m_apply->setObjectName("applyFileOptions");
    m_apply->setToolTip(text("ui.apply_column_unit_edits_before_running"));
    m_applySelection = new QPushButton(text("ui.apply_to_selected_files"));
    m_applySelection->setToolTip(text("ui.apply_column_indices_each_file_is_validated_when_read"));
    layout->addWidget(m_apply);
    layout->addWidget(m_applySelection);
    connect(m_apply, &QPushButton::clicked, this,
            [this] { emit applyRequested(options(), false); });
    connect(m_applySelection, &QPushButton::clicked, this,
            [this] { emit applyRequested(options(), true); });
    for (auto *box : {m_delimiter, m_header})
        connect(box, qOverload<int>(&QComboBox::activated), this,
                [this]
                {
                    applyFileOptions(options(), m_options);
                    requestProbe();
                });
    const auto checkColumns = [this]
    {
        const bool valid = m_x->currentIndex() >= 0 && m_y->currentIndex() >= 0;
        if (valid && m_header->currentData().toInt() == -1 && m_probe.ok())
        {
            // Auto-header detection depends on the selected numeric columns.
            // Re-probe so column names and the row table agree with the full reader.
            applyFileOptions(options(), m_options);
            requestProbe();
            return;
        }
        m_apply->setEnabled(valid);
        m_applySelection->setEnabled(valid);
        refreshPreview();
        emit readinessChanged(ready());
        notifyPreviewInput();
    };
    connect(m_x, qOverload<int>(&QComboBox::currentIndexChanged), this, checkColumns);
    connect(m_y, qOverload<int>(&QComboBox::currentIndexChanged), this, checkColumns);
    connect(m_skip, &QCheckBox::toggled, this, [this] { refreshPreview(); notifyPreviewInput(); });
    connect(m_xUnit, &QLineEdit::textChanged, this, &FileOptionsEditor::notifyPreviewInput);
    connect(m_yUnit, &QLineEdit::textChanged, this, &FileOptionsEditor::notifyPreviewInput);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, &FileOptionsEditor::refreshPreview);
    setFile({}, {});
}
FileOptionsEditor::~FileOptionsEditor()
{
    if (m_worker)
    {
        m_worker->wait();
        delete m_worker;
    }
}
bool FileOptionsEditor::ready() const
{
    return !m_path.isEmpty() && m_apply->isEnabled();
}
QJsonObject FileOptionsEditor::options() const
{
    auto o = m_options;
    o.delimiter = m_delimiter->currentData().toString();
    o.autoHeader = m_header->currentData().toInt() == -1;
    o.header = m_header->currentData().toInt() == 1;
    if (m_x->currentIndex() >= 0)
        o.xColumn = m_x->currentData().toInt();
    if (m_y->currentIndex() >= 0)
        o.yColumn = m_y->currentData().toInt();
    o.xUnit = m_xUnit->text().trimmed();
    o.yUnit = m_yUnit->text().trimmed();
    o.skipInvalidRows = m_skip->isChecked();
    return fileOptionsToJson(o);
}
void FileOptionsEditor::notifyPreviewInput()
{
    emit previewInputChanged(m_path, options(), ready());
}
void FileOptionsEditor::setFile(QString path, QJsonObject value)
{
    // Unrelated graph/status refreshes must not discard pending user edits or
    // restart a completed preview for the same persisted configuration.
    if (m_initialized && path == m_path && value == m_loadedOptions)
        return;
    m_initialized = true;
    m_loadedOptions = value;
    m_path = path;
    QSignalBlocker xUnitBlock(m_xUnit), yUnitBlock(m_yUnit), skipBlock(m_skip);
    m_options = SeriesFileOptions{};
    applyFileOptions(value, m_options);
    m_delimiter->setCurrentIndex(m_delimiter->findData(m_options.delimiter));
    m_header->setCurrentIndex(
        m_header->findData(m_options.autoHeader ? -1 : int(m_options.header)));
    m_xUnit->setText(m_options.xUnit);
    m_yUnit->setText(m_options.yUnit);
    m_skip->setChecked(m_options.skipInvalidRows);
    setEnabled(!path.isEmpty());
    requestProbe();
}
void FileOptionsEditor::requestProbe()
{
    ++m_generation;
    m_apply->setEnabled(false);
    m_applySelection->setEnabled(false);
    emit readinessChanged(false);
    {
        QSignalBlocker xBlock(m_x), yBlock(m_y);
        m_x->clear();
        m_y->clear();
    }
    m_pending = !m_path.isEmpty();
    m_probe = {};
    refreshPreview();
    m_info->setText(m_pending ? text("ui.reading_columns")
                              : text("ui.select_a_file_to_configure_x_y"));
    notifyPreviewInput();
    if (m_pending && !m_worker)
        startProbe();
}
void FileOptionsEditor::startProbe()
{
    m_pending = false;
    const auto generation = m_generation;
    const auto path = m_path;
    const auto options = m_options;
    m_worker = QThread::create(
        [this, generation, path, options]
        {
            QElapsedTimer timer;
            timer.start();
            const auto probe = probeSeriesFile(path, options);
            const auto readMs = timer.elapsed();
            QMetaObject::invokeMethod(
                this,
                [this, generation, options, probe, timer, readMs]
                {
                    if (generation != m_generation)
                        return;
                    qInfo() << "[Aster import] preview_read_ms=" << readMs
                            << "gui_delivery_wait_ms=" << timer.elapsed() - readMs
                            << "columns=" << probe.columns.size() << "ok=" << probe.ok();
                    QElapsedTimer uiTimer;
                    uiTimer.start();
                    if (!probe.ok())
                    {
                        m_info->setText(probe.error);
                        return;
                    }
                    QSignalBlocker xBlock(m_x), yBlock(m_y);
                    m_probe = probe;
                    for (int i = 0; i < probe.columns.size(); ++i)
                    {
                        const auto label = QString::number(i + 1) + " · " + probe.columns[i];
                        m_x->addItem(label, i);
                        m_y->addItem(label, i);
                    }
                    m_x->setCurrentIndex(m_x->findData(options.xColumn));
                    m_y->setCurrentIndex(m_y->findData(options.yColumn));
                    m_info->setText(
                        text("ui.columns") + QString::number(probe.columns.size()) +
                        (probe.header ? text("ui.header_e65875") : text("ui.no_header_67bb2e")));
                    const bool valid = m_x->currentIndex() >= 0 && m_y->currentIndex() >= 0;
                    m_apply->setEnabled(valid);
                    m_applySelection->setEnabled(valid);
                    if (!valid)
                        m_info->setText(text("ui.configured_column_is_missing_select_x_y"));
                    refreshPreview();
                    qInfo() << "[Aster import] column_controls_ms=" << uiTimer.elapsed();
                    emit readinessChanged(ready());
                    notifyPreviewInput();
                },
                Qt::QueuedConnection);
        });
    connect(m_worker, &QThread::finished, this,
            [this]
            {
                auto *w = m_worker;
                m_worker = nullptr;
                w->deleteLater();
                if (m_pending)
                    startProbe();
            });
    m_worker->start();
}
void FileOptionsEditor::refreshPreview()
{
    m_preview->setRowCount(m_probe.preview.size());
    QLocale locale = QLocale::c();
    locale.setNumberOptions(QLocale::RejectGroupSeparator);
    bool invalid = false;
    for (int row = 0; row < m_probe.preview.size(); ++row)
    {
        const auto &sample = m_probe.preview[row];
        m_preview->setItem(row, 0, new QTableWidgetItem(QString::number(sample.line)));
        for (int col = 0; col < 2; ++col)
        {
            auto *selector = col == 0 ? m_x : m_y;
            const int source = selector->currentIndex() < 0 ? -1 : selector->currentData().toInt();
            const auto value = source >= 0 ? sample.fields.value(source) : QString();
            bool numeric = false;
            const double number = locale.toDouble(value, &numeric);
            const bool valid = sample.wellFormed && numeric && std::isfinite(number);
            const auto display = source < 0 || source >= sample.fields.size()
                ? text("ui.preview_missing")
                : value.isEmpty() ? text("ui.preview_empty") : value.left(160);
            auto *item = new QTableWidgetItem(display);
            item->setToolTip(value.left(240));
            if (!valid)
            {
                item->setForeground(themeColor("error"));
                item->setBackground(themeColor("hover"));
                invalid = true;
            }
            m_preview->setItem(row, col + 1, item);
        }
    }
    int previewHeight = m_preview->horizontalHeader()->sizeHint().height() +
                        2 * m_preview->frameWidth();
    for (int row = 0; row < m_preview->rowCount(); ++row)
        previewHeight += m_preview->rowHeight(row);
    m_preview->setFixedHeight(previewHeight);
    m_previewHint->setText(invalid ? text(m_skip->isChecked() ? "ui.preview_skipped" : "ui.preview_invalid")
                                   : text("ui.preview_not_full_validation"));
    auto palette = m_previewHint->palette();
    palette.setColor(QPalette::WindowText, themeColor(invalid ? "warning" : "muted"));
    m_previewHint->setPalette(palette);
}
} // namespace aster::ui
