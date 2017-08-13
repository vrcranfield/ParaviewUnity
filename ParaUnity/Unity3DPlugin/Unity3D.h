#ifndef Unity3D_h
#define Unity3D_h

// Qt Includes
#include <QDir>
#include <QProcess>
#include <QTcpSocket>
#include <QActionGroup>
#include <QApplication>
#include <QBitmap>
#include <QFileInfo>
#include <QMessageBox>
#include <QMovie>
#include <QPixmap>
#include <QProcess>
#include <QSplashScreen>
#include <QStyle>
#include <QThread>
#include <QTime>

// VTK Includes
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderWindow.h>
#include <vtkSMRenderViewProxy.h>
#include <vtkSphereSource.h>
#include <vtkX3DExporter.h>
#include <vtkPVPluginsInformation.h>
#include <vtkSMPropertyHelper.h>
#include "vtkOutputWindow.h"

// pq Includes
#include "pqServerManagerModel.h"
#include "pqAnimationManager.h"
#include "pqActiveObjects.h"
#include "pqPluginManager.h"
#include "pqAnimationScene.h"
#include "pqApplicationCore.h"
#include "pqPVApplicationCore.h"
#include "pqPipelineSource.h"
#include "pqRenderView.h"
#include "pqServer.h"

// Libraries
#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>
#include <cstdio>

// Other project files
#include "LoadingSplashScreen.h"

// Plugin main class
class Unity3D : public QActionGroup
{
    Q_OBJECT
public:
    // Constructor
		Unity3D(QObject* p);

		// Exporting the scene
		void exportScene(pqServerManagerModel *sm, int port);
		void exportToUnityPlayer(pqServerManagerModel* sm);
		void exportToUnityEditor(pqServerManagerModel* sm);
		void writeSceneToMemory();
		void exportFirstFrame();
		void exportNextFrame();
		void freeSharedMemory();

		// Network
		bool pollClient(int port);
		bool sendMessage(const QString& message, int port);
		bool sendMessageExpectingReply(const QString & message, int port);
		
		// Fields
		HANDLE handle;
		char *pBuf;
		QTcpSocket *socket;
		char *objectName;
		unsigned long objectSize;
		int totalFrames;
		int lastExportedFrame;
		vtkX3DExporter *exporter = vtkX3DExporter::New();
		vtkSMRenderViewProxy *renderProxy;

private:
		// Fields
		int port;
    QProcess* unityPlayerProcess;
    QString workingDir;
    QString playerWorkingDir;

public slots:
		// Qt Slots
    void onAction(QAction* a);
		void readyRead();
};

// Forward declaration for static functions
static void LogDebug(QString message);
static quint64 getProcessID(const QProcess* proc);
static QString getUnityPlayerBinary(const QString& workingDir);
static int getPortNumberFrom(const QString& playerWorkingDir);
static bool fileExists(const QString& path);
static bool dirExists(const std::string& dirName_in);
static bool removeDir(const QString& dirName);
static int findPortFile(const QString& playerWorkingDir);

#endif // Unity3D_h

