#ifndef Unity3D_h
#define Unity3D_h

#include <QDir>
#include <QProcess>
#include <QTcpSocket>
#include <QActionGroup>
#include "pqServerManagerModel.h"

#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderWindow.h>
#include <vtkSMRenderViewProxy.h>
#include <vtkSphereSource.h>
#include <vtkX3DExporter.h>
#include <vtkPVPluginsInformation.h>
#include <vtkSMPropertyHelper.h>
#include "vtkOutputWindow.h"

#include "pqAnimationManager.h"
#include "pqActiveObjects.h"
#include "pqPluginManager.h"
#include "pqAnimationScene.h"
#include "pqApplicationCore.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqServer.h"

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
		bool pollClient(int port);
		bool sendMessage(const QString& message, int port);
		bool sendMessageExpectingReply(const QString & message, int port);
		void exportSceneToFile(pqServerManagerModel * sm, const QString& exportLocation, int port);
		void exportSceneToSharedMemory(pqServerManagerModel *sm, int port);
		void writeExporterStringToSharedMemory();
		void exportFirstFrame();
		void exportNextFrame();
		void freeSharedMemory();

		HANDLE previousHandle;
		char *previousBuf;
		HANDLE currentHandle;
		char *currentBuf;
		QTcpSocket *socket;

		char *objectNameRoot;
		char *objectName;
		unsigned long objectSize;

		int totalFrames;
		int lastExportedFrame;

		vtkX3DExporter *exporter = vtkX3DExporter::New();
		vtkSMRenderViewProxy *renderProxy;

private:
    QProcess* unityPlayerProcess;
    int port;
    QString workingDir;
    QString playerWorkingDir;
		void exportToUnityPlayer(pqServerManagerModel* sm);
    void exportToUnityEditor(pqServerManagerModel* sm);

		// Fields for Shared Memory allocations
public slots:
    void onAction(QAction* a);
		void readyRead();
};

#endif // Unity3D_h

