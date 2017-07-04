#ifndef Unity3D_h
#define Unity3D_h

#include <QDir>
#include <QProcess>
#include <QTcpSocket>
#include <QActionGroup>
#include "pqServerManagerModel.h"

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>
#include <cstdio>

class Unity3D : public QActionGroup
{
    Q_OBJECT
public:
    Unity3D(QObject* p);
private:
    QProcess* unityPlayerProcess;
    int port;
    QString workingDir;
    QString playerWorkingDir;
		void exportToUnityPlayer(pqServerManagerModel* sm);
    void exportToUnityEditor(pqServerManagerModel* sm);

		HANDLE handle;
		char *pBuf;
		QTcpSocket *socket;
public slots:
    void onAction(QAction* a);
	bool sendMessage(const QString&, int port);
	bool sendMessageExpectingReply(const QString & message, int port);
	void exportSceneToFile(pqServerManagerModel * sm, const QString & exportLocation, int port);
	void exportSceneToSharedMemory(pqServerManagerModel *sm, const QString& exportLocation, int port);
	void freeSharedMemory();
	void readyRead();
};

#endif // Unity3D_h

