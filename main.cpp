#include "main.h"
#include <iostream>
#include <unistd.h>
#include <thread>
#include <QAbstractButton>
#include <QLabel>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <QComboBox>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QTableView>
#include <QModelIndex>
#include <QVariant>
#include <QWidgetAction>
#include <QDockWidget>
#include <QTimer>
#include <fstream>
#include <stack>
#include <QMainWindow>
#include <QDebug>
#include <QMetaEnum>
#include <QToolButton>
#include <QListWidget>
#include <QTimer>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include "logViewWindow.h"

EventHandler mainHandler;
logViewWindow *loggerWindow = nullptr;
const char *replayFileEnv = "REPLAY_QT_SO";
const char *logFileEnv = "LOG_QT_SO";
const char *optionsAndValuesEnv = "RR_QT_GUI_OPTIONS";

std::unordered_map<std::string, std::string> toolOptions = {
    {"USE_MODEL_DATA_FOR_TREE_VIEW","true"},
    {"USE_MODEL_DATA_FOR_LIST_VIEW","true"},
    {"SHOW_IGNORED_EVENTS","false"}
};

void waitForQApp()
{
    while(!QApplication::instance()) sleep(1);
}

void readAndParseOptionsCsv(const char *argsStr, std::unordered_map<std::string, std::string> &argsMap){
    std::istringstream argStream(argsStr);
    std::string s;
    while(getline(argStream, s, ','))
    {
        if(s == "") continue;
        std::string key;
        std::string val;
        auto eqPos = s.find('=');
        if(eqPos == std::string::npos)
        {
            key = s;
            val = "true";       //Treated as boolean
        } 
        else
        {
            key = s.substr(0, eqPos);
            val = s.substr(eqPos+1);
        }
        argsMap[key] = val;
    }
}


enum{
    NO_MODE,
    RECORD_MODE,
    REPLAY_MODE 
} appState;

int mainDebug = 0;

QString getQWidgetText(QWidget * obj)
{
    QAbstractButton *q_push_btn = qobject_cast<QAbstractButton*>(obj);
    QRadioButton *q_radio_btn = qobject_cast<QRadioButton*>(obj);
    QCheckBox *q_check_box = qobject_cast<QCheckBox*>(obj);
    QLabel *q_label = qobject_cast<QLabel*>(obj);
    QDockWidget *q_dock = qobject_cast<QDockWidget*>(obj);
    QListWidget *q_list_wgt = qobject_cast<QListWidget*>(obj);
    QString objName = obj?obj->objectName():"";
    QString ret;
    if(q_push_btn)
        ret = q_push_btn->text();
    else if(q_radio_btn)
        ret = q_radio_btn->text();
    else if(q_check_box)
        ret = q_check_box->text();
    else if(q_label)
        ret = q_label->text();
    else if(q_dock)
        ret = q_dock->windowTitle();
    else if(q_list_wgt && objName == "" && q_list_wgt->item(0))
        ret = q_list_wgt->item(0)->text();
    return ret;
}

std::map<QString,QString> getObjectProperties(QObject *obj)
{
    std::map<QString,QString> properties;
    QGroupBox *q_group_box = qobject_cast<QGroupBox*>(obj);
    QMenu *q_menu = qobject_cast<QMenu*>(obj);
    QDialog *q_dialog = qobject_cast<QDialog*>(obj);
    QWidget *q_widget = qobject_cast<QWidget*>(obj);
    QAction *q_action = qobject_cast<QAction*>(obj);

    if(q_menu)
    {
        properties["Visible"] = QString::number(q_menu->isVisible());
        if(q_menu->title().length())
            properties["MenuTitle"] = q_menu->title();
        QPoint pt(10,10);
        QAction *act_1 = q_menu->actionAt(pt);
        if(act_1)
        {
            auto w_act_1 = (qobject_cast<QWidgetAction*>(act_1));
            QString firstActionName;
            if(w_act_1)
            {
                QWidget *w = w_act_1->defaultWidget();
                if(w) firstActionName = getQWidgetText(w);
            }
            else
                firstActionName = act_1->text();
            if(firstActionName.length()) properties["FirstAction"] = firstActionName;
        }
    }
    else if(q_group_box)
        properties["Title"] = q_group_box->title();
    else if(q_dialog)
    {
        properties["Visible"] = QString::number(q_dialog->isVisible());
        properties["Modal"] = QString::number(q_dialog->isModal());
        properties["Title"] = q_dialog->windowTitle();
    }
    else if(q_widget && getQWidgetText(q_widget) != "")
        properties["WidgetText"] = getQWidgetText(q_widget);
    else if(q_action)
    {
        properties["ActionText"] = q_action->text(); 
    }

    if(properties.size() == 0)
    {
        QObject * p = obj->parent();
        QList<QWidget*> childList = p->findChildren<QWidget*>(QString());
        int j = 0;
        for(int i = 0; i<childList.size(); ++i)
        {
            if(!(childList[i]->isVisible() && childList[i]->parent()==p)) continue;
            if(childList[i] == obj)
            {
                properties["VisibleIndexInParent"] = QString::number(j);
                break;
            }
            ++j;
        }
    }
    return properties;
}

void QObjInfo::printToFile(std::ostream& o)
{
    o<<objId<<";"<<parentObjId<<";"<<qPrintable(objName)<<";"<<qPrintable(objTypeName)<<";";
    for(auto &objProp:objectProperties)
        o<<qPrintable(objProp.first)<<"="<<qPrintable(objProp.second)<<";";
    o<<"\n";
}

void QObjInfo::readFromFile(std::istream& in, int lineN)
{
    std::string line;
    std::getline(in, line);
    QStringList values = QString(line.c_str()).split(";");
    if(values.size() < 5) {
        std::cerr<<"Can't parse obj info: "<<line<<"\n";
        objId = -1;
        return;
    }
    lineNum = lineN;
    objId = values[0].toInt();
    parentObjId = values[1].toInt();
    objName = values[2];
    objTypeName = values[3];
    //objectProperties = values[4];
    for(int i = 4; i<values.size(); ++i)
    {
        QString value = values[i].trimmed();
        if(value.length()==0) continue;
        QStringList keyVal = value.split("=");
        if(keyVal.size() == 1) 
            objectProperties[keyVal[0]] = "";
        else if(keyVal.size() != 2) continue; //Error?
        objectProperties[keyVal[0]] = keyVal[1];
    }
}

void QEventObj::printToFile(std::ostream& o)
{
    o<<objId<<";"<<qPrintable(eventName)<<";";
    for(auto &e_data:eventDataMap)
        o<<qPrintable(e_data.first)<<"="<<qPrintable(e_data.second)<<";";
    o<<"\n";
}

void QEventObj::readFromFile(std::istream& in, int lineN)
{
    std::string line;
    std::getline(in, line);
    QStringList values = QString(line.c_str()).split(";");
    objId = values[0].toInt();
    eventName = values[1];
    lineNum = lineN;
    for(int i = 2; i<values.size(); ++i)
    {
        if(values[i].length()==0) continue;
        QStringList keyVal = values[i].split("=");
        if(keyVal.size() == 1) 
            eventDataMap[keyVal[0]] = "";
        else if(keyVal.size() != 2) continue; //Error?
        eventDataMap[keyVal[0]] = keyVal[1];
    }
}

QList<QObject*> findChildObjByNameAndType(QObject *obj, QObjInfo *childInfo)
{
    QList<QObject*> childList = obj?obj->findChildren<QObject*>(childInfo->objName):QList<QObject*>();
    QWidgetList topLevelList = obj?QWidgetList():(QApplication::topLevelWidgets());
    QList<QObject*> retList;
    int sz = obj?childList.size():topLevelList.size();
    //std::cerr<<"\nObject ID = "<<childInfo->objId<<"\n";
    for(int i = 0; i<sz; ++i)
    {
        QObject *o = obj?childList[i]:topLevelList[i];
        if(!o) continue;
        if(!obj && o->objectName() != childInfo->objName) continue;
        //std::cerr<<"\t"<<qPrintable(o->objectName())<<"; "<<(o->metaObject()->className())<<";"<<(bool)(o->parent() == obj)<<"\n";
        if(o->parent() != obj) continue;
        if(QString(o->metaObject()->className()) == childInfo->objTypeName)
        {
            retList.append(o);
        }
    }
    //std::cerr<<"\n\n";
    return retList;
}

QObject *findChildByProperties(QObject *obj, QObjInfo *childInfo)
{
    //assert that childInfo parent is matching
    QObject *c_obj = nullptr;
    QList<QObject*> objsMatchingName = findChildObjByNameAndType(obj, childInfo);

    int sz = objsMatchingName.size();
    if(sz == 1) return objsMatchingName[0];       //Sometimes objects are not loaded, so matching properties is must??
    for(int i = sz-1; i>=0; --i)    //Find last child that is added
    {
        QObject *o = objsMatchingName[i];
        std::map<QString,QString> objProperties = getObjectProperties(o);
        uint recMatchCount = 0;
        std::map<QString,QString> childObjProperties = childInfo->objectProperties;
        //if(childObjProperties.size() > 1) childObjProperties.erase("VisibleIndexInParent"); //Not a good property to match
        for(const auto &recordedKeyVal : childObjProperties)       //Match only stored properties, so that adding new properties don't break existing tests
        {
            if(objProperties[recordedKeyVal.first] != recordedKeyVal.second) break;
            ++recMatchCount;
        }
        if(recMatchCount == childObjProperties.size())
        {
            c_obj = o;  
            break;
        }
    }
    return c_obj;
}

QObjInfo* createObjectInfo(QObject *obj)
{
    static int objCount = 0;
    if(!obj) return nullptr;
    if(mainHandler.objMap.find(obj) != mainHandler.objMap.end())
        return mainHandler.objMap[obj];
    QObjInfo *parentInfo = createObjectInfo(obj->parent());

    QObjInfo *objInfo = new QObjInfo();
    objInfo->objName = obj->objectName();
    objInfo->objId = (objCount++);
    objInfo->objectProperties = getObjectProperties(obj);
    objInfo->parentObjId = parentInfo?parentInfo->objId:-1;
    objInfo->objTypeName = obj->metaObject()->className();
    objInfo->resolvedObj = nullptr; //resolve only by resolver for testing, but set topmost parent
    mainHandler.idVsObjMap[objInfo->objId] = objInfo;
    mainHandler.objMap[obj] = objInfo;
    return objInfo;
}

QObjInfo *parentInfo(QObjInfo *objInfo)
{
    if(!objInfo) return nullptr;
    if(objInfo->parentObjId == -1) return nullptr;
    if(mainHandler.idVsObjMap.find(objInfo->parentObjId) == mainHandler.idVsObjMap.end()) return nullptr;
    return mainHandler.idVsObjMap[objInfo->parentObjId];
}

QObject *findObjectFromInfo(QObjInfo *objInfo)
{
    if(!objInfo) return nullptr;
    if(objInfo->resolvedObj) return objInfo->resolvedObj;   //Optimize for unresolvalble nodes
    QObject *parentObj = findObjectFromInfo(parentInfo(objInfo));
    QObject *resObj = findChildByProperties(parentObj, objInfo);
    return resObj;  //Always find, bcoz some items may match based on prior state of GUI
    objInfo->resolvedObj = resObj;
    return objInfo->resolvedObj;
}

template <typename T>
T* getFirstParentOfType(QObject *obj)
{
    QObject *o = obj;
    while(o)
    {
        T *cast_obj = qobject_cast<T*>(o);
        if(cast_obj) return cast_obj;
        o = o->parent();
    }
    return nullptr;
}

QComboBox* getQComboWidgetArea(QObject *obj)
{
    return getFirstParentOfType<QComboBox>(obj);
}

QAbstractItemView* getAbstractView(QObject *obj)
{
    if(!obj) return nullptr;
    if(obj->objectName() != "qt_scrollarea_viewport") return nullptr;
    return getFirstParentOfType<QAbstractItemView>(obj);
}

QTableView* getTableView(QObject *obj)
{
    if(obj->objectName() != "qt_scrollarea_viewport") return nullptr;
    return getFirstParentOfType<QTableView>(obj);
}

QHeaderView* getHeaderView(QObject *obj)
{
    if(obj->objectName() != "qt_scrollarea_viewport") return nullptr;
    return getFirstParentOfType<QHeaderView>(obj);
}

QModelIndex *QModelIndexFromStr(QAbstractItemView *v, QString str)
{
    QStringList idxList = str.split(",");
    QModelIndex *parent = nullptr;
    QModelIndex *idx = nullptr;
    for(int i = 0; i<idxList.size(); ++i)
    {
        QStringList rc = idxList[i].split(":");
        int r = rc[0].toInt();
        int c = rc[1].toInt();
        idx = nullptr;
        if(v->model())
            idx = new QModelIndex(v->model()->index(r,c,parent?*parent:QModelIndex()));
        if(!idx || !idx->isValid()) {
            std::cerr<<"Invalid index "<<i<<" "<<r<<" "<<c<<"\n";
            return nullptr;
        }
        //else
       // {
            //std::cerr<<"Valid index "<<i<<" "<<r<<" "<<c<<" "<<qPrintable(idx->data().toString())<<"\n";
       // }
        parent = idx;
    }
    return idx;
}

QModelIndex *QModelIndexFromDataStr(QAbstractItemView *v, QString data)
{
    //Format: Row0Col0 data\Row1 Col0 Data\Row2 Col0 Data:col
    QStringList dataList = data.split("\\");
    QModelIndex *parent = nullptr;
    QModelIndex *idx = nullptr;
    for(int i = 0; i<dataList.size(); ++i)
    {
        idx = nullptr;
        QString data = dataList[i];
        int col = 0;
        if(i==dataList.size() - 1)
        {
            auto tempList = data.split(":");
            data = tempList[0];
            col = (tempList.size()>1)?tempList[1].toInt():0;
        }
        int rCount = v->model()->rowCount(parent?*parent:QModelIndex());
        for(int r=0; r<rCount; ++r)
        {
            QString r_col0_data = v->model()->index(r,0,parent?*parent:QModelIndex()).data().toString();
            //std::cerr<<"Data at col 0 of row "<<r<<qPrintable(r_col0_data)<<"\n";
            if(r_col0_data == data)
            {
                idx = new QModelIndex(v->model()->index(r,col,parent?*parent:QModelIndex()));
                break;
            }
        }
        if(!idx || !idx->isValid()) {
            std::cerr<<"Data "<<qPrintable(data)<<" not found in view\n";
            return nullptr;
        }
        //else
       // {
            //std::cerr<<"Valid index "<<i<<" "<<r<<" "<<c<<" "<<qPrintable(idx->data().toString())<<"\n";
       // }
        parent = idx;
    }
    return idx;
}

long getSleepTimeUsBetweenEvents(QEventObj *eventObj)
{
    int miliSec = 500;
    if(eventObj && eventObj->eventName == "KeyClick")
        miliSec = 50;
    return miliSec*1000;
}

std::list<QEvent*> QEventObj::createQEventList(QObject *&obj)
{
    std::list<QEvent*> eventList;
    QEvent::Type type = (QEvent::Type)eventDataMap["Type"].toInt();
    if(/*eventName == "QKeyEvent" || */eventName == "KeyClick" || eventName == "KeySequenceClick")
    {
        bool multipleKeys = (eventName == "KeySequenceClick")?true:false;
        //bool click = (eventName == "KeyClick" || eventName == "KeySequenceClick")?true:false;
        //if(click) type = QEvent::KeyPress;
        QString textD = eventDataMap["text"];
        int key = multipleKeys?0:eventDataMap["Key"].toInt();
        Qt::KeyboardModifiers m = (Qt::KeyboardModifiers)eventDataMap["Modifiers"].toInt(); 
        int count = multipleKeys?1:eventDataMap["Count"].toInt();
        QStringList textSeq;
        if(multipleKeys) textSeq = textD.split("");
        else textSeq.append(textD);
        for(QString text:textSeq)
        {
            QKeyEvent *keyEvent = new QKeyEvent(QEvent::KeyPress, key, m, text, count==1?false:true, count);
            eventList.push_back(keyEvent);
            keyEvent = new QKeyEvent(QEvent::KeyRelease, key, m, text, count==1?false:true, count);
            eventList.push_back(keyEvent);
        }
    }
    else if(eventName == "QShortcutEvent")
    {
        QShortcutEvent *scEvent = new QShortcutEvent(eventDataMap["KeySeqStr"], 0);
        eventList.push_back(scEvent);
    }
    else if(eventName == "QMouseEvent" || eventName == "MenuActionClick" || eventName == "MouseClick")
    {
        int x= eventDataMap["localX"].toInt();
        int y = eventDataMap["localY"].toInt();
        Qt::MouseButton button = (Qt::MouseButton)eventDataMap["button"].toInt();
        Qt::MouseButtons buttons = (Qt::MouseButtons)eventDataMap["buttons"].toInt();
        Qt::KeyboardModifiers m = (Qt::KeyboardModifiers)eventDataMap["keyboardModifers"].toInt();
        bool foundCorrectPoint = true;
        if(eventName == "MenuActionClick")  //Before click move to position
        {
            foundCorrectPoint = false;
            QString actName = eventDataMap["ActionName"];
            QMenu *qm = qobject_cast<QMenu*>(obj);
            QList<QAction*> actList = qm->actions();
            for(auto ac:actList)
            {
                if(ac->text() == actName)// && type == QEvent::MouseButtonRelease)
                {
                    QRect actG = qm->actionGeometry(ac);
                    x = actG.x()+1;
                    y = actG.y()+1;
                    foundCorrectPoint = true;
                    break;
                }
            }
        }
        else if(eventDataMap.find("ModelIndex") != eventDataMap.end() || eventDataMap.find("ModelData") != eventDataMap.end())
        {
            QAbstractItemView *v = getAbstractView(obj);
            QModelIndex *idx = (eventDataMap.find("ModelIndex") != eventDataMap.end())?QModelIndexFromStr(v, eventDataMap["ModelIndex"]):QModelIndexFromDataStr(v, eventDataMap["ModelData"]);
            if(idx && idx->isValid())
            {
                int dx = eventDataMap[QString("dx")].toInt();
                int dy = eventDataMap[QString("dy")].toInt();
                QTreeView *tv = qobject_cast<QTreeView*>(v);
                if(tv){
                    //std::cerr<<"Expanding tree view\n";
                    tv->expand(*idx);
                }
                v->scrollTo(*idx);
                QRect rect = v->visualRect(*idx);
                x = rect.x()+dx;
                y = rect.y()+dy;
            }
            else
                foundCorrectPoint = false;
        }
        else if(eventDataMap.find("HeaderIndex") != eventDataMap.end() && getTableView(obj))
        {
            QTableView *v = getTableView(obj);
            Qt::Orientation ho = (Qt::Orientation)eventDataMap["HeaderOrientation"].toInt();
            int hi = eventDataMap["HeaderIndex"].toInt();
            QHeaderView *hv = (ho == Qt::Horizontal)?v->horizontalHeader():v->verticalHeader();
            int firstC = v->horizontalHeader()->logicalIndex(0);
            int firstR = v->verticalHeader()->logicalIndex(0);
            QModelIndex tableIndex = v->model()->index((ho==Qt::Horizontal)?hi:firstR, (ho==Qt::Horizontal)?firstC:hi);  
            v->scrollTo(tableIndex);
            if(ho==Qt::Horizontal)
                x = hv->sectionViewportPosition(hi) + eventDataMap[QString("dx")].toInt();
            else
                y = hv->sectionViewportPosition(hi) + eventDataMap[QString("dy")].toInt();
        }
        if(foundCorrectPoint)
        {
            QPoint l_pos(x, y);
            QPoint g_pos = qobject_cast<QWidget*>(obj)->mapToGlobal(l_pos); //Necessary for QMenu atleast
            QMouseEvent *mEvent = new QMouseEvent(type, l_pos, g_pos, button, buttons, m);
            eventList.push_front(mEvent); 
            if(eventName == "MenuActionClick" || eventName == "MouseClick")
            {
                QMouseEvent *mEvent1 = new QMouseEvent(QEvent::MouseButtonPress, l_pos, g_pos, button, buttons, m);
                //QMouseEvent *moveMouseToObj = new QMouseEvent(QEvent::MouseMove, l_pos, g_pos, button, buttons, m);
                eventList.push_front(mEvent1);
                //eventList.push_front(moveMouseToObj);
            }
            QCursor::setPos(g_pos);
            usleep(100);
        }
    }
    else if(eventName == "QContextMenuEvent")
    {
        int x= eventDataMap["X"].toInt();
        int y = eventDataMap["Y"].toInt();
        QAbstractItemView *v = getAbstractView(obj);
        if(eventDataMap.find("ModelIndex") != eventDataMap.end() || eventDataMap.find("ModelData") != eventDataMap.end())
        {
            QModelIndex *idx = (eventDataMap.find("ModelIndex") != eventDataMap.end())?QModelIndexFromStr(v, eventDataMap["ModelIndex"]):QModelIndexFromDataStr(v, eventDataMap["ModelData"]);
            if(idx && idx->isValid())
            {
                int dx = eventDataMap[QString("dx")].toInt();
                int dy = eventDataMap[QString("dy")].toInt();
                QTreeView *tv = qobject_cast<QTreeView*>(v);
                if(tv){
                    //std::cerr<<"Expanding tree view\n";
                    tv->expand(*idx);
                }
                v->scrollTo(*idx);
                QRect rect = v->visualRect(*idx);
                x = rect.x()+dx;
                y = rect.y()+dy;
            }
        }
        QPoint l_pos(x, y);
        QPoint g_pos = v->viewport()->mapToGlobal(l_pos); //Necessary for QMenu atleast
        QContextMenuEvent::Reason reason = (QContextMenuEvent::Reason)eventDataMap["Reason"].toInt();
        QCursor::setPos(g_pos);
        usleep(100);
        eventList.push_back(new QContextMenuEvent(reason, l_pos, g_pos));
    }
    else if (eventName == "ComboSelectionChange")
    {
        QComboBox *combo = qobject_cast<QComboBox*>(obj);
        combo->showPopup();
        usleep(1000);
        int index = combo->findText(eventDataMap["NewSelText"]);
        if(index != -1)
            combo->setCurrentIndex(index);
        else
            std::cerr<<"Could not find \""<<qPrintable(eventDataMap["NewSelText"])<<"\" in combo box\n";
        combo->hidePopup();
    }
    //else if(eventName == "QScrollEvent")
    //{
    //    QPoint contentPos(eventDataMap["contentPos_x"].toInt(), eventDataMap["contentPos_y"].toInt());
    //    QPoint overshootDis(eventDataMap["overshoot_x"].toInt(), eventDataMap["overshoot_y"].toInt());
    //    QScrollEvent::ScrollState state = (QScrollEvent::ScrollState)eventDataMap["scrollState"].toInt();

    //    event = new QScrollEvent(contentPos, overshootDis, state);
    //}
    //else if (eventName == "QResizeEvent")
    //{
    //    int h = eventDataMap["NewHeight"].toInt();
    //    int w = eventDataMap["NewWidth"].toInt();
    //    QSize newSize(w, h);
    //    QWidget *qw = qobject_cast<QWidget*>(obj);
    //    eventList.push_back(new QResizeEvent(newSize, qw->size()));
    //}
    else if (eventName == "QFocusEvent")
    {
        eventList.push_back((QEvent*)new QFocusEvent(type));
    }
    else if(eventName == "QCloseEvent")
    {
        QTimer::singleShot(100, (QWidget*)obj, SLOT(close()));  //use single shot to call close, otherwise it can block all other calls
        //((QWidget*)obj)->close();
        usleep(getSleepTimeUsBetweenEvents(this));
        //eventList.push_back((QEvent*)new QCloseEvent());
    }
    else if(eventName == "CloseAllWindows")
    {
        QList<QWidget *> topLevelWidgets = QApplication::topLevelWidgets();
        std::cerr << "Closing top level Widgets: \n";
        for (QWidget *widget : topLevelWidgets) {
            if(widget->isVisible())
                std::cerr<<"\t"<<qPrintable(widget->objectName()) << " (" << widget->metaObject()->className()<<"); \n";
        }
        std::cerr<<"\n";
        QApplication::quit();
    }
    else
        std::cerr<<"Invalid event name: "<<qPrintable(eventName)<<"\n";
    return eventList;
}

void EventHandler::logSavedEventsToFile()
{
    if(appState != RECORD_MODE) return;
    if(!eventsList.size()) return;
    const char *out_file = getenv(logFileEnv);
    optimizeEvents();
    if(!out_file || *out_file =='\0') out_file = "gui_actions.replay";
    std::ofstream of(out_file);
    for(auto &iter:idVsObjMap)
    {
        auto obj = iter.second;
        of<<"OBJECT ";
        obj->printToFile(of);
        //std::cerr<<"Object Id = "<<obj->objId<<"\n"; 
        //std::cerr<<"Object Name = "<<qPrintable(obj->objName)<<"\n"; 
        //std::cerr<<"Object Class Name = "<<qPrintable(obj->objTypeName)<<"\n"; 
        //std::cerr<<"Extra Data = "<<qPrintable(obj->objectProperties)<<"\n"; 
        //std::cerr<<"Parent Id = "<<obj->parentObjId<<"\n"; 
        //std::cerr<<"Resolved = "<<(obj->resolvedObj?"true":"false")<<"\n"; 

        //std::cerr<<"\n\n";
    }
    for(auto &obj:eventsList)
    {
        of<<"EVENT ";
        obj->printToFile(of);
    }
}

EventHandler::~EventHandler()
{
    logSavedEventsToFile();
}

QString QModelIndexToString(QModelIndex idx)
{
    QString str;
    std::stack<std::pair<int, int>> indexes;
    //std::stack<QString> dataS;
    while(idx.isValid())
    {
        indexes.push(std::pair<int, int>(idx.row(), idx.column()));
        //dataS.push(idx.data().toString());
        QModelIndex p = idx.parent();
        idx = p;
    }
    while(indexes.size())
    {
        auto rc = indexes.top();
        indexes.pop();
        if(str.length()) str +=",";
        str += QString::number(rc.first);
        str += ":";
        str += QString::number(rc.second);
        //std::cerr<<qPrintable(str)<<" = "<<qPrintable(dataS.top())<<"; ";
        //dataS.pop();
    }
    //std::cerr<<"\n";
    return str;
}

QString QModelIndexToDataString(QModelIndex idx)
{
    QString str;
    std::stack<QString> col0Datas;
    int col=idx.column();
    while(idx.isValid())
    {
        QModelIndex col0Idx = idx.model()->index(idx.row(), 0, idx.parent());
        col0Datas.push(col0Idx.data().toString());
        QModelIndex p = idx.parent();
        idx = p;
    }
    while(col0Datas.size())
    {
        auto rc = col0Datas.top();
        col0Datas.pop();
        if(str.length()) str +="\\";
        str += rc;
    }
    str+= ":";
    str += QString::number(col);
    //std::cerr<<"\n";
    return str;
}

void EventHandler::optimizeEvents()
{
    if(appState != RECORD_MODE || eventsList.size() < 2) return;
    auto prevEventIter = eventsList.begin();
    auto currEventIter = std::next(prevEventIter);
    while(currEventIter != eventsList.end())
    {
        QEventObj *currEvent = *currEventIter;
        QEventObj *prevEvent = *prevEventIter;
        //if(currEvent->objId == prevEvent->objId)
        {
            if(currEvent->eventName == "KeyClick" && (prevEvent->eventName == "KeyClick" || prevEvent->eventName == "KeySequenceClick"))
            {
                bool prevEventSeq = (prevEvent->eventName == "KeySequenceClick")?true:false;
                int prevModifiers = prevEvent->eventDataMap["Modifiers"].toInt();
                int currModifiers = currEvent->eventDataMap["Modifiers"].toInt();
                int prevCount = prevEventSeq?1:prevEvent->eventDataMap["Count"].toInt();
                int currCount = currEvent->eventDataMap["Count"].toInt();
                int prevKey = prevEventSeq?0:prevEvent->eventDataMap["Key"].toInt();
                int currKey = currEvent->eventDataMap["Key"].toInt();
                bool valid = true;
                if(!(currModifiers == 0 || currModifiers == Qt::ShiftModifier)) valid = false;  //Don't continue, need to increment
                if(!(prevModifiers == 0 || prevModifiers == Qt::ShiftModifier)) valid = false;
                if(prevCount != 1 || currCount != 1) valid = false;
                if((currKey & 0x01000000) || (prevKey & 0x01000000)) valid = false;
                if(valid)
                {
                    QString mergedText =  prevEvent->eventDataMap["text"] + currEvent->eventDataMap["text"];
                    if(!prevEventSeq)
                    {
                        prevEvent->eventName = "KeySequenceClick";
                        prevEvent->eventDataMap.clear();
                        prevEvent->eventDataMap["Modifiers"] = "0";
                        prevEvent->eventDataMap["Type"] = QString::number(QEvent::KeyPress);
                    }
                    prevEvent->eventDataMap["text"] = mergedText;
                    currEventIter = eventsList.erase(currEventIter);        //Points to next iter, don't update prevIter
                    continue;
                }
            }
        }
        ++currEventIter;
        ++prevEventIter;  
    }
}

void addDelayEventToList(std::list<QEventObj*> &eventsList, int secs)
{
    QEventObj *delayEvent = new QEventObj();
    delayEvent->eventName = "DELAY";
    delayEvent->objId = 0;
    delayEvent->eventDataMap["TIME"]=QString::number(secs);
    eventsList.push_back(delayEvent);
}

bool fillModelIndexInfoForViewEvents(QObject *obj, QPoint pos, std::map<QString, QString> &paramValues){
    QAbstractItemView *v = getAbstractView(obj);
    if(!v) return false;
    QModelIndex idx = v->indexAt(pos);
    bool dataFilled = false;
    if(idx.isValid())
    {
        static bool useModelDataForTreeView = (toolOptions["USE_MODEL_DATA_FOR_TREE_VIEW"] == "true")?true:false;
        static bool useModelDataForListView = (toolOptions["USE_MODEL_DATA_FOR_LIST_VIEW"] == "true")?true:false;
        if(qobject_cast<QTreeView*>(v) && useModelDataForTreeView)
            paramValues["ModelData"] = QModelIndexToDataString(idx);
        else if(qobject_cast<QListView*>(v) && useModelDataForListView)
            paramValues["ModelData"] = QModelIndexToDataString(idx);
        else
            paramValues["ModelIndex"] = QModelIndexToString(idx);

        //QModelIndex *retIdx = QModelIndexFromStr(v,  paramValues["ModelIndex"]);
        QRect visR = v->visualRect(idx);
        int dx = pos.x() - visR.x();
        int dy = pos.y() - visR.y();
        paramValues["dx"] = QString::number(dx);
        paramValues["dy"] = QString::number(dy);
        dataFilled = true;
    }
    else if(getTableView(obj) && getHeaderView(obj))
    {
        QHeaderView *hv = getHeaderView(obj);
        int headerIndex = hv->logicalIndexAt(pos);
        int secPos = hv->sectionViewportPosition(headerIndex);
        int ho = hv->orientation();
        paramValues["HeaderIndex"] = QString::number(headerIndex);
        paramValues["HeaderOrientation"] = QString::number(ho);
        if(ho==Qt::Horizontal)
            paramValues["dx"] = QString::number(pos.x() - secPos);
        else
            paramValues["dy"] = QString::number(pos.y() - secPos);
        dataFilled = true;
    }
    return dataFilled;
}

bool EventHandler::logEvent(QObject *obj, QEvent *event)
{
    if(appState != RECORD_MODE) return false;
    const char *eventName = nullptr;
    std::map<QString, QString> paramValues;
    static QObject *s_lastEventObj = nullptr;
    static QEvent::Type s_lastEventType = QEvent::None;
    switch(event->type())
    {
        case QEvent::FocusIn:
            //if(obj->inherits("QMenu"))
            //{
            //    eventName = "QFocusEvent";
            //    paramValues["Type"] = QString::number(event->type());
            //}
        case QEvent::FocusOut:
            break;
        case QEvent::Shortcut:
            eventName = "QShortcutEvent";
            {
            QShortcutEvent *scEvent = static_cast<QShortcutEvent*>(event);
            paramValues["Type"] = QString::number(scEvent->type());
            paramValues["KeySeqStr"] = scEvent->key().toString();
            }
            break;
        case QEvent::KeyPress:
        //case QEvent::KeyRelease:
            eventName = "KeyClick";
            {
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            if(((keyEvent->modifiers() && keyEvent->modifiers() != Qt::ShiftModifier)) ||
                   (keyEvent->key() == Qt::Key_Shift) ) //Keys with modifiers other than shift not handled, shortcuts handled separately
            {
                eventName = nullptr;
                break;
            }
            paramValues["Type"] = QString::number(keyEvent->type());
            paramValues["Key"] = QString::number(keyEvent->key());
            paramValues["Modifiers"] = QString::number(keyEvent->modifiers());
            paramValues["Count"] = QString::number(keyEvent->count());
            paramValues["text"] = keyEvent->text();
            }
            break;
        /*case QEvent::Scroll:
            eventName = "QScrollEvent";
            {
            QScrollEvent *scrollEvent = static_cast<QScrollEvent*>(event);
            paramValues["Type"] = QString::number(scrollEvent->type());
            paramValues["contentPos_x"] = QString::number(scrollEvent->contentPos().x);
            paramValues["contentPos_y"] = QString::number(scrollEvent->contentPos().y);
            paramValues["overshoot_x"] = QString::number(scrollEvent->overshootDistance().x);
            paramValues["overshoot_y"] = QString::number(scrollEvent->overshootDistance().y);
            paramValues["scrollState"] = QString::number(scrollEvent->scrollState());

            }
            break;*/
        /*case QEvent::MouseButtonPress:
            {
                QToolButton *toolBtn = qobject_cast<QToolButton*>(obj);
                QPushButton *pushBtn = qobject_cast<QPushButton*>(obj);
                QMouseEvent *mEvent =  static_cast<QMouseEvent*>(event);
                if((toolBtn && toolBtn->menu()) || (pushBtn && pushBtn->menu()))
                {           //Only for button with menu
                    eventName = "MouseClick";
                    paramValues["Type"] = QString::number(QEvent::MouseButtonRelease);
                    paramValues["localX"] = QString::number(mEvent->x());
                    paramValues["localY"] = QString::number(mEvent->y());
                    paramValues["button"] = QString::number(mEvent->button());
                    paramValues["buttons"] = QString::number(mEvent->buttons());
                    paramValues["keyboardModifers"] = QString::number(mEvent->modifiers());
                }
            }
            break;*/
        case QEvent::MouseButtonRelease:
            break;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
            {
                QMouseEvent *mEvent =  static_cast<QMouseEvent*>(event);
                if(qobject_cast<QMenu*>(obj))
                {
                    if(event->type() != QEvent::MouseButtonPress) break;  //Optimize menu selection
                    QMenu *q_menu = qobject_cast<QMenu*>(obj);
                    QAction *action = q_menu->actionAt(mEvent->pos());
                    if(action && action->text() != ""){
                        paramValues["ActionName"] = action->text();
                        eventName = "MenuActionClick";
                    }
                    else
                        break;
                }
                else if(getQComboWidgetArea(obj))
                {
                    if(event->type() != QEvent::MouseButtonPress) break;  //Optimize combo box selection
                    QComboBox *c_box = getQComboWidgetArea(obj);
                    if(obj == c_box)  //Just list shown, continue with click
                        eventName = "MouseClick";
                    else
                    {
                        QAbstractItemView *listV = c_box->view();
                        if(listV)
                        {
                            obj = c_box;
                            QModelIndex idx = listV->indexAt(mEvent->pos());
                            if(idx.isValid())
                            {
                                eventName = "ComboSelectionChange";
                                paramValues["NewSelText"] = idx.data(Qt::DisplayRole).toString();  
                            }
                        }
                    }
                }
                else if(getAbstractView(obj))
                {
                    eventName = "MouseClick";
                    fillModelIndexInfoForViewEvents(obj, mEvent->pos(), paramValues);
                }
                else
                    eventName = "MouseClick";
                QEvent::Type logEventType = event->type();
                if(logEventType == QEvent::MouseButtonPress) logEventType = QEvent::MouseButtonRelease;
                paramValues["Type"] = QString::number(logEventType);
                paramValues["localX"] = QString::number(mEvent->x());
                paramValues["localY"] = QString::number(mEvent->y());
                paramValues["button"] = QString::number(mEvent->button());
                paramValues["buttons"] = QString::number(mEvent->buttons());
                paramValues["keyboardModifers"] = QString::number(mEvent->modifiers());
            }
            break;
        //case QEvent::Resize:
        //    if(!(qobject_cast<QMainWindow*>(obj) || (qobject_cast<QDockWidget*>(obj)) || (qobject_cast<QDockWidget*>(obj))))
        //        break;
        //    {
        //        QResizeEvent *rszEvent = static_cast<QResizeEvent*>(event);
        //        QSize newSize =  rszEvent->size();
        //        if(newSize == rszEvent->oldSize()) break;
        //        paramValues["Type"] = QString::number(rszEvent->type());
        //        paramValues["NewHeight"] = QString::number(newSize.rheight());
        //        paramValues["NewWidth"] = QString::number(newSize.rwidth());
        //        eventName = "QResizeEvent";
        //    }
        //    break;
        case QEvent::ContextMenu:
            eventName = "QContextMenuEvent";
            {
            QContextMenuEvent *contextEvent = static_cast<QContextMenuEvent*>(event);
            if(getAbstractView(obj)) fillModelIndexInfoForViewEvents(obj, QPoint(contextEvent->x(), contextEvent->y()), paramValues);
            paramValues["Reason"] = QString::number(contextEvent->reason());
            paramValues["X"] = QString::number(contextEvent->x());
            paramValues["Y"] = QString::number(contextEvent->y());
            }
            break;
        case QEvent::Close:
            if(!obj->inherits("QToolTip") && !obj->inherits("QTipLabel") && !obj->inherits("Qtitan::RibbonToolTip")
                    && !obj->inherits("QMenu"))
                eventName = "QCloseEvent";
            break;
        default:
            break;
    }

    /*if(!eventName && obj->inherits("QMenu"))
    {
        std::cerr<<"Signal not captured on "<<qPrintable(obj->metaObject()->className())
                 <<" type "<<event->type()<<"\n";
    }*/
    if(eventName)
    {
        QString objName = obj->objectName();
        const QObject *currObj = obj->parent();
        while(currObj)
        {   
            objName.prepend(".");
            objName.prepend(currObj->objectName());
            currObj = currObj->parent();
        }
        QObjInfo *infoObj = createObjectInfo(obj);
        QObject *foundObj = findObjectFromInfo(infoObj);    //ApplicationMainWindow::getInstance(), objName);
        /*QEventObj *lastRecordedEvent = eventsList.back();
        if(lastRecordedEvent && lastRecordedEvent->objId == infoObj->objId && lastRecordedEvent->eventName == eventName)
        {
            if(strcmp(eventName, "QResizeEvent")==0)
            {
                //eventsList.remove(eventsList.end()-1);
                eventsList.pop_back(); 
                lastRecordedEvent = nullptr;
            }
        }*/
        if(!foundObj)
            std::cerr<<"\t Could not resolve object by name ("<<qPrintable(infoObj->objTypeName)<<")\n";
        else if(foundObj != obj)
            std::cerr<<"\t Found object is different from event fired object ("<<qPrintable(infoObj->objTypeName)<<")\n";
        int addDelay = 0;
        if(!foundObj || foundObj != obj) addDelay = 3;
        static std::unordered_set<QAbstractItemView*> s_processedViews;
        QAbstractItemView *objView = getAbstractView(obj);
        if(objView && s_processedViews.find(objView) == s_processedViews.end())
        {
            s_processedViews.insert(objView);
            addDelay = 3;
        }
        else if(objView)
            addDelay = 1;
        if(addDelay) addDelayEventToList(eventsList, addDelay); //Add delay in case objects need loading
        eventsList.push_back(new QEventObj());
        eventsList.back()->objId = infoObj->objId;
        eventsList.back()->eventName = eventName;
        eventsList.back()->eventDataMap = paramValues;
        std::cerr<<"QT Event recorded: Event Name = "<<eventName<<", Object ID = "<<infoObj->objId<<", Object = "<<qPrintable(objName)<<"\n";
    }
    static bool showIgnoreEvents = (toolOptions["SHOW_IGNORED_EVENTS"] == "true")?true:false;
    if(!eventName && showIgnoreEvents && obj)
    {
        static int eventEnumIndex = QEvent::staticMetaObject.indexOfEnumerator("Type");
        QString eventName = QEvent::staticMetaObject.enumerator(eventEnumIndex).valueToKey(event->type());
        if(eventName == "") eventName = QString::number(event->type());
        std::cerr<<"QT EVENT Ignored "<<qPrintable(eventName)<<" on "<<qPrintable(obj->objectName())<<" ("<<obj->metaObject()->className()<<") Widget Text: "<<qPrintable(getQWidgetText(qobject_cast<QWidget*>(obj)))<<"\n";
    }

    s_lastEventObj = obj;
    s_lastEventType = event->type();
    return eventName?true:false;
}

int getWaitTimeBasedOnEvent(const QString &eventName)
{
    if(eventName == "QCloseEvent")
        return 1;
    return 10;
}

long getSleepTimeUsForEventList(QObject *obj, QEventObj *eventObj)
{
    int miliSec = 500;
    if(eventObj->eventName == "KeySequenceClick" || eventObj->eventName == "KeyClick" || eventObj->eventName == "MouseClick")
        miliSec = 50;
    if(!obj) miliSec = 1000;
    return miliSec*1000;
}

void addCloseEventToEnd(std::list<QEventObj*> &eventsList)
{
    addDelayEventToList(eventsList, 10);
    QEventObj *closeAppEvent = new QEventObj();
    closeAppEvent->eventName = "CloseAllWindows";
    closeAppEvent->objId = 0;
    eventsList.push_back(closeAppEvent);
}

bool waitForObjectReady(QObject *q_obj)
{
    bool ready = true;
    QWidget *q_widget = qobject_cast<QWidget*>(q_obj);
    if(q_widget)
    {
        ready = false;
        uint delay = 10;
        do{
            if(q_widget->isEnabled()){
                ready = true;
                break;
            }
            sleep(1);
            --delay;
        }while(delay);
    }
    return ready;
}

void EventHandler::createReplayEventQueue()
{
    //Should be called only once
    static bool firstTime = true;
    if(!firstTime) return;
    firstTime = false;
    std::thread e_th(
       [this](){
       const char *replayQtPath = getenv(replayFileEnv);
       if(*replayQtPath == '\0')
           replayQtPath = "gui_actions.replay";
       std::ifstream inf(replayQtPath);
       int lineNum = 0;
       while(!inf.eof())
       {
           std::string s;
           inf >> s;
           ++lineNum;
           if(s == "OBJECT")
           {
               QObjInfo *info = new QObjInfo();
               info->readFromFile(inf, lineNum);
               idVsObjMap[info->objId] = info;
           }
           else if(s == "EVENT")
           {
               eventsList.push_back(new QEventObj());
               eventsList.back()->readFromFile(inf, lineNum);
           }
       }
       //addCloseEventToEnd(eventsList);
       sleep(10);
       int i = 0;
       for(auto eventObj:eventsList)
       {
           QObject *q_obj = nullptr;
           QObjInfo *o_info = idVsObjMap[eventObj->objId];
           ++i;
           //std::cerr<<"Executing event "<<i<<"...";
           std::cerr<<"Executing event "<<i<<" Obj ID: " <<eventObj->objId<<" ";
           if(eventObj->lineNum) std::cerr<<"at line "<<eventObj->lineNum;
           std::cerr<<"...";
           if(eventObj->eventName == "DELAY")
           {        
               int secs = eventObj->eventDataMap["TIME"].toInt();
               for(long i=0; i<1000L*secs; ++i) usleep(1000);
               std::cerr<<"\n";
               continue;
           }
           if(!o_info){
               std::cerr<<"Object info not found "<<eventObj->objId<<"\n";
               continue;
           }
           int wait = getWaitTimeBasedOnEvent(eventObj->eventName);
           do{
               q_obj = findObjectFromInfo(o_info);
               --wait;
               if(q_obj) break;
               sleep(1);
           }while(wait && !q_obj);
           if(!q_obj && eventObj->eventName != "QCloseEvent"){  //Don't show error for close event
               std::cerr<<"Object not found "<<eventObj->objId<<"\n";
               continue;
           }
           if(!waitForObjectReady(q_obj)){
               std::cerr<<"Object not ready "<<eventObj->objId<<"\n";
               continue;
           }
           std::cerr<<"\n";
           //notify_mutex.lock();
           replay_q.push(std::pair<QObject*, QEventObj*>(q_obj, eventObj));
           //notify_mutex.unlock();
           //if(event->type() != QEvent::MouseButtonPress)   //Maybe release is next, causing click
           while(replay_q.size())
               usleep(getSleepTimeUsBetweenEvents(eventObj));
       }
       usleep(1000*100);
       QApplication::sendPostedEvents();
     }
    );
    e_th.detach();
}

bool EventHandler::eventFilter(QObject *obj, QEvent *event)
{
    //static bool inCall = false;
    //if(inCall)  return false;   //Prevent recursive call
    if(!loggerWindow){
        loggerWindow = (logViewWindow*)0x01;    //Stop recursive calling
        loggerWindow = new logViewWindow("log_view_contents");
    }
    if(appState == RECORD_MODE)
    {
        if(logEvent(obj, event))
        {
            if(loggerWindow && getAbstractView(obj))
                loggerWindow->setCurrentView(getAbstractView(obj));
        }
        if(event->type() == QEvent::Destroy)
        {
            if(objMap.find(obj) != objMap.end())
            {
                //int objId = objMap[obj]->objId;
                //std::cerr<<"Object destroyed event: "<<qPrintable(obj->objectName())<<" "<<objId<<"\n";
                objMap.erase(obj);
            }
        }
    }
    else
    {
        static QTimer sTimer;
        static bool firstTime = true;
        if(firstTime)
        {
            firstTime = false;
            createReplayEventQueue();
            sTimer.setInterval(1);      //Just in case event loop doesn't start
        }
        if(replay_q.size())
        {
            static bool processingEvent = false;    //Don't recursively process other events
            if(!processingEvent){
                processingEvent = true;
                waitForQApp();
                auto& p = replay_q.front();
                //static int tryCount = 0;
                //int maxTryCount = 30;
                QObject *_obj = p.first;
                QEventObj *eObj = p.second;
                std::list<QEvent*> eventL = eObj->createQEventList(_obj);

                obj = _obj;
                for(auto _event:eventL){
                    QApplication::postEvent(_obj, _event);
                    if(_event != eventL.back()) usleep(getSleepTimeUsForEventList(_obj, eObj));
                }
                if(obj && loggerWindow && getAbstractView(obj))
                    loggerWindow->setCurrentView(getAbstractView(obj));
                processingEvent = false;
                replay_q.pop();
            }
        }
        sTimer.start();
    }
    return false;
}

void __attribute__((constructor)) init()
{
    //std::cerr<<"Hey!! Starting from .so \n";
    appState = getenv(replayFileEnv)?REPLAY_MODE:RECORD_MODE;
    const char *optEnv = getenv(optionsAndValuesEnv);
    if(optEnv) readAndParseOptionsCsv(optEnv, toolOptions);
    if(appState == RECORD_MODE && !getenv(logFileEnv)) return;
    std::thread th([]{
        waitForQApp();
        QApplication::instance()->installEventFilter(&mainHandler);
        if(REPLAY_MODE == appState)
        {
            QFocusEvent *e = new QFocusEvent(QEvent::FocusIn);
            usleep(100);
            QApplication::instance()->postEvent(QApplication::instance(), e);
        }
        });
    th.detach();
}

void __attribute__((destructor)) dtor()
{
    if(loggerWindow) delete loggerWindow;
    //std::cerr<<"Done... \n";
}
