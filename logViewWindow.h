#ifndef __LOG_VIEW_WINDOW__H
#define __LOG_VIEW_WINDOW__H

#include "ui_log_view_contents.h"
#include<fstream>
#include<QTreeView>
#include<QAbstractItemModel>

class logViewWindow:public QObject{    

    Q_OBJECT

    Ui::LogViewContentWindow *ui = nullptr;
    QMainWindow *loggerWindow = nullptr;
    QAbstractItemView *currentView = nullptr;
    std::ofstream outFile;
    int checkpoint = 0;
    void logTreeData(QTreeView *v, QModelIndex idx = QModelIndex(), int currIdent = 0);
    void logTreeViewContents();
    void logTableViewContents();
    public:
        void log(){}
        logViewWindow(const char *fileName);
        void setCurrentView(QAbstractItemView* v);
        void show();
        ~logViewWindow();
       public slots:
          void logBtnClicked(); 
};


#endif
