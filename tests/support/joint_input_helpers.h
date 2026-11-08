// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_TEST_JOINT_INPUT_HELPERS_H
#define ASTER_TEST_JOINT_INPUT_HELPERS_H
#include <QtTest>
#include <QTableWidget>
#include <QTableView>
#include <QTreeWidget>
#include <QPushButton>
#include "ui/panels/joint_input_dialog.h"
#include "ui/panels/pairing_panel.h"
namespace aster::test {
inline ui::JointInputDialog* openMember(ui::PairingDialog* dialog,int row,int column,bool keyboard=false) {
    auto* table=dialog->findChild<QTableWidget*>("pairTable");if(!table || !table->item(row,column))return nullptr;
    table->scrollToItem(table->item(row,column));
    if(keyboard){table->setCurrentCell(row,column);table->setFocus();QTest::keyClick(table,Qt::Key_Return);}
    else QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualItemRect(table->item(row,column)).center());
    QCoreApplication::processEvents();return dialog->findChild<ui::JointInputDialog*>();
}
inline bool selectMember(ui::JointInputDialog* picker,NodeId node,int row,bool keyboard=false) {
    if(!picker)return false;
    auto* tree=picker->findChild<QTreeWidget*>("jointSourceTree");auto* table=picker->findChild<QTableView*>("jointMemberTable");
    if(!tree||!table)return false;
    QTreeWidgetItem* item=nullptr;for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toUuid()==node){item=*it;break;}
    if(!item || !(item->flags()&Qt::ItemIsSelectable))return false;
    tree->expandAll();tree->setCurrentItem(item);tree->scrollToItem(item);QCoreApplication::processEvents();
    if(row<0 || row>=table->model()->rowCount())return false;
    const auto index=table->model()->index(row,0);table->setCurrentIndex(index);table->scrollTo(index);table->setFocus();QCoreApplication::processEvents();
    if(keyboard)QTest::keyClick(table,Qt::Key_Return);
    else picker->findChild<QPushButton*>("jointInputConfirm")->click();
    const bool accepted=picker->result()==QDialog::Accepted;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);return accepted;
}
}
#endif
