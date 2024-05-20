#include <iostream>
#include "logViewWindow.h"
#include <QTreeView>
#include <QTableView>
#include <QCloseEvent>
#include <QEvent>
#include <QString>
#include <Qt>

void logViewWindow::show()
{
    loggerWindow->show();
}

logViewWindow::logViewWindow(const char *fileName):outFile(fileName){
    loggerWindow = new QMainWindow();
    loggerWindow->setObjectName("View_Logger_Window");
    ui = new Ui::LogViewContentWindow();
    ui->setupUi(loggerWindow);
    loggerWindow->setWindowFlags(Qt::WindowStaysOnTopHint);
    connect(ui->logViewsBtn, SIGNAL(clicked()), this, SLOT(logBtnClicked()));
    loggerWindow->show();
    //ui->show();
}

void logViewWindow::setCurrentView(QAbstractItemView* v){
    currentView = v;
    QString name, type; 
    if(currentView)
    {
        name = currentView->objectName();
        if(qobject_cast<QTreeView*>(currentView))
            type = "Tree View";
        else if(qobject_cast<QTableView*>(currentView))
            type = "Table View";
        else
            type = "Unknown";
    }
    if(type != "" && type != "Unknown")
        ui->logViewsBtn->setEnabled(true);
    else
        ui->logViewsBtn->setEnabled(false);
    ui->nameTextBox->setText(name);
    ui->typeTextBox->setText(type);
}  
          
logViewWindow::~logViewWindow()
{
    outFile.close();
}

void logViewWindow::logTreeData(QTreeView *v, QModelIndex idx, int currIdent)
{
    QAbstractItemModel *m = v->model();
    int rows = m->rowCount(idx);
    int cols = m->columnCount(idx);
    for(int i=0; i<rows; ++i)
    {
        if(v->isRowHidden(i, idx)) continue;
        QString rowData;
        QModelIndex c0Idx =  m->index(i, 0 , idx);

        //bool isSelected = false;
        for(int j = 0; j < cols; ++j)
        {
            QModelIndex cIdx = m->index(i, j , idx);
            if(j>0) rowData += "\t";
            if(cIdx.isValid())
                rowData += cIdx.data().toString();
        }
        int childCount = 0;
        if(!c0Idx.isValid()) break;
        char symbol = ' ';
        if(c0Idx.isValid())
        {
            childCount = m->rowCount(c0Idx);
            if(childCount)
            {
                if(v->isExpanded(c0Idx))
                    symbol = '-';
                else 
                    symbol = '+';
            }
        }
        for(int k=0; k<currIdent; ++k) outFile<<"  ";
        outFile<<symbol;
        outFile<<qPrintable(rowData)<<"\n";
        if(childCount && symbol =='-')
            logTreeData(v, c0Idx, currIdent+1);
    }
}

void logViewWindow::logTreeViewContents()
{
    QTreeView * treeV = qobject_cast<QTreeView*>(currentView);
    logTreeData(treeV, QModelIndex() /*treeV->model()->index(0,0)*/, 0);
}

void logViewWindow::logTableViewContents()
{
    QTableView *tableV = qobject_cast<QTableView*>(currentView);
    QAbstractItemModel *m = tableV->model();
    if(!m) return;
    int rows = m->rowCount();
    int cols = m->columnCount();    
    //Add header columns as well
    for(int i=0; i<rows; ++i)
    {
        if(tableV->isRowHidden(i)) continue;
        bool colPrinted = false;
        for(int j=0; j<cols; ++j)
        {
            if(tableV->isColumnHidden(j)) continue;
            if(colPrinted) outFile<<" | ";
            QString data = m->index(i, j, QModelIndex()).data().toString();
            outFile<<qPrintable(data);
            colPrinted = true;
        }
        outFile<<"\n";
    }

}

void logViewWindow::logBtnClicked()
{
    if(currentView)
    {
        ++checkpoint;
        outFile<<"=================Checkpoint = "<<checkpoint<<"=================\n";
        outFile<<"View Name: "<<qPrintable(currentView->objectName())<<"\n\n";
        if(qobject_cast<QTreeView*>(currentView))
            logTreeViewContents();
        else if(qobject_cast<QTableView*>(currentView))
            logTableViewContents();
        outFile<<"\n========================================\n";
    }

}

