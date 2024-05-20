#ifndef __MAIN_H

#define __MAIN_H

#include<iostream>
#include<string>
#include<QString>
#include<vector>
#include<queue>
#include<QMap>
#include<map>
#include<QMenu>
#include<QDialog>
#include<QEvent>
#include<QKeyEvent>
#include<QMouseEvent>
#include<QApplication>

typedef struct{
    int objId;
    int parentObjId = -1;   //no parent by default
    int lineNum = 0;
    QString objName;
    QString objTypeName;
    std::map<QString,QString> objectProperties;
    QObject *resolvedObj;
    void printToFile(std::ostream& o);
    void readFromFile(std::istream& in, int);

}QObjInfo;

typedef struct{
    int objId;
    int lineNum = 0;
    QString eventName;
    std::map<QString, QString> eventDataMap;
    void printToFile(std::ostream& o);
    void readFromFile(std::istream& in, int);
    std::list<QEvent*> createQEventList(QObject *&obj);
}QEventObj;


class EventHandler:public QObject{
    Q_OBJECT
    public:
       std::queue<std::pair<QObject*, QEventObj*>> replay_q;
       std::list<QEventObj*> eventsList;
       std::map<QObject*, QObjInfo*> objMap;
       std::map<int, QObjInfo*> idVsObjMap;
       bool eventFilter(QObject *obj, QEvent *event);
       bool logEvent(QObject *obj, QEvent *event);
       void createReplayEventQueue();
       void logSavedEventsToFile();
       void optimizeEvents();
       ~EventHandler();
       //void replayEventFromQueue();
};

#endif

