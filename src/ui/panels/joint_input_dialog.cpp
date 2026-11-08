// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/panels/joint_input_dialog.h"
#include "ui/common/presentation.h"
#include "ui/common/popup_placement.h"
#include "ui/settings/theme_manager.h"
#include <QAbstractTableModel>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStyle>
#include <QResizeEvent>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace aster::ui {
namespace {
QString shortId(QUuid id) { return id.toString(QUuid::WithoutBraces).left(8); }
QString fileLabel(const SourceItem& s) {
    return s.provenance.contains("path") ? QFileInfo(s.label).fileName() : s.label;
}
QVariant storeRef(const BatchSeriesRef& r) {
    return QVariantMap{{"node",r.node},{"dataset",r.dataset},{"port",r.port}};
}
BatchSeriesRef loadRef(const QVariant& value) {
    const auto map=value.toMap();
    return {map.value("dataset").toUuid(),map.value("port").toString(),map.value("node").toUuid()};
}
void openBounded(QDialog* dialog, QSize preferred) {
    auto* owner=dialog->parentWidget();
    const QRect anchor(owner ? owner->window()->mapToGlobal(QPoint()) : QPoint(), owner ? owner->window()->size() : preferred);
    dialog->ensurePolished();
    const auto bounds=popupBounds(owner,anchor);
    dialog->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    dialog->setMinimumSize(0,0); dialog->setMaximumSize(bounds.size());
    const auto size=preferred.boundedTo(bounds.size());
    const QRect rect(QPoint(bounds.left()+(bounds.width()-size.width())/2,
                            bounds.top()+(bounds.height()-size.height())/2),size);
    dialog->setGeometry(rect); dialog->show();
    const auto client=dialog->geometry(),frame=dialog->frameGeometry();
    const auto inner=bounds.adjusted(client.left()-frame.left(),client.top()-frame.top(),
                                    client.right()-frame.right(),client.bottom()-frame.bottom());
    const auto finalSize=preferred.boundedTo(inner.size());
    dialog->setMaximumSize(inner.size());
    dialog->setGeometry(QRect(QPoint(inner.left()+(inner.width()-finalSize.width())/2,
                                    inner.top()+(inner.height()-finalSize.height())/2),finalSize));
    dialog->raise(); dialog->activateWindow();
}
QLabel* plainLabel(const QString& textValue, const char* name=nullptr) {
    auto* label=new QLabel(textValue); label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true); if(name) label->setObjectName(name); return label;
}
}
QString jointOriginLabel(const WorkflowDocument& doc, NodeId id) {
    const auto& nodes=doc.model().definition().nodes;
    for(qsizetype i=0;i<nodes.size();++i) if(nodes[i].id==id)
        return QString("#%1 · %2").arg(i+1).arg(methodLabel(doc.model().registry().find(nodes[i].method)->descriptor));
    return text("ui.joint_missing_node").arg(shortId(id));
}
class JointInputCatalog {
public:
    NodeId parent;
    QVector<NodeId> order;
    QHash<NodeId,NodeId> parents,roots;
    QHash<NodeId,QString> labels,search;
    QSet<NodeId> eligible;
    QHash<NodeId,QVector<OutputSpec>> ports;
    QHash<NodeId,QVector<BatchDataset>> datasets;
    QHash<NodeId,QVector<QString>> dataNames;
    QHash<NodeId,QHash<QUuid,int>> indices;
    explicit JointInputCatalog(const WorkflowDocument& doc,NodeId joint) {
        const auto* target=doc.node(joint); if(!target || !target->input) return;
        parent=target->input->parent;
        const auto origins=linkedInputOrigins(doc.model(),joint);
        eligible=QSet<NodeId>(origins.begin(),origins.end());
        const auto kind=doc.model().registry().find(target->method)->descriptor.inputKind;
        QHash<NodeId,QVector<NodeId>> children;
        int ordinal=0;
        for(const auto& node:doc.model().definition().nodes) {
            labels[node.id]=QString("#%1 · %2").arg(++ordinal).arg(methodLabel(doc.model().registry().find(node.method)->descriptor));
            // Node search follows the displayed number/name. Random UUIDs
            // would make a one-digit query match almost every node.
            search[node.id]=labels[node.id];
            if(node.input) { parents[node.id]=node.input->parent;children[node.input->parent].push_back(node.id); }
            else {
                order.push_back(node.id);roots[node.id]=node.id;
                datasets[node.id]=doc.datasets(node.id);
                const auto& data=datasets[node.id];
                QHash<QString,int> shortCounts,fullCounts;
                for(const auto& dataset:data){++shortCounts[fileLabel(dataset.source)];++fullCounts[dataset.source.label];}
                for(int i=0;i<data.size();++i){
                    indices[node.id][data[i].id]=i;
                    auto name=fileLabel(data[i].source);
                    if(shortCounts.value(name)>1)name=data[i].source.label;
                    if(fullCounts.value(data[i].source.label)>1)name+=" ["+shortId(data[i].id)+"]";
                    dataNames[node.id].append(name);
                }
            }
            if(eligible.contains(node.id)) {
                const auto& descriptor=doc.model().registry().find(node.method)->descriptor;
                for(auto port:descriptor.outputs) if(port.kind==kind) {
                    port.label=portLabel(descriptor,port.key);ports[node.id].push_back(port);
                }
            }
        }
        for(qsizetype i=0;i<order.size();++i)for(auto child:children.value(order[i])) {
            roots[child]=roots.value(order[i]);order.push_back(child);
        }
    }
    NodeId origin(const BatchSeriesRef& ref) const { return ref.node.isNull()?parent:ref.node; }
    bool available(const BatchSeriesRef& ref) const {
        const auto node=origin(ref);if(!eligible.contains(node) || !indices.value(roots.value(node)).contains(ref.dataset))return false;
        for(const auto& port:ports.value(node)) if(port.key==ref.port)return true;
        return false;
    }
    QString description(const BatchSeriesRef& ref,bool tooltip=false) const {
        if(ref.dataset.isNull())return text("ui.joint_choose_member");
        const auto node=origin(ref);const auto root=roots.value(node);
        const auto nodeName=labels.value(node,text("ui.joint_missing_node").arg(shortId(node)));
        const int index=indices.value(root).value(ref.dataset,-1);
        // value() returns a shared container. Const at() avoids detaching and
        // copying the whole dataset/name list for every displayed pair member.
        const auto name=index<0?text("ui.joint_missing_data").arg(shortId(ref.dataset)):
            (tooltip?datasets.value(root).at(index).source.label:dataNames.value(root).at(index));
        QString output=ref.port;
        for(const auto& port:ports.value(node))if(port.key==ref.port)output=port.label;
        return (available(ref)?QString():text("ui.joint_unavailable")+" · ")+name+" · "+output+"\n"+nodeName+(tooltip?"\n"+text("ui.joint_data_identity").arg(shortId(ref.dataset)):QString());
    }
};
bool jointMemberAvailable(const WorkflowDocument& doc,NodeId joint,const BatchSeriesRef& ref) {
    const auto* target=doc.node(joint);if(!target || !target->input)return false;
    const auto origin=ref.node.isNull()?target->input->parent:ref.node;
    const auto* node=doc.node(origin);if(!node)return false;
    const auto kind=doc.model().registry().find(target->method)->descriptor.inputKind;
    bool portValid=false;for(const auto& port:doc.model().registry().find(node->method)->descriptor.outputs)
        if(port.key==ref.port&&port.kind==kind)portValid=true;
    if(!portValid)return false;
    for(auto* ancestor=node;ancestor;ancestor=ancestor->input?doc.node(ancestor->input->parent):nullptr)
        if(doc.model().registry().find(ancestor->method)->descriptor.pairedInput)return false;
    for(const auto& dataset:doc.datasets(origin))if(dataset.id==ref.dataset)return true;
    return false;
}
QString jointMemberLabel(const WorkflowDocument& doc,NodeId joint,const BatchSeriesRef& ref) {
    const auto* target=doc.node(joint);if(!target || !target->input)return text("ui.joint_unavailable");
    const auto origin=ref.node.isNull()?target->input->parent:ref.node;
    QString label=text("ui.joint_missing_data").arg(shortId(ref.dataset)),output=ref.port;
    for(const auto& dataset:doc.datasets(origin))if(dataset.id==ref.dataset){label=dataset.source.label;break;}
    if(const auto* node=doc.node(origin))output=portLabel(doc.model().registry().find(node->method)->descriptor,ref.port);
    return label+" · "+output+"\n"+jointOriginLabel(doc,origin);
}
namespace {
// Only the selected node's dataset/output product is exposed; no widget or item
// is allocated per candidate and no all-nodes × all-datasets list is built.
class MembersModel final : public QAbstractTableModel {
public:
    MembersModel(std::shared_ptr<const JointInputCatalog> c,QObject* p):QAbstractTableModel(p),catalog(std::move(c)){}
    std::shared_ptr<const JointInputCatalog> catalog;
    NodeId node;
    void select(NodeId id){beginResetModel();node=id;endResetModel();}
    int rowCount(const QModelIndex& p={}) const override {
        if(p.isValid())return 0;
        return catalog->datasets.value(catalog->roots.value(node)).size()*catalog->ports.value(node).size();
    }
    int columnCount(const QModelIndex& p={}) const override{return p.isValid()?0:2;}
    BatchSeriesRef ref(int row) const {
        const auto& ports=catalog->ports[node];const auto& data=catalog->datasets[catalog->roots.value(node)];
        if(row<0 || ports.isEmpty() || row/ports.size()>=data.size())return {};
        return {data[row/ports.size()].id,ports[row%ports.size()].key,node==catalog->parent?NodeId{}:node};
    }
    QVariant data(const QModelIndex& index,int role=Qt::DisplayRole) const override {
        if(!index.isValid() || index.row()>=rowCount())return {};
        const auto& ports=catalog->ports[node];const auto& data=catalog->datasets[catalog->roots.value(node)];
        const auto& item=data[index.row()/ports.size()];const auto& port=ports[index.row()%ports.size()];
        if(role==Qt::DisplayRole)return index.column()==0?catalog->dataNames.value(catalog->roots.value(node)).at(index.row()/ports.size()):port.label;
        if(role==Qt::ToolTipRole)return item.source.label+"\n"+port.label+"\n"+text("ui.joint_data_identity").arg(shortId(item.id));
        if(role==Qt::UserRole)return catalog->dataNames.value(catalog->roots.value(node)).at(index.row()/ports.size())+" "+port.label+" "+port.key;
        return {};
    }
    QVariant headerData(int section,Qt::Orientation orientation,int role) const override {
        if(orientation==Qt::Horizontal && role==Qt::DisplayRole)return text(section==0?"ui.joint_dataset":"ui.joint_output");
        return QAbstractTableModel::headerData(section,orientation,role);
    }
};
class MembersTable final : public QTableView {
public:
    using QTableView::QTableView;
    std::function<void()> choose;
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter) {
            if(choose)choose();
            event->accept();return;
        }
        QTableView::keyPressEvent(event);
    }
};
class PairTable final : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
    std::function<void(int,int)> choose;
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if((event->key()==Qt::Key_Return || event->key()==Qt::Key_Enter || event->key()==Qt::Key_Space) && currentColumn()>0 && currentRow()>=0) {
            if(choose)choose(currentRow(),currentColumn());
            event->accept();return;
        }
        QTableWidget::keyPressEvent(event);
    }
};
class MemberDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index) const override {
        auto opt=option;initStyleOption(&opt,index);const auto lines=index.data(Qt::DisplayRole).toString().split('\n');opt.text.clear();
        const auto* style=opt.widget?opt.widget->style():QApplication::style();
        painter->save();
        style->drawControl(QStyle::CE_ItemViewItem,&opt,painter,opt.widget);
        painter->restore();
        painter->save();painter->setFont(option.font);const bool selected=opt.state&QStyle::State_Selected;
        painter->setPen(opt.palette.color(selected?QPalette::HighlightedText:QPalette::Text));
        auto r=option.rect.adjusted(9,6,-22,-6);const int h=option.fontMetrics.height();
        painter->drawText(QRect(r.topLeft(),QSize(r.width(),h+3)),Qt::AlignVCenter,
            option.fontMetrics.elidedText(lines.value(0),Qt::ElideMiddle,r.width()));
        if(!selected)painter->setPen(themeColor("muted"));
        painter->drawText(r.adjusted(0,h+4,0,0),Qt::AlignVCenter,
            option.fontMetrics.elidedText(lines.value(1),Qt::ElideRight,r.width()));
        painter->drawText(option.rect.adjusted(0,0,-7,0),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("›"));
        painter->restore();
    }
};
}
struct JointInputDialog::Impl {
    QPointer<WorkflowDocument> doc;NodeId joint;BatchSeriesRef previous;quint64 revision;bool committed=false;
    std::shared_ptr<const JointInputCatalog> catalog;
    QTreeWidget* tree;MembersTable* table;MembersModel* model;QSortFilterProxyModel* proxy;
    QLineEdit* nodeSearch;QLineEdit* dataSearch;QLabel* status=nullptr;QLabel* help=nullptr;QPushButton* confirm;QSplitter* split=nullptr;
    BatchSeriesRef selected() const {return model->ref(proxy->mapToSource(table->currentIndex()).row());}
};
JointInputDialog::JointInputDialog(WorkflowDocument* doc,NodeId joint,BatchSeriesRef current,QWidget* parent):QDialog(parent),d(new Impl) {
    d->doc=doc;d->joint=joint;d->previous=current;d->revision=doc->revision();d->catalog=std::make_shared<JointInputCatalog>(*doc,joint);
    setObjectName("jointInputDialog");setAttribute(Qt::WA_DeleteOnClose);setWindowModality(Qt::WindowModal);
    setWindowTitle(text("ui.joint_choose_member"));auto* layout=new QVBoxLayout(this);layout->setContentsMargins(16,16,16,12);layout->setSpacing(10);
    d->help=plainLabel(text("ui.joint_choose_help"));layout->addWidget(d->help);setToolTip(text("ui.joint_choose_help"));
    auto* split=new QSplitter;d->split=split;split->setObjectName("jointInputSplitter");layout->addWidget(split,1);
    auto* left=new QWidget;auto* l=new QVBoxLayout(left);l->setContentsMargins(0,0,6,0);l->addWidget(plainLabel(text("ui.joint_source_node"),"SectionTitle"));
    d->nodeSearch=new QLineEdit;d->nodeSearch->setObjectName("jointNodeSearch");d->nodeSearch->setPlaceholderText(text("ui.joint_search_nodes"));l->addWidget(d->nodeSearch);
    d->tree=new QTreeWidget;d->tree->setObjectName("jointSourceTree");d->tree->setHeaderHidden(true);d->tree->setUniformRowHeights(true);d->tree->setMinimumSize(0,0);l->addWidget(d->tree,1);split->addWidget(left);
    QSet<NodeId> visible=d->catalog->eligible;
    for(auto id:d->catalog->order)if(d->catalog->eligible.contains(id))for(auto next=d->catalog->parents.value(id);!next.isNull()&&!visible.contains(next);next=d->catalog->parents.value(next))visible.insert(next);
    QHash<NodeId,QTreeWidgetItem*> items;
    for(auto id:d->catalog->order)if(visible.contains(id)) {
        auto* item=new QTreeWidgetItem;item->setText(0,d->catalog->labels.value(id));item->setData(0,Qt::UserRole,id);item->setToolTip(0,d->catalog->search.value(id));
        if(!d->catalog->eligible.contains(id))item->setFlags(item->flags()&~Qt::ItemIsSelectable);
        const auto p=d->catalog->parents.value(id);if(items.contains(p))items[p]->addChild(item);else d->tree->addTopLevelItem(item);items[id]=item;
    }
    d->tree->expandToDepth(1);
    auto* right=new QWidget;auto* r=new QVBoxLayout(right);r->setContentsMargins(6,0,0,0);r->addWidget(plainLabel(text("ui.joint_data_output"),"SectionTitle"));
    d->dataSearch=new QLineEdit;d->dataSearch->setObjectName("jointDataSearch");d->dataSearch->setPlaceholderText(text("ui.joint_search_data"));r->addWidget(d->dataSearch);
    d->table=new MembersTable;d->table->setObjectName("jointMemberTable");d->model=new MembersModel(d->catalog,this);d->proxy=new QSortFilterProxyModel(this);d->proxy->setSourceModel(d->model);d->proxy->setFilterRole(Qt::UserRole);d->proxy->setFilterKeyColumn(0);d->proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);d->table->setModel(d->proxy);
    d->table->setSelectionBehavior(QAbstractItemView::SelectRows);d->table->setSelectionMode(QAbstractItemView::SingleSelection);d->table->setEditTriggers(QAbstractItemView::NoEditTriggers);d->table->verticalHeader()->hide();d->table->verticalHeader()->setDefaultSectionSize(fontMetrics().height()+18);d->table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);d->table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Interactive);d->table->setColumnWidth(1,160);d->table->setMinimumSize(0,0);r->addWidget(d->table,1);split->addWidget(right);split->setSizes({310,530});split->setChildrenCollapsible(false);
    d->status=plainLabel({},"jointInputStatus");layout->addWidget(d->status);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);d->confirm=buttons->button(QDialogButtonBox::Ok);d->confirm->setObjectName("jointInputConfirm");d->confirm->setText(text("ui.joint_use_input"));d->confirm->setEnabled(false);layout->addWidget(buttons);
    const auto update=[this]{const auto ref=d->selected();const bool valid=d->catalog->available(ref);d->confirm->setEnabled(valid);d->status->setText(valid?d->catalog->description(ref):text("ui.joint_candidate_count").arg(d->proxy->rowCount()));d->confirm->setToolTip(d->status->text());};
    connect(d->tree,&QTreeWidget::currentItemChanged,this,[this,update](QTreeWidgetItem* item){
        d->dataSearch->clear();const auto id=item?item->data(0,Qt::UserRole).toUuid():NodeId{};d->model->select(d->catalog->eligible.contains(id)?id:NodeId{});d->table->setCurrentIndex({});update();
    });
    connect(d->table->selectionModel(),&QItemSelectionModel::currentChanged,this,[update]{update();});
    connect(d->dataSearch,&QLineEdit::textChanged,this,[this,update](const QString& text){d->proxy->setFilterFixedString(text);if(d->table->currentIndex().isValid()&&!d->table->selectionModel()->isSelected(d->table->currentIndex()))d->table->setCurrentIndex({});update();});
    connect(d->nodeSearch,&QLineEdit::textChanged,this,[this,items](const QString& value){
        QSet<NodeId> show;for(auto id:d->catalog->order)if(items.contains(id)&&d->catalog->search.value(id).contains(value,Qt::CaseInsensitive))
            for(auto next=id;!next.isNull()&&!show.contains(next);next=d->catalog->parents.value(next))show.insert(next);
        for(auto it=items.cbegin();it!=items.cend();++it){it.value()->setHidden(!show.contains(it.key()));if(!value.isEmpty()&&show.contains(it.key()))it.value()->setExpanded(true);}
        if(d->tree->currentItem()&&d->tree->currentItem()->isHidden())d->tree->setCurrentItem(nullptr);
    });
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    const auto choose=[this]{
        if(d->committed)return;
        if(!d->doc || d->doc->revision()!=d->revision || d->doc->editingLocked()){reject();return;}
        auto ref=d->selected();if(!d->catalog->available(ref))return;
        if(d->catalog->origin(ref)==d->catalog->origin(d->previous)&&ref.dataset==d->previous.dataset&&ref.port==d->previous.port)ref=d->previous;
        d->committed=true;emit chosen(ref.node,ref.dataset,ref.port);accept();
    };
    connect(buttons,&QDialogButtonBox::accepted,this,choose);connect(d->table,&QTableView::doubleClicked,this,[choose]{choose();});d->table->choose=choose;
    connect(doc,&WorkflowDocument::changed,this,&QDialog::reject);connect(doc,&WorkflowDocument::sourcesChanged,this,&QDialog::reject);connect(doc,&QObject::destroyed,this,&QDialog::reject);
    if(auto* outer=qobject_cast<QDialog*>(parent))connect(outer,&QDialog::finished,this,&QDialog::reject);
    const auto initial=d->catalog->available(current)?d->catalog->origin(current):d->catalog->parent;
    if(items.contains(initial)){d->tree->setCurrentItem(items[initial]);d->tree->scrollToItem(items[initial]);}
    if(d->catalog->available(current))for(int i=0;i<d->model->rowCount();++i){const auto ref=d->model->ref(i);if(ref.dataset==current.dataset&&ref.port==current.port){d->table->setCurrentIndex(d->proxy->mapFromSource(d->model->index(i,0)));d->table->scrollTo(d->table->currentIndex());break;}}
    update();
}
JointInputDialog::~JointInputDialog()=default;
void JointInputDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if(d->help)d->help->setVisible(height()>=520);
    if(d->status)d->status->setVisible(height()>=520);
    if(d->split){const auto orientation=width()<720?Qt::Vertical:Qt::Horizontal;
        if(d->split->orientation()!=orientation){d->split->setOrientation(orientation);d->split->setSizes({250,400});}}
}
void JointInputDialog::openWithinOwner(){if(!d->doc || d->doc->revision()!=d->revision || d->doc->editingLocked()){reject();return;}openBounded(this,{920,590});}
struct PairingDialog::Impl {
    QPointer<WorkflowDocument> doc;NodeId joint;quint64 revision;bool committing=false,committed=false;
    std::shared_ptr<const JointInputCatalog> catalog;PairTable* table=nullptr;QLabel* status;QPushButton* confirm;QPushButton* add;
    QLabel* help=nullptr;QGridLayout* actions=nullptr;QPushButton* remove=nullptr;QPushButton* a=nullptr;QPushButton* b=nullptr;
    bool compact=false;QString statusFull,statusShort;
    void showStatus(){status->setText(compact?statusShort:statusFull);status->setToolTip(statusFull);}
    std::shared_ptr<const PreparedWorkflowEdit> prepared;
    QPointer<JointInputDialog> picker;
};
PairingDialog::PairingDialog(WorkflowDocument* doc,NodeId joint,QWidget* parent):QDialog(parent),d(new Impl) {
    d->doc=doc;d->joint=joint;d->revision=doc->revision();d->catalog=std::make_shared<JointInputCatalog>(*doc,joint);
    setObjectName("pairingDialog");setAttribute(Qt::WA_DeleteOnClose);setWindowModality(Qt::WindowModal);setWindowTitle(text("ui.manage_pairs"));
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(16,16,16,12);layout->setSpacing(10);
    layout->addWidget(plainLabel(jointOriginLabel(*doc,joint),"SectionTitle"));
    d->help=plainLabel(text("ui.pair_help"));d->help->setToolTip(text("ui.pair_help"));layout->addWidget(d->help);
    d->table=new PairTable(0,3);d->table->setObjectName("pairTable");d->table->setHorizontalHeaderLabels({text("ui.pair_name"),"A","B"});d->table->setSelectionBehavior(QAbstractItemView::SelectRows);d->table->setSelectionMode(QAbstractItemView::SingleSelection);d->table->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);d->table->setMinimumSize(0,0);d->table->setWordWrap(false);
    d->table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Interactive);d->table->setColumnWidth(0,145);d->table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);d->table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);d->table->horizontalHeader()->setMinimumSectionSize(90);d->table->verticalHeader()->setDefaultSectionSize(fontMetrics().height()*2+20);d->table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    d->table->setItemDelegateForColumn(1,new MemberDelegate(d->table));d->table->setItemDelegateForColumn(2,new MemberDelegate(d->table));layout->addWidget(d->table,1);
    auto* actions=new QGridLayout;d->actions=actions;d->add=new QPushButton(text("ui.add_pair"));d->add->setObjectName("addPair");actions->addWidget(d->add,0,0);
    auto* remove=new QPushButton(text("ui.remove_pair"));d->remove=remove;remove->setObjectName("removePair");actions->addWidget(remove,0,1);actions->setColumnStretch(2,1);
    auto* a=new QPushButton(text("ui.joint_choose_a"));auto* b=new QPushButton(text("ui.joint_choose_b"));d->a=a;d->b=b;a->setObjectName("choosePairA");b->setObjectName("choosePairB");actions->addWidget(a,0,3);actions->addWidget(b,0,4);layout->addLayout(actions);
    d->status=plainLabel({},"pairingStatus");layout->addWidget(d->status);auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);d->confirm=buttons->button(QDialogButtonBox::Ok);d->confirm->setObjectName("pairingConfirm");layout->addWidget(buttons);
    const auto update=[this]{
        if(!d->doc || d->doc->revision()!=d->revision){reject();return;}
        d->prepared.reset();QVector<BatchPair> pairs;bool complete=true;
        for(int row=0;row<d->table->rowCount();++row){auto* name=d->table->item(row,0);const auto a=loadRef(d->table->item(row,1)->data(Qt::UserRole)),b=loadRef(d->table->item(row,2)->data(Qt::UserRole));complete&=d->catalog->available(a)&&d->catalog->available(b);pairs.push_back({name->data(Qt::UserRole).toUuid(),name->text().trimmed(),a,b});}
        d->add->setEnabled(pairs.size()<BatchPairLimit);
        if(!complete){d->confirm->setEnabled(false);d->statusFull=d->statusShort=text("ui.joint_repair_pairs");d->showStatus();return;}
        auto draft=d->doc->beginEdit();draft.state.grouping[d->joint]=pairs;const auto prepared=d->doc->prepareEdit(std::move(draft));d->prepared=prepared.edit;d->confirm->setEnabled(prepared.validation.ok()&&!d->doc->editingLocked());
        d->statusFull=!prepared.validation.ok()?prepared.validation.message():pairs.isEmpty()?text("ui.joint_empty_pairs"):text("ui.joint_pairs_ready").arg(pairs.size());
        d->statusShort=prepared.validation.ok()&&!pairs.isEmpty()?text("ui.joint_pairs_ready_short").arg(pairs.size()):d->statusFull;d->showStatus();
    };
    const auto setMember=[this](int row,int col,const BatchSeriesRef& ref){auto* item=d->table->item(row,col);item->setData(Qt::UserRole,storeRef(ref));item->setText(d->catalog->description(ref));item->setToolTip(d->catalog->description(ref,true));};
    const auto addRow=[this,setMember](const BatchPair& pair){
        QSignalBlocker blocker(d->table);const int row=d->table->rowCount();d->table->insertRow(row);
        auto* name=new QTableWidgetItem(pair.id.isNull()?text("ui.pair_number").arg(row+1):pair.label);name->setData(Qt::UserRole,pair.id.isNull()?QUuid::createUuid():pair.id);d->table->setItem(row,0,name);
        for(int col=1;col<3;++col){auto* member=new QTableWidgetItem;member->setFlags(member->flags()&~Qt::ItemIsEditable);d->table->setItem(row,col,member);setMember(row,col,col==1?pair.a:pair.b);}
    };
    for(const auto& pair:doc->grouping().value(joint))addRow(pair);
    connect(d->table,&QTableWidget::itemChanged,this,[update]{update();});
    d->table->choose=[this,setMember,update](int row,int col){
        if(col<1||row<0||row>=d->table->rowCount()||!d->doc||d->doc->editingLocked())return;
        if(d->picker && d->picker->isVisible()){d->picker->raise();return;}
        const auto id=d->table->item(row,0)->data(Qt::UserRole).toUuid();
        auto* picker=new JointInputDialog(d->doc,d->joint,loadRef(d->table->item(row,col)->data(Qt::UserRole)),this);d->picker=picker;
        connect(picker,&JointInputDialog::chosen,this,[this,id,col,setMember,update](QUuid node,QUuid dataset,QString port){
            for(int row=0;row<d->table->rowCount();++row)if(d->table->item(row,0)->data(Qt::UserRole).toUuid()==id){QSignalBlocker blocker(d->table);setMember(row,col,{dataset,port,node});break;}
            update();
        });picker->openWithinOwner();
    };
    connect(d->table,&QTableWidget::cellClicked,this,[this](int row,int col){if(col>0)d->table->choose(row,col);});
    connect(a,&QPushButton::clicked,this,[this]{d->table->choose(d->table->currentRow(),1);});connect(b,&QPushButton::clicked,this,[this]{d->table->choose(d->table->currentRow(),2);});
    const auto selection=[this,a,b,remove]{const bool chosen=d->table->currentRow()>=0;a->setEnabled(chosen);b->setEnabled(chosen);remove->setEnabled(chosen);};
    connect(d->table,&QTableWidget::itemSelectionChanged,this,selection);
    connect(d->add,&QPushButton::clicked,this,[this,addRow,update]{if(d->table->rowCount()>=BatchPairLimit)return;addRow({});d->table->setCurrentCell(d->table->rowCount()-1,0);d->table->scrollToBottom();update();});
    connect(remove,&QPushButton::clicked,this,[this,selection,update]{d->table->removeRow(d->table->currentRow());selection();update();});
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{
        if(d->committed)return;
        if(!d->doc || d->doc->revision()!=d->revision || !d->prepared){reject();return;}
        d->committing=true;const auto result=d->doc->commitEdit(d->prepared);d->committing=false;
        if(!result.ok()){d->statusFull=d->statusShort=result.error;d->showStatus();d->confirm->setEnabled(false);return;}d->committed=true;accept();
    });
    const auto invalidate=[this]{if(!d->committing)reject();};
    connect(doc,&WorkflowDocument::changed,this,invalidate);connect(doc,&WorkflowDocument::sourcesChanged,this,invalidate);connect(doc,&QObject::destroyed,this,&QDialog::reject);
    selection();update();
}
PairingDialog::~PairingDialog()=default;
void PairingDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if(!d->actions)return;
    const bool compact=width()<720;
    if(compact!=d->compact){
        d->compact=compact;
        for(auto* button:{d->add,d->remove,d->a,d->b})d->actions->removeWidget(button);
        d->actions->setColumnStretch(2,compact?0:1);
        d->actions->addWidget(d->add,0,0);d->actions->addWidget(d->remove,0,1);
        d->actions->addWidget(d->a,compact?1:0,compact?0:3);d->actions->addWidget(d->b,compact?1:0,compact?1:4);
        d->help->setText(text(compact?"ui.pair_help_short":"ui.pair_help"));d->showStatus();
    }
    d->table->setColumnWidth(0,compact?std::max(80,width()/5):145);
}
void PairingDialog::openWithinOwner(){if(!d->doc || d->doc->revision()!=d->revision || d->doc->editingLocked()){reject();return;}openBounded(this,{1040,620});}
} // namespace aster::ui
