#include "Unity3D.h"

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

#ifdef Q_OS_WIN

#endif

#include "LoadingSplashScreen.h"

#define UNITY_PLAYER_ACTION "UNITY_PLAYER_ACTION"

#define UNITY_EDITOR_ACTION "UNITY_EDITOR_ACTION"

//-----------------------------------------------------------------------------

bool Unity3D::sendMessage(const QString& message, int port) {
	socket = new QTcpSocket(this);
	socket->connectToHost("127.0.0.1", port);
	if (!socket->waitForConnected()) {
		return false;
	}

	// Print message to console
	vtkOutputWindow::GetInstance()->DisplayDebugText(std::string("Sending message: " + message.toStdString()).c_str());

	socket->write(QByteArray(message.toLatin1()));
	socket->waitForBytesWritten();
	return true;
}

//-----------------------------------------------------------------------------

bool Unity3D::sendMessageExpectingReply(const QString& message, int port) {
	
	bool result = sendMessage(message, port);
		
	if(result)
		QObject::connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()));

	return result;
}

//-----------------------------------------------------------------------------

void Unity3D::readyRead() {

	QString reply = QString(socket->readAll());

	vtkOutputWindow::GetInstance()->DisplayDebugText(std::string("Received reply: " + reply.toStdString()).c_str());

	if(reply.compare(QString("OK")) == 0)
		freeSharedMemory();
}

//-----------------------------------------------------------------------------

static quint64 getProcessID(const QProcess* proc) {

#ifdef Q_WS_WIN
	struct _PROCESS_INFORMATION* procinfo = proc->pid();
	return procinfo ? procinfo->dwProcessId : 0;
#else // other
	return (quint64)proc->pid();
#endif // Q_WS_WIN
}

//-----------------------------------------------------------------------------

static QString getUnityPlayerBinary(const QString& workingDir) {

	pqPVApplicationCore* core = pqPVApplicationCore::instance();
	pqPluginManager* pluginManager = core->getPluginManager();
	vtkPVPluginsInformation* localPlugins = pluginManager->loadedExtensions(pqActiveObjects::instance().activeServer(), false);

#if defined(Q_OS_MAC)
	QString targetFile = "unity_player.app/Contents/MacOS/unity_player";
#elif defined(Q_OS_WIN32)
	QString targetFile = "unity_player.exe";
#else
	QString targetFile = "unity_player";
#endif
    
	for (unsigned int i = 0; i < localPlugins->GetNumberOfPlugins(); i++) {
		QString pluginName(localPlugins->GetPluginName(i));
		if (pluginName == "Unity3D") {
			QDir pluginDir(localPlugins->GetPluginFileName(i));
			pluginDir.cdUp();
			return pluginDir.absolutePath() + "/" + targetFile;
        }
	}

	qFatal("Unable to resolve plugin location\n");
	return NULL;
}

//-----------------------------------------------------------------------------

static int getPortNumberFrom(const QString& playerWorkingDir) {
	QFileInfoList files = QDir(playerWorkingDir).entryInfoList();
	foreach(const QFileInfo &file, files) {
		if (!file.isDir() && file.baseName().contains("port")) {
			return file.baseName().mid(4).toInt();
		}
	}
	return 0;
}

//-----------------------------------------------------------------------------

bool fileExists(const QString& path) {
	QFileInfo check_file(path);
	// check if file exists and if yes: Is it really a file and no directory?
	return (check_file.exists() && check_file.isFile());
}

//-----------------------------------------------------------------------------

static bool removeDir(const QString& dirName)
{
	bool result = true;
	QDir dir(dirName);

	if (dir.exists()) {
		foreach(QFileInfo info, dir.entryInfoList(QDir::NoDotAndDotDot
			| QDir::System | QDir::Hidden | QDir::AllDirs
			| QDir::Files, QDir::DirsFirst)) {
			if (info.isDir()) {
				result = removeDir(info.absoluteFilePath());
			}
			else {
				result = QFile::remove(info.absoluteFilePath());
			}

			if (!result) {
				return result;
			}
		}
		result = QDir().rmdir(dirName);
	}
	return result;
}

//-----------------------------------------------------------------------------
static int findPortFile(const QString& playerWorkingDir) {
	/* Process startet, but we still
	 * have to wait until Unity
	 * initialization finished
	 */
	QWidget *window = QApplication::activeWindow();
	LoadingSplashScreen splashScreen(":/Unity3D/resources/loader.gif");
	int port = 0;

	window->setEnabled(false);
	splashScreen.show();

	QTime nextCheck = QTime::currentTime().addSecs(1);
	do {
		if (QTime::currentTime() > nextCheck) {
			port = getPortNumberFrom(playerWorkingDir);
			nextCheck = QTime::currentTime().addSecs(1);
		}
		QApplication::instance()->processEvents();
	} while (port == 0);

	splashScreen.hide();
	window->setEnabled(true);
	return port;
}

//-----------------------------------------------------------------------------
void Unity3D::exportSceneToFile(pqServerManagerModel *sm, 
	const QString& exportLocation, int port) {
	QList<pqRenderView *> renderViews = sm->findItems<pqRenderView *>();
	vtkSMRenderViewProxy *renderProxy = renderViews[0]->getRenderViewProxy();
	vtkSmartPointer<vtkX3DExporter> exporter =
		vtkSmartPointer<vtkX3DExporter>::New();

	pqPVApplicationCore* core = pqPVApplicationCore::instance();
	pqAnimationScene* scene = core->animationManager()->getActiveScene();

	QString message;

	if (scene->getTimeSteps().length() > 0) {
		vtkSMPropertyHelper animationProp(scene->getProxy(), "AnimationTime");
		double lastTime = animationProp.GetAsDouble();
		QString exportDir = exportLocation + "/paraview_output";
        
        QDir dir(exportDir);
        if (dir.exists()) {
            removeDir(dir.absolutePath());
        }
        QDir(exportLocation).mkdir("paraview_output");
        
		for (int i = 0; i < scene->getTimeSteps().length(); i++) {
			animationProp.Set(scene->getTimeSteps()[i]);
			scene->getProxy()->UpdateVTKObjects();
            
			QString exportFile = exportDir + "/frame_" + QString::number(i) + ".x3d";

			exporter->SetInput(renderProxy->GetRenderWindow());
			exporter->SetFileName(exportFile.toLatin1());
			exporter->Write();
		}

		animationProp.Set(lastTime);
		message = exportDir;
	}
	else {
		QString exportFile = exportLocation + "/paraview_output.x3d";
		if (fileExists(exportFile)) {
			QFile::remove(exportFile);
		}

		exporter->SetInput(renderProxy->GetRenderWindow());
		exporter->SetFileName(exportFile.toLatin1());
		exporter->Write();


		message = exportFile;
	}

	if (!sendMessage(message, port)) {
		QMessageBox::critical(NULL, tr("Unity Error"),
			tr("Unable to communicate to Unity process"));
	}
}

//-----------------------------------------------------------------------------
void Unity3D::exportSceneToSharedMemory(pqServerManagerModel *sm, const QString& exportLocation, int port) {
	
	QList<pqRenderView *> renderViews = sm->findItems<pqRenderView *>();
	
	vtkSMRenderViewProxy *renderProxy = renderViews[0]->getRenderViewProxy();
	
	vtkSmartPointer<vtkX3DExporter> exporter = vtkSmartPointer<vtkX3DExporter>::New();

	pqPVApplicationCore* core = pqPVApplicationCore::instance();
	pqAnimationScene* scene = core->animationManager()->getActiveScene();

	QString message;

	const char *objectName = "Global\\ParaviewOutput";

	// Write to a field in the exporter
	exporter->SetWriteToOutputString(1);

	// TODO enable for multiple frames
	if (scene->getTimeSteps().length() > 0) {
		vtkSMPropertyHelper animationProp(scene->getProxy(), "AnimationTime");
		double lastTime = animationProp.GetAsDouble();
		QString exportDir = exportLocation + "/paraview_output";

		QDir dir(exportDir);
		if (dir.exists()) {
			removeDir(dir.absolutePath());
		}
		QDir(exportLocation).mkdir("paraview_output");

		for (int i = 0; i < scene->getTimeSteps().length(); i++) {
			animationProp.Set(scene->getTimeSteps()[i]);
			scene->getProxy()->UpdateVTKObjects();

			QString exportFile = exportDir + "/frame_" + QString::number(i) + ".x3d";

			exporter->SetInput(renderProxy->GetRenderWindow());
			exporter->SetFileName(exportFile.toLatin1());
			exporter->Write();
		}

		animationProp.Set(lastTime);
		message = exportDir;
	}
	
	// Single frame case
	else {
		exporter->SetInput(renderProxy->GetRenderWindow());
		exporter->Write();

		message = objectName;
	}

	unsigned long objectSize = exporter->GetOutputStringLength() * sizeof(char);
	
	message += ";;" + QString::number(objectSize);

	handle = CreateFileMapping(
		INVALID_HANDLE_VALUE,									// use paging file
		NULL,																	// default security
		PAGE_READWRITE,												// read/write access
		0,																		// maximum object size (high-order DWORD)
		objectSize,    // maximum object size (low-order DWORD)
		objectName);													// name of mapping object

	if (!handle || handle == INVALID_HANDLE_VALUE)
	{
		QMessageBox::critical(NULL, tr("Unity 1"),
													tr(std::string("Could not create file mapping object: " + GetLastError()).c_str()));
		return;
	}

	pBuf = (char *)MapViewOfFile(handle,   // handle to map object
															 FILE_MAP_ALL_ACCESS, // read/write permission
															 0,
															 0,
															 objectSize);

	if (pBuf == NULL)
	{
		QMessageBox::critical(NULL, tr("Unity 2"),
													tr(std::string("Could not map view of file: " + GetLastError()).c_str()));
		CloseHandle(handle);
		return;
	}

	CopyMemory((void *)pBuf, exporter->GetOutputString(), objectSize);


	if (!sendMessageExpectingReply(message, port)) {
		QMessageBox::critical(NULL, tr("Unity 3"),
													tr("Unable to communicate to Unity process"));
	}
}


//-----------------------------------------------------------------------------
void Unity3D::freeSharedMemory() {
	if (pBuf != NULL) {
		UnmapViewOfFile(pBuf);
		pBuf = NULL;
	}

	if (handle != NULL) {
		CloseHandle(handle);
		handle = NULL;
	}
}


//-----------------------------------------------------------------------------
Unity3D::Unity3D(QObject *p) : QActionGroup(p), unityPlayerProcess(NULL) {
	this->workingDir = QDir::tempPath() + "/Unity3DPlugin";

	// Player mode
	QIcon embeddedActionIcon(QPixmap(":/Unity3D/resources/player.png"));
	embeddedActionIcon.addPixmap(
		QPixmap(":/Unity3D/resources/player_selected.png"),
		QIcon::Mode::Selected);
	QAction *embeddedAction =
		new QAction(embeddedActionIcon, "Show in Unity Player", this);
	embeddedAction->setData(UNITY_PLAYER_ACTION);
	this->addAction(embeddedAction);


	// Editor mode
	QIcon exportActionIcon(QPixmap(":/Unity3D/resources/editor.png"));
	exportActionIcon.addPixmap(QPixmap(":/Unity3D/resources/editor_selected.png"),
		QIcon::Mode::Selected);
	QAction *exportAction =
		new QAction(exportActionIcon, "Export to Unity Editor", this);
	exportAction->setData(UNITY_EDITOR_ACTION);
	this->addAction(exportAction);
	
	QObject::connect(this, SIGNAL(triggered(QAction *)), this,
		SLOT(onAction(QAction *)));
}

//-----------------------------------------------------------------------------
void Unity3D::onAction(QAction *a) {
	pqApplicationCore *core = pqApplicationCore::instance();
	pqServerManagerModel *sm = core->getServerManagerModel();

	if (sm->getNumberOfItems<pqServer *>()) {
		if (a->data() == UNITY_PLAYER_ACTION) {
			this->exportToUnityPlayer(sm);
		}
		else if (a->data() == UNITY_EDITOR_ACTION) {
			this->exportToUnityEditor(sm);
		}
		else {
			qFatal("Unexpected action type\n");
		}
	}
}

//-----------------------------------------------------------------------------
void Unity3D::exportToUnityPlayer(pqServerManagerModel *sm) {

	if (this->unityPlayerProcess == NULL ||
		this->unityPlayerProcess->pid() <= 0) {
		this->unityPlayerProcess = new QProcess(this);

		QString processBinary = getUnityPlayerBinary(this->workingDir);
		this->unityPlayerProcess->start(processBinary);
		if (!unityPlayerProcess->waitForStarted()) {
			QMessageBox::critical(NULL, tr("Unity Player Error"),
				tr("Player process could not be executed"));
			return;
		}

		this->playerWorkingDir = this->workingDir + "/Embedded/" +
			QString::number(getProcessID(this->unityPlayerProcess));

		this->port = findPortFile(playerWorkingDir);
	}

	exportSceneToSharedMemory(sm, this->playerWorkingDir, this->port);
}

//-----------------------------------------------------------------------------
void Unity3D::exportToUnityEditor(pqServerManagerModel *sm) {
	QString exportLocations(this->workingDir + "/Editor");

	QList<int> activeUnityInstances;
	foreach(const QString &dir, QDir(exportLocations).entryList()) {
		if (dir != "." && dir != "..") {
			int port = dir.toInt();

			if (sendMessage("TEST", port)) {
				activeUnityInstances << port;
			}
			else {
				removeDir(exportLocations + "/" + dir);
			}
		}
	}

	if (activeUnityInstances.isEmpty()) {
		QMessageBox::critical(NULL, tr("Unity Editor not running"),
			tr("Start a prepared Unity project first"));
	}
	else if (activeUnityInstances.length() > 1) {
		QMessageBox::critical(NULL, tr("Error"),
			tr("Multiple Unity instances are running at the same time"));
	}
	else {
		QString exportLocation = exportLocations + "/" + QString::number(activeUnityInstances[0]);
		exportSceneToSharedMemory(sm, exportLocation, activeUnityInstances[0]);
	}
}

