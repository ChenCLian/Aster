// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/common/numeric_editor.h"
#include "ui/settings/theme_manager.h"
#include "ui/panels/parameter_editor.h"
#include "ui/common/presentation.h"
#include "ui/common/parameter_icons.h"
#include <QHBoxLayout>
#include <QToolButton>
#include "ui/help/help_dialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <limits>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFileDialog>
#include <QPointer>
#include <algorithm>
namespace aster::ui
{
namespace
{
constexpr int parameterRightMargin = 5;
class ParameterScrollArea : public QScrollArea
{
  public:
    explicit ParameterScrollArea(QHBoxLayout *heading) : m_heading(heading)
    {
        heading->parentWidget()->installEventFilter(this);
    }

  protected:
    bool viewportEvent(QEvent *event) override
    {
        const bool handled = QScrollArea::viewportEvent(event);
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move)
            alignHeading();
        return handled;
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        const bool handled = QScrollArea::eventFilter(object, event);
        // QScrollArea already watches its content widget. Its size and the
        // header's allocation can settle in either order during a relayout.
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
            event->type() == QEvent::Show)
            alignHeading();
        return handled;
    }

  private:
    void alignHeading()
    {
        if (!m_heading || !widget()) return;
        auto *header = m_heading->parentWidget();
        if (!header || !header->parentWidget() || window() != header->window()) return;
        const QWidget *panel = header->parentWidget();
        const int left = widget()->mapTo(panel, QPoint()).x() - header->x();
        const int right = header->width() - left - widget()->width() + parameterRightMargin;
        const QMargins margins(std::max(0, left), 0, std::max(0, right), 0);
        if (m_heading->contentsMargins() != margins)
            m_heading->setContentsMargins(margins);
    }

    QPointer<QHBoxLayout> m_heading;
};
class CompactDoubleSpinBox : public DecimalEditor
{
  protected:
    QString textFromValue(double value) const override
    {
        // The text must round-trip to the same double, including very small
        // nonzero cutoffs and parameters loaded from a saved workflow.
        return locale().toString(value, 'g', QLocale::FloatingPointShortest);
    }
    QValidator::State validate(QString &input, int &position) const override
    {
        // QDoubleSpinBox's default validator rejects exponent notation even
        // though textFromValue uses it. Rejecting just "e-" can turn 1e-15 into 115.
        QDoubleValidator validator(minimum(), maximum(), decimals());
        validator.setLocale(locale());
        validator.setNotation(QDoubleValidator::ScientificNotation);
        return validator.validate(input, position);
    }
    double valueFromText(const QString &input) const override
    {
        bool ok = false;
        const double parsed = locale().toDouble(input, &ok);
        return ok ? parsed : value();
    }
};
} // namespace
ParameterEditor::ParameterEditor(QWidget *p) : QWidget(p)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    m_title = new QLabel(text("ui.select_a_task"));
    m_title->setObjectName("SectionTitle");
    m_title->setWordWrap(true);
    auto *header = new QWidget(this);
    header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *heading = new QHBoxLayout(header);
    heading->setContentsMargins(0, 0, parameterRightMargin, 0);
    heading->addWidget(m_title, 1);
    m_help = new QToolButton;
    m_help->setObjectName("methodHelp");
    m_help->setText("?");
    m_help->setAutoRaise(true);
    m_help->hide();
    heading->addWidget(m_help, 0, Qt::AlignTop);
    layout->addWidget(header);
    connect(m_help, &QToolButton::clicked, m_help, [this] {
        openHelp(window(), methodHelpTopic(m_method));
    });
    m_description = new QLabel;
    m_description->setWordWrap(true);
    m_description->setObjectName("Muted");
    m_description->hide();
    layout->addWidget(m_description);
    auto *scroll = new ParameterScrollArea(heading);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    content->setObjectName("Surface");
    content->setMinimumWidth(0);
    m_form = new QVBoxLayout(content);
    m_form->setAlignment(Qt::AlignTop);
    m_form->setContentsMargins(0, 0, parameterRightMargin, 0);
    m_form->setSpacing(12);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    m_error = new QLabel;
    m_error->setObjectName("Error");
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);
    m_commit.setSingleShot(true); m_commit.setInterval(260);
    connect(&m_commit, &QTimer::timeout, this, &ParameterEditor::applyRequested);
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this] {
        for(auto* label:findChildren<QLabel*>()) {
            const auto name=label->property("parameterIconName").toString();
            if(!name.isEmpty())label->setPixmap(parameterIcon(name).pixmap(20,20));
        }
    });
}
ParameterEditor::~ParameterEditor()
{
    // A focused spin box can emit editingFinished while QWidget deletes its children.
    // Disconnect before m_fields and the other editor members are destroyed.
    for (auto *field : m_fields)
        disconnect(field, nullptr, this, nullptr);
}
void ParameterEditor::showNode(const NodeDefinition *n, const TaskRegistry &r)
{
    m_fields.clear();
    m_rows.clear();m_specs.clear();
    while(auto* item=m_form->takeAt(0)) {delete item->widget();delete item;}
    m_id = n ? n->id : NodeId{};
    m_method = n ? n->method : QString{};
    m_dirty = false;
    m_error->hide();
    m_commit.stop();
    m_title->setText(text("ui.select_a_task"));
    m_description->clear();
    m_description->hide();
    m_help->hide();
    m_help->setToolTip({});
    m_help->setAccessibleName({});
    m_help->setAccessibleDescription({});
    const auto *registration = n ? r.find(n->method) : nullptr;
    if (!registration)
        return;
    const auto &d = registration->descriptor;
    m_specs=d.parameters;
    m_title->setText(methodLabel(d));
    if (n->input)
    {
        m_description->setText(d.pairedInput ? text("ui.paired_input_ports") : text("ui.input_port") + portLabel(n->input->output));
        m_description->show();
    }
    const auto help=methodHelp(d.key);
    if (!help.isEmpty())
    {
        m_help->setAccessibleName(methodLabel(d));
        m_help->setAccessibleDescription(help);
        m_help->show();
    }
    if (n->method == "file_source")
        return;
    if (d.parameters.isEmpty())
    {
        auto *label = new QLabel(text("ui.this_task_has_no_adjustable_parameters"));
        label->setWordWrap(true);
        m_form->addWidget(label);
    }
    QString lastGroup;
    for (const auto &p : d.parameters)
    {
        QWidget *field = nullptr;
        const auto value =
            n->parameters.contains(p.key) ? n->parameters.value(p.key) : p.defaultValue;
        auto dirty = [this]
        {
            m_dirty = true;
            m_commit.start();
            emit edited();
            updateVisibility();
        };
        if (p.kind == ParameterKind::Boolean)
        {
            auto *w = new QCheckBox;
            w->setChecked(value.toBool());
            connect(w, &QCheckBox::toggled, this, dirty);
            field = w;
        }
        else if (p.kind == ParameterKind::Integer)
        {
            auto *w = new IntegerEditor;
            w->setRange(int(std::max(-2147483647., p.minimum)),
                        int(std::min(2147483647., p.maximum)));
            w->setValue(value.toInt());
            connect(w, qOverload<int>(&QSpinBox::valueChanged), this, dirty);
            field = w;
        }
        else if (p.kind == ParameterKind::Number)
        {
            auto *w = new CompactDoubleSpinBox;
            w->setDecimals(std::numeric_limits<double>::max_exponent10 +
                           std::numeric_limits<double>::digits10);
            w->setRange(p.minimum, p.maximum);
            w->setSingleStep(.05);
            w->setValue(value.toDouble());
            connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged), this, dirty);
            field = w;
        }
        else if (!p.choices.isEmpty())
        {
            auto *w = new ChoiceEditor;
            for (const auto &choice : p.choices)
                w->addItem(choiceLabel(p.key, choice), choice);
            w->setCurrentIndex(w->findData(value.toString()));
            w->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            w->setMinimumContentsLength(6);
            connect(w, &QComboBox::currentTextChanged, this, dirty);
            field = w;
        }
        else
        {
            auto *w = new QLineEdit(value.toString());
            connect(w, &QLineEdit::textChanged, this, dirty);
            field = w;
        }
        field->setObjectName("parameter_" + p.key);
        field->setMinimumWidth(0);
        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        const auto help=parameterHelp(p,d.key);
        field->setAccessibleDescription(help);
        if(!p.group.isEmpty() && p.group!=lastGroup) {
            auto* group=new QLabel(translatedLabel("parametergroup."+p.group,p.group));
            group->setObjectName("ParameterGroup");
            group->setWordWrap(true);
            group->setMinimumWidth(0);
            group->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            if (!lastGroup.isEmpty()) m_form->addSpacing(8);
            m_form->addWidget(group);lastGroup=p.group;
        }
        auto* row=new QWidget;row->setObjectName("parameterRow_"+p.key);
        row->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        auto* box=new QVBoxLayout(row);box->setContentsMargins(0,0,0,0);box->setSpacing(5);
        auto* heading=new QHBoxLayout;heading->setContentsMargins(0,0,0,0);heading->setSpacing(5);
        const auto icon=parameterIcon(p.icon);
        if(!icon.isNull()){auto* symbol=new QLabel;symbol->setProperty("parameterIconName",p.icon);symbol->setPixmap(icon.pixmap(20,20));symbol->setFixedSize(20,20);heading->addWidget(symbol);}
        auto *label = new QLabel(parameterLabel(p,d.key));
        label->setObjectName("parameterLabel_"+p.key);label->setWordWrap(true);
        label->setMinimumWidth(0);label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        label->setBuddy(field);heading->addWidget(label,1);
        if(p.kind==ParameterKind::Boolean) {
            field->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
            heading->addWidget(field);
        }
        if(!help.isEmpty()) {
            auto* hint=new QToolButton;hint->setText("?");hint->setObjectName("ParameterHelp");
            hint->setAccessibleName(parameterLabel(p,d.key));hint->setAccessibleDescription(help);
            hint->setAutoRaise(true);heading->addWidget(hint);
            connect(hint, &QToolButton::clicked, hint, [hint, method=d.key, key=p.key] {
                openHelp(hint->window(), parameterHelpTopic(method,key));
            });
        }
        if(p.kind==ParameterKind::Boolean){box->addLayout(heading);}
        else {
            box->addLayout(heading);
            if (!p.fileFilter.isEmpty()) {
                auto* fileRow = new QHBoxLayout;
                fileRow->setContentsMargins(0, 0, 0, 0);
                fileRow->addWidget(field, 1);
                auto* browse = new QToolButton(row);
                browse->setObjectName("parameterBrowse_" + p.key);
                browse->setText(QString::fromUtf8("…"));
                browse->setToolTip(text("ui.browse_data_table"));
                browse->setAccessibleName(text("ui.browse_data_table") + ": " + parameterLabel(p,d.key));
                fileRow->addWidget(browse);
                box->addLayout(fileRow);
                QPointer<QLineEdit> edit = qobject_cast<QLineEdit*>(field);
                const auto filter = parameterLabel(p,d.key) + " (" + p.fileFilter + ")";
                connect(browse, &QToolButton::clicked, this, [this, edit, filter] {
                    if (!edit) return;
                    const auto path = QFileDialog::getOpenFileName(
                        this, text("ui.browse_data_table"), edit->text(), filter);
                    // Cancellation preserves the old value. setText uses the same
                    // debounced parameter signal as typing or clearing the path.
                    if (edit && !path.isEmpty()) edit->setText(path);
                });
            } else {
                box->addWidget(field);
            }
        }
        m_form->addWidget(row);m_rows.insert(p.key,row);
        m_fields.insert(p.key, field);
    }
    if (d.key == "mtm" || d.key == "evolution_fft" || d.key == "evolution_mtm")
        if (auto *limit = qobject_cast<QDoubleSpinBox *>(m_fields.value("frequency_max")))
        {
            const QString legacyKey = d.key == "mtm" ? "fmax_ratio" : "evo_frequency_ratio";
            const auto clearLegacyLimit = [this, legacyKey] {
                if (auto *legacy = qobject_cast<QDoubleSpinBox *>(m_fields.value(legacyKey)))
                    legacy->setValue(1);
                updateVisibility();
            };
            connect(limit, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    clearLegacyLimit);
            // An old ratio also has frequency_max=0. Explicitly confirming zero
            // must select Auto even though QDoubleSpinBox emits no valueChanged.
            // Mere focus changes, or cancelled/invalid input, retain the old ratio.
            if (auto *input = limit->findChild<QLineEdit *>())
                connect(input, &QLineEdit::textEdited, limit, [limit](const QString &value) {
                    bool ok = false;
                    const double number = limit->locale().toDouble(value, &ok);
                    limit->setProperty("frequencyZeroEdited", ok && number == 0);
                });
            connect(limit, &QDoubleSpinBox::editingFinished, this, [limit, clearLegacyLimit] {
                const bool explicitZero = limit->property("frequencyZeroEdited").toBool();
                limit->setProperty("frequencyZeroEdited", false);
                if (explicitZero && limit->value() == 0) clearLegacyLimit();
            });
        }
    updateVisibility();
}
void ParameterEditor::updateVisibility()
{
    const auto values=parameters();
    // Keep the editable text numeric. setSpecialValueText() reformats even a
    // partially typed decimal when visibility is refreshed by valueChanged.
    if (auto *label = findChild<QLabel *>("parameterLabel_frequency_max"))
        label->setText(text("parameter.frequency_max") + " · " +
                       text(values.value("frequency_max").toDouble() == 0 &&
                            values.value(m_method == "mtm" ? "fmax_ratio" : "evo_frequency_ratio").toDouble(1) < 1
                                ? "ui.frequency_legacy" : "ui.frequency_zero_hint"));
    for(const auto& p:m_specs) {
        bool visible = !p.hideWhenDefault || values.value(p.key) != p.defaultValue;
        for(auto it=p.visibleWhen.begin();it!=p.visibleWhen.end();++it)
            if(values.value(it.key())!=it.value())visible=false;
        if(auto* row=m_rows.value(p.key)){row->setVisible(visible);row->setEnabled(visible);}
    }
}
QJsonObject ParameterEditor::parameters() const
{
    QJsonObject out;
    for (auto it = m_fields.begin(); it != m_fields.end(); ++it)
    {
        auto *w = it.value();
        if (auto *p = qobject_cast<QCheckBox *>(w))
            out[it.key()] = p->isChecked();
        else if (auto *p = qobject_cast<QSpinBox *>(w))
            out[it.key()] = p->value();
        else if (auto *p = qobject_cast<QDoubleSpinBox *>(w))
            out[it.key()] = p->value();
        else if (auto *p = qobject_cast<QComboBox *>(w))
            out[it.key()] = p->currentData().toString();
        else if (auto *p = qobject_cast<QLineEdit *>(w))
            out[it.key()] = p->text();
    }
    return out;
}
void ParameterEditor::markClean()
{
    m_dirty = false;
    m_commit.stop();
    m_error->hide();
}
void ParameterEditor::setError(const QString &e)
{
    m_error->setText(e);
    m_error->setVisible(!e.isEmpty());
}
} // namespace aster::ui
